// ============================================================================
// D-Engine - Source/Core/Audio/WinMmAudio.cpp
// ----------------------------------------------------------------------------
// Purpose : WinMM waveOut backend implementation for Audio contract M2.
// Contract: No exceptions/RTTI. Uses fixed-size internal buffers only. Device
//           lifecycle is explicit via Init/Shutdown and is idempotent.
// Notes   : Provides a simple PCM16 WAV load path and deterministic slot-order
//           voice mixing. No dynamic allocation is performed in Mix().
// ============================================================================

#include "Core/Audio/WinMmAudio.hpp"

#include "Core/Platform/PlatformDefines.hpp"

#include <cstring>

#if DNG_PLATFORM_WINDOWS
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <mmsystem.h>
#    pragma comment(lib, "winmm.lib")
#endif

namespace dng::audio
{
dng::i16 WinMmAudio::s_ClipSamplePool[WinMmAudio::kMaxClipSamplePool]{};
WinMmAudio::StreamClipState WinMmAudio::s_StreamClips[WinMmAudio::kMaxStreamClips]{};
bool WinMmAudio::s_GlobalClipPoolInUse = false;

namespace
{
    [[nodiscard]] constexpr dng::u32 MaxU32() noexcept
    {
        return ~dng::u32{0};
    }

    [[nodiscard]] inline dng::u16 ReadLe16(const dng::u8* data) noexcept
    {
        return static_cast<dng::u16>(data[0]) |
               static_cast<dng::u16>(static_cast<dng::u16>(data[1]) << 8u);
    }

    [[nodiscard]] inline dng::u32 ReadLe32(const dng::u8* data) noexcept
    {
        return static_cast<dng::u32>(data[0]) |
               static_cast<dng::u32>(static_cast<dng::u32>(data[1]) << 8u) |
               static_cast<dng::u32>(static_cast<dng::u32>(data[2]) << 16u) |
               static_cast<dng::u32>(static_cast<dng::u32>(data[3]) << 24u);
    }

    [[nodiscard]] inline dng::i16 FloatToPcm16(float value) noexcept
    {
        float clamped = value;
        if (clamped > 1.0f)
        {
            clamped = 1.0f;
        }
        else if (clamped < -1.0f)
        {
            clamped = -1.0f;
        }

        constexpr float kScale = 32767.0f;
        const dng::i32 pcm = static_cast<dng::i32>(clamped * kScale);
        return static_cast<dng::i16>(pcm);
    }

    [[nodiscard]] inline float Pcm16ToFloat(dng::i16 value) noexcept
    {
        constexpr float kInvScale = 1.0f / 32768.0f;
        return static_cast<float>(value) * kInvScale;
    }

    [[nodiscard]] inline float ClampUnit(float value) noexcept
    {
        if (value > 1.0f)
        {
            return 1.0f;
        }
        if (value < -1.0f)
        {
            return -1.0f;
        }
        return value;
    }

    [[nodiscard]] inline float Lerp(float a, float b, float t) noexcept
    {
        return a + ((b - a) * t);
    }

    struct WavPcm16Info
    {
        dng::u16 channelCount = 0;
        dng::u32 sampleRate = 0;
        dng::u64 dataOffsetBytes = 0;
        dng::u32 dataSizeBytes = 0;
    };

    [[nodiscard]] inline AudioStatus MapFsStatusToAudioStatus(fs::FsStatus status) noexcept
    {
        switch (status)
        {
            case fs::FsStatus::Ok:           return AudioStatus::Ok;
            case fs::FsStatus::InvalidArg:   return AudioStatus::InvalidArg;
            case fs::FsStatus::NotSupported: return AudioStatus::NotSupported;
            case fs::FsStatus::UnknownError: return AudioStatus::UnknownError;
            case fs::FsStatus::NotFound:
            case fs::FsStatus::AccessDenied: return AudioStatus::NotSupported;
            default:                         return AudioStatus::UnknownError;
        }
    }

    [[nodiscard]] inline bool IsSupportedPcmFormat(dng::u16 fmtTag,
                                                   dng::u16 channels,
                                                   dng::u32 sampleRate,
                                                   dng::u16 bitsPerSample) noexcept
    {
        return fmtTag == 1u &&
               bitsPerSample == 16u &&
               (channels == 1u || channels == 2u) &&
               sampleRate != 0u;
    }

    [[nodiscard]] inline AudioStatus ParseWavPcm16FromMemory(const dng::u8* fileData,
                                                             dng::u32 fileSizeBytes,
                                                             WavPcm16Info& outInfo) noexcept
    {
        outInfo = WavPcm16Info{};
        if (fileData == nullptr || fileSizeBytes < 12u)
        {
            return AudioStatus::InvalidArg;
        }

        if (std::memcmp(fileData, "RIFF", 4) != 0 ||
            std::memcmp(&fileData[8], "WAVE", 4) != 0)
        {
            return AudioStatus::InvalidArg;
        }

        bool fmtFound = false;
        dng::u16 fmtTag = 0;
        dng::u16 fmtChannels = 0;
        dng::u32 fmtSampleRate = 0;
        dng::u16 fmtBitsPerSample = 0;

        dng::u32 cursor = 12u;
        while (cursor + 8u <= fileSizeBytes)
        {
            const dng::u8* chunkHeader = &fileData[cursor];
            const dng::u32 chunkSize = ReadLe32(&chunkHeader[4]);
            cursor += 8u;

            if (chunkSize > (fileSizeBytes - cursor))
            {
                return AudioStatus::InvalidArg;
            }

            const dng::u8* chunkData = &fileData[cursor];
            const bool hasPadByte = (chunkSize & 1u) != 0u;

            if (std::memcmp(chunkHeader, "fmt ", 4) == 0)
            {
                if (chunkSize < 16u)
                {
                    return AudioStatus::InvalidArg;
                }

                fmtTag = ReadLe16(&chunkData[0]);
                fmtChannels = ReadLe16(&chunkData[2]);
                fmtSampleRate = ReadLe32(&chunkData[4]);
                fmtBitsPerSample = ReadLe16(&chunkData[14]);
                fmtFound = true;
            }
            else if (std::memcmp(chunkHeader, "data", 4) == 0)
            {
                if (!fmtFound ||
                    !IsSupportedPcmFormat(fmtTag, fmtChannels, fmtSampleRate, fmtBitsPerSample) ||
                    (chunkSize % 2u) != 0u)
                {
                    return AudioStatus::InvalidArg;
                }

                const dng::u32 sampleCount = chunkSize / 2u;
                if (sampleCount == 0u ||
                    (sampleCount % static_cast<dng::u32>(fmtChannels)) != 0u)
                {
                    return AudioStatus::InvalidArg;
                }

                outInfo.channelCount = fmtChannels;
                outInfo.sampleRate = fmtSampleRate;
                outInfo.dataOffsetBytes = cursor;
                outInfo.dataSizeBytes = chunkSize;
                return AudioStatus::Ok;
            }

            const dng::u32 nextCursor = cursor + chunkSize + (hasPadByte ? 1u : 0u);
            if (nextCursor < cursor || nextCursor > fileSizeBytes)
            {
                return AudioStatus::InvalidArg;
            }
            cursor = nextCursor;
        }

        return AudioStatus::InvalidArg;
    }

    [[nodiscard]] inline bool ReadFileRangeExact(fs::FileSystemInterface& fileSystem,
                                                 fs::PathView path,
                                                 dng::u64 offsetBytes,
                                                 void* dst,
                                                 dng::u64 dstSizeBytes,
                                                 fs::FsStatus& outStatus) noexcept
    {
        outStatus = fs::FsStatus::UnknownError;
        if (dst == nullptr && dstSizeBytes != 0u)
        {
            outStatus = fs::FsStatus::InvalidArg;
            return false;
        }

        dng::u64 bytesRead = 0;
        outStatus = fs::ReadFileRange(fileSystem, path, offsetBytes, dst, dstSizeBytes, bytesRead);
        return outStatus == fs::FsStatus::Ok && bytesRead == dstSizeBytes;
    }

    [[nodiscard]] inline AudioStatus ParseWavPcm16FromFile(fs::FileSystemInterface& fileSystem,
                                                           fs::PathView path,
                                                           WavPcm16Info& outInfo) noexcept
    {
        outInfo = WavPcm16Info{};

        dng::u64 fileSize = 0;
        const fs::FsStatus sizeStatus = fs::FileSize(fileSystem, path, fileSize);
        if (sizeStatus != fs::FsStatus::Ok)
        {
            return MapFsStatusToAudioStatus(sizeStatus);
        }

        if (fileSize < 12u)
        {
            return AudioStatus::InvalidArg;
        }

        dng::u8 riffHeader[12]{};
        fs::FsStatus readStatus = fs::FsStatus::UnknownError;
        if (!ReadFileRangeExact(fileSystem, path, 0u, riffHeader, sizeof(riffHeader), readStatus))
        {
            return (readStatus == fs::FsStatus::Ok)
                ? AudioStatus::InvalidArg
                : MapFsStatusToAudioStatus(readStatus);
        }

        if (std::memcmp(riffHeader, "RIFF", 4) != 0 ||
            std::memcmp(&riffHeader[8], "WAVE", 4) != 0)
        {
            return AudioStatus::InvalidArg;
        }

        bool fmtFound = false;
        dng::u16 fmtTag = 0;
        dng::u16 fmtChannels = 0;
        dng::u32 fmtSampleRate = 0;
        dng::u16 fmtBitsPerSample = 0;

        dng::u64 cursor = 12u;
        while (cursor + 8u <= fileSize)
        {
            dng::u8 chunkHeader[8]{};
            if (!ReadFileRangeExact(fileSystem, path, cursor, chunkHeader, sizeof(chunkHeader), readStatus))
            {
                return (readStatus == fs::FsStatus::Ok)
                    ? AudioStatus::InvalidArg
                    : MapFsStatusToAudioStatus(readStatus);
            }
            cursor += 8u;

            const dng::u32 chunkSize = ReadLe32(&chunkHeader[4]);
            if (static_cast<dng::u64>(chunkSize) > (fileSize - cursor))
            {
                return AudioStatus::InvalidArg;
            }

            const bool hasPadByte = (chunkSize & 1u) != 0u;

            if (std::memcmp(chunkHeader, "fmt ", 4) == 0)
            {
                if (chunkSize < 16u)
                {
                    return AudioStatus::InvalidArg;
                }

                dng::u8 fmtChunk[16]{};
                if (!ReadFileRangeExact(fileSystem, path, cursor, fmtChunk, sizeof(fmtChunk), readStatus))
                {
                    return (readStatus == fs::FsStatus::Ok)
                        ? AudioStatus::InvalidArg
                        : MapFsStatusToAudioStatus(readStatus);
                }

                fmtTag = ReadLe16(&fmtChunk[0]);
                fmtChannels = ReadLe16(&fmtChunk[2]);
                fmtSampleRate = ReadLe32(&fmtChunk[4]);
                fmtBitsPerSample = ReadLe16(&fmtChunk[14]);
                fmtFound = true;
            }
            else if (std::memcmp(chunkHeader, "data", 4) == 0)
            {
                if (!fmtFound ||
                    !IsSupportedPcmFormat(fmtTag, fmtChannels, fmtSampleRate, fmtBitsPerSample) ||
                    (chunkSize % 2u) != 0u)
                {
                    return AudioStatus::InvalidArg;
                }

                const dng::u64 sampleCount = static_cast<dng::u64>(chunkSize / 2u);
                if (sampleCount == 0u ||
                    (sampleCount % static_cast<dng::u64>(fmtChannels)) != 0u)
                {
                    return AudioStatus::InvalidArg;
                }

                outInfo.channelCount = fmtChannels;
                outInfo.sampleRate = fmtSampleRate;
                outInfo.dataOffsetBytes = cursor;
                outInfo.dataSizeBytes = chunkSize;
                return AudioStatus::Ok;
            }

            const dng::u64 nextCursor = cursor + static_cast<dng::u64>(chunkSize) + (hasPadByte ? 1u : 0u);
            if (nextCursor < cursor || nextCursor > fileSize)
            {
                return AudioStatus::InvalidArg;
            }
            cursor = nextCursor;
        }

        return AudioStatus::InvalidArg;
    }
} // namespace

AudioClipId WinMmAudio::AllocateClipId() noexcept
{
    for (dng::u32 attempt = 0; attempt < kMaxClips; ++attempt)
    {
        const dng::u32 value = ((m_NextClipValue - 1u + attempt) % kMaxClips) + 1u;
        const dng::u32 index = value - 1u;
        if (!m_Clips[index].valid)
        {
            m_NextClipValue = (value % kMaxClips) + 1u;
            return MakeAudioClipId(value);
        }
    }

    return AudioClipId{};
}

bool WinMmAudio::IsSameInterface(const fs::FileSystemInterface& fileSystem) const noexcept
{
    return m_StreamFileSystem.userData == fileSystem.userData &&
           m_StreamFileSystem.vtable.exists == fileSystem.vtable.exists &&
           m_StreamFileSystem.vtable.fileSize == fileSystem.vtable.fileSize &&
           m_StreamFileSystem.vtable.readFile == fileSystem.vtable.readFile &&
           m_StreamFileSystem.vtable.readFileRange == fileSystem.vtable.readFileRange &&
           m_StreamFileSystem.vtable.getCaps == fileSystem.vtable.getCaps;
}

dng::u32 WinMmAudio::ToBusIndex(AudioBus bus) const noexcept
{
    if (!IsValid(bus))
    {
        return static_cast<dng::u32>(AudioBus::Master);
    }
    return static_cast<dng::u32>(bus);
}

void WinMmAudio::ResetVoiceForInvalidClip(VoiceState& voice) noexcept
{
    voice.active = false;
    voice.paused = false;
    voice.loop = false;
    voice.stopAfterGainRamp = false;
    voice.clip = AudioClipId{};
    voice.frameCursor = 0.0;
    voice.currentGain = 0.0f;
    voice.targetGain = 1.0f;
    voice.gainStepPerFrame = 0.0f;
    voice.gainRampFramesRemaining = 0;
    voice.pitch = 1.0f;
    voice.bus = AudioBus::Master;
}

dng::u16 WinMmAudio::AllocateStreamSlot() noexcept
{
    for (dng::u16 i = 0; i < kMaxStreamClips; ++i)
    {
        if (!s_StreamClips[i].valid)
        {
            return i;
        }
    }
    return kInvalidStreamSlot;
}

bool WinMmAudio::EnsureStreamCache(StreamClipState& streamState,
                                   dng::u32 sourceFrame) noexcept
{
    if (!streamState.valid ||
        !m_HasStreamFileSystem ||
        streamState.channelCount == 0u ||
        streamState.sampleRate == 0u ||
        streamState.frameCount == 0u ||
        sourceFrame >= streamState.frameCount)
    {
        return false;
    }

    if (streamState.cacheValid &&
        sourceFrame >= streamState.cacheStartFrame &&
        sourceFrame < (streamState.cacheStartFrame + streamState.cacheFrameCount))
    {
        return true;
    }

    const dng::u32 framesToCache = kStreamCacheFrames;
    const dng::u32 loadStartFrame = (sourceFrame / framesToCache) * framesToCache;
    dng::u32 loadFrameCount = streamState.frameCount - loadStartFrame;
    if (loadFrameCount > framesToCache)
    {
        loadFrameCount = framesToCache;
    }
    if (loadFrameCount == 0u)
    {
        return false;
    }

    const dng::u64 bytesPerFrame = static_cast<dng::u64>(streamState.channelCount) * sizeof(dng::i16);
    const dng::u64 byteOffset = streamState.dataOffsetBytes +
        (static_cast<dng::u64>(loadStartFrame) * bytesPerFrame);
    const dng::u64 byteCount = static_cast<dng::u64>(loadFrameCount) * bytesPerFrame;

    fs::PathView path{};
    path.data = streamState.path;
    path.size = streamState.pathSize;

    dng::u64 bytesRead = 0;
    const fs::FsStatus readStatus = fs::ReadFileRange(m_StreamFileSystem,
                                                      path,
                                                      byteOffset,
                                                      streamState.cacheSamples,
                                                      byteCount,
                                                      bytesRead);
    if (readStatus != fs::FsStatus::Ok || bytesRead != byteCount)
    {
        return false;
    }

    streamState.cacheStartFrame = loadStartFrame;
    streamState.cacheFrameCount = loadFrameCount;
    streamState.cacheValid = true;
    return true;
}

bool WinMmAudio::HasClip(AudioClipId clip) const noexcept
{
    if (!IsValid(clip) || clip.value > kMaxClips)
    {
        return false;
    }

    const dng::u32 index = clip.value - 1u;
    return m_Clips[index].valid;
}

AudioStatus WinMmAudio::LoadWavPcm16Clip(const dng::u8* fileData,
                                         dng::u32 fileSizeBytes,
                                         AudioClipId& outClip) noexcept
{
    outClip = AudioClipId{};
    if (!m_IsInitialized || fileData == nullptr || fileSizeBytes < 12u)
    {
        return AudioStatus::InvalidArg;
    }

    WavPcm16Info info{};
    const AudioStatus parseStatus = ParseWavPcm16FromMemory(fileData, fileSizeBytes, info);
    if (parseStatus != AudioStatus::Ok)
    {
        return parseStatus;
    }

    const dng::u32 sampleCount = info.dataSizeBytes / 2u;
    AudioClipId clip = AllocateClipId();
    if (!IsValid(clip))
    {
        return AudioStatus::NotSupported;
    }

    const dng::u32 clipIndex = clip.value - 1u;
    if (sampleCount > (kMaxClipSamplePool - m_NextClipSample))
    {
        return AudioStatus::NotSupported;
    }

    const dng::u32 sampleOffset = m_NextClipSample;
    const dng::u64 endOffset = info.dataOffsetBytes + info.dataSizeBytes;
    if (endOffset > static_cast<dng::u64>(fileSizeBytes))
    {
        return AudioStatus::InvalidArg;
    }

    const dng::u8* pcmData = fileData + static_cast<size_t>(info.dataOffsetBytes);
    std::memcpy(&s_ClipSamplePool[sampleOffset], pcmData, static_cast<size_t>(info.dataSizeBytes));

    ClipState& clipState = m_Clips[clipIndex];
    clipState.valid = true;
    clipState.storage = ClipStorageKind::Memory;
    clipState.channelCount = info.channelCount;
    clipState.streamSlot = kInvalidStreamSlot;
    clipState.sampleRate = info.sampleRate;
    clipState.sampleOffset = sampleOffset;
    clipState.sampleCount = sampleCount;
    clipState.streamFrameCount = 0;
    m_NextClipSample += sampleCount;
    ++m_LoadedClipCount;

    outClip = clip;
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::BindStreamFileSystem(fs::FileSystemInterface& fileSystem) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (fileSystem.userData == nullptr ||
        fileSystem.vtable.getCaps == nullptr ||
        fileSystem.vtable.exists == nullptr ||
        fileSystem.vtable.fileSize == nullptr ||
        fileSystem.vtable.readFile == nullptr ||
        fileSystem.vtable.readFileRange == nullptr)
    {
        return AudioStatus::InvalidArg;
    }

    if (m_HasStreamFileSystem && !IsSameInterface(fileSystem))
    {
        // Stream FS cannot be swapped while backend is active.
        return AudioStatus::NotSupported;
    }

    m_StreamFileSystem = fileSystem;
    m_HasStreamFileSystem = true;
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::UnbindStreamFileSystem() noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (m_LoadedStreamClipCount != 0u)
    {
        // Keep FS alive while streamed clips still reference its paths.
        return AudioStatus::NotSupported;
    }

    m_StreamFileSystem = fs::FileSystemInterface{};
    m_HasStreamFileSystem = false;
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::LoadWavPcm16StreamClip(fs::PathView path,
                                               AudioClipId& outClip) noexcept
{
    outClip = AudioClipId{};
    if (!m_IsInitialized || !m_HasStreamFileSystem || path.data == nullptr || path.size == 0u)
    {
        return AudioStatus::InvalidArg;
    }
    if (path.size >= kMaxStreamPathBytes)
    {
        return AudioStatus::NotSupported;
    }

    WavPcm16Info info{};
    const AudioStatus parseStatus = ParseWavPcm16FromFile(m_StreamFileSystem, path, info);
    if (parseStatus != AudioStatus::Ok)
    {
        return parseStatus;
    }

    const dng::u64 sampleCount64 = static_cast<dng::u64>(info.dataSizeBytes / 2u);
    const dng::u64 frameCount64 = sampleCount64 / static_cast<dng::u64>(info.channelCount);
    if (frameCount64 == 0u || frameCount64 > static_cast<dng::u64>(MaxU32()))
    {
        return AudioStatus::NotSupported;
    }

    const dng::u16 streamSlot = AllocateStreamSlot();
    if (streamSlot == kInvalidStreamSlot)
    {
        return AudioStatus::NotSupported;
    }

    AudioClipId clip = AllocateClipId();
    if (!IsValid(clip))
    {
        return AudioStatus::NotSupported;
    }

    StreamClipState& streamState = s_StreamClips[streamSlot];
    streamState = StreamClipState{};
    streamState.valid = true;
    streamState.channelCount = info.channelCount;
    streamState.pathSize = static_cast<dng::u16>(path.size);
    streamState.sampleRate = info.sampleRate;
    streamState.frameCount = static_cast<dng::u32>(frameCount64);
    streamState.dataOffsetBytes = info.dataOffsetBytes;
    streamState.dataSizeBytes = info.dataSizeBytes;
    streamState.cacheStartFrame = 0;
    streamState.cacheFrameCount = 0;
    streamState.cacheValid = false;
    std::memcpy(streamState.path, path.data, static_cast<size_t>(path.size));
    streamState.path[path.size] = '\0';

    const dng::u32 clipIndex = clip.value - 1u;
    ClipState& clipState = m_Clips[clipIndex];
    clipState.valid = true;
    clipState.storage = ClipStorageKind::Stream;
    clipState.channelCount = info.channelCount;
    clipState.streamSlot = streamSlot;
    clipState.sampleRate = info.sampleRate;
    clipState.sampleOffset = 0;
    clipState.sampleCount = 0;
    clipState.streamFrameCount = static_cast<dng::u32>(frameCount64);

    ++m_LoadedStreamClipCount;
    ++m_LoadedClipCount;
    outClip = clip;
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::UnloadClip(AudioClipId clip) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(clip) || clip.value > kMaxClips)
    {
        return AudioStatus::InvalidArg;
    }

    const dng::u32 clipIndex = clip.value - 1u;
    const ClipState removedClip = m_Clips[clipIndex];
    if (!removedClip.valid)
    {
        return AudioStatus::InvalidArg;
    }

    for (dng::u32 voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
    {
        VoiceState& voice = m_Voices[voiceIndex];
        if (!voice.active || voice.clip.value != clip.value)
        {
            continue;
        }

        ResetVoiceForInvalidClip(voice);
        ++voice.generation;
        if (voice.generation == 0)
        {
            voice.generation = 1;
        }
    }

    if (removedClip.storage == ClipStorageKind::Memory)
    {
        const dng::u32 removeOffset = removedClip.sampleOffset;
        const dng::u32 removeSamples = removedClip.sampleCount;
        const dng::u32 tailOffset = removeOffset + removeSamples;
        if (tailOffset > m_NextClipSample)
        {
            return AudioStatus::UnknownError;
        }

        const dng::u32 tailSamples = m_NextClipSample - tailOffset;
        if (tailSamples > 0)
        {
            std::memmove(&s_ClipSamplePool[removeOffset],
                         &s_ClipSamplePool[tailOffset],
                         static_cast<size_t>(tailSamples) * sizeof(dng::i16));
        }

        if (removeSamples > 0 && m_NextClipSample >= removeSamples)
        {
            std::memset(&s_ClipSamplePool[m_NextClipSample - removeSamples],
                        0,
                        static_cast<size_t>(removeSamples) * sizeof(dng::i16));
        }

        for (dng::u32 i = 0; i < kMaxClips; ++i)
        {
            ClipState& clipState = m_Clips[i];
            if (!clipState.valid ||
                clipState.storage != ClipStorageKind::Memory ||
                clipState.sampleOffset <= removeOffset)
            {
                continue;
            }
            clipState.sampleOffset -= removeSamples;
        }

        m_NextClipSample -= removeSamples;
    }
    else if (removedClip.storage == ClipStorageKind::Stream)
    {
        if (removedClip.streamSlot >= kMaxStreamClips ||
            !s_StreamClips[removedClip.streamSlot].valid)
        {
            return AudioStatus::UnknownError;
        }

        s_StreamClips[removedClip.streamSlot] = StreamClipState{};
        if (m_LoadedStreamClipCount > 0)
        {
            --m_LoadedStreamClipCount;
        }
    }

    m_Clips[clipIndex] = ClipState{};
    if (m_LoadedClipCount > 0)
    {
        --m_LoadedClipCount;
    }

    return AudioStatus::Ok;
}

bool WinMmAudio::Init(const WinMmAudioConfig& config) noexcept
{
    Shutdown();

#if !DNG_PLATFORM_WINDOWS
    (void)config;
    return false;
#else
    static_assert(sizeof(WAVEHDR) <= kWaveHeaderStorageBytes,
                  "WinMmAudio wave header storage is too small.");

    if (config.sampleRate == 0 ||
        config.channelCount == 0 ||
        config.channelCount > kMaxChannels ||
        config.framesPerBuffer == 0 ||
        config.framesPerBuffer > kMaxFramesPerBuffer)
    {
        return false;
    }

    if (s_GlobalClipPoolInUse)
    {
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(config.channelCount);
    format.nSamplesPerSec = config.sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>((format.wBitsPerSample / 8u) * format.nChannels);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;

    HWAVEOUT device = nullptr;
    const MMRESULT openResult = ::waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (openResult != MMSYSERR_NOERROR)
    {
        return false;
    }

    s_GlobalClipPoolInUse = true;
    m_OwnsGlobalClipPool = true;
    std::memset(s_ClipSamplePool, 0, sizeof(s_ClipSamplePool));
    std::memset(s_StreamClips, 0, sizeof(s_StreamClips));

    m_Device = device;
    m_SampleRate = config.sampleRate;
    m_ChannelCount = config.channelCount;
    m_FramesPerBuffer = config.framesPerBuffer;
    m_IsInitialized = true;
    m_NextBufferIndex = 0;
    m_NextClipValue = 1;
    m_NextClipSample = 0;
    m_LoadedClipCount = 0;
    m_LoadedStreamClipCount = 0;
    m_StreamFileSystem = fs::FileSystemInterface{};
    m_HasStreamFileSystem = false;
    m_BusGains[0] = 1.0f;
    m_BusGains[1] = 1.0f;
    m_BusGains[2] = 1.0f;

    const dng::u32 bytesPerBuffer =
        m_FramesPerBuffer * static_cast<dng::u32>(m_ChannelCount) * static_cast<dng::u32>(sizeof(dng::i16));

    for (dng::u32 i = 0; i < kBufferCount; ++i)
    {
        std::memset(m_WaveHeaders[i], 0, kWaveHeaderStorageBytes);
        WAVEHDR* header = reinterpret_cast<WAVEHDR*>(m_WaveHeaders[i]);
        header->lpData = reinterpret_cast<LPSTR>(m_PcmBuffers[i]);
        header->dwBufferLength = static_cast<DWORD>(bytesPerBuffer);
        header->dwFlags = 0;
        header->dwLoops = 0;

        const MMRESULT prepResult = ::waveOutPrepareHeader(device, header, static_cast<UINT>(sizeof(WAVEHDR)));
        if (prepResult != MMSYSERR_NOERROR)
        {
            Shutdown();
            return false;
        }

        m_HeaderPrepared[i] = true;
        m_InFlight[i] = false;
    }

    return true;
#endif
}

void WinMmAudio::Shutdown() noexcept
{
#if DNG_PLATFORM_WINDOWS
    HWAVEOUT device = static_cast<HWAVEOUT>(m_Device);
    if (device != nullptr)
    {
        (void)::waveOutReset(device);

        for (dng::u32 i = 0; i < kBufferCount; ++i)
        {
            if (!m_HeaderPrepared[i])
            {
                continue;
            }

            WAVEHDR* header = reinterpret_cast<WAVEHDR*>(m_WaveHeaders[i]);
            (void)::waveOutUnprepareHeader(device, header, static_cast<UINT>(sizeof(WAVEHDR)));
        }

        (void)::waveOutClose(device);
    }
#endif

    m_Device = nullptr;
    m_NextBufferIndex = 0;
    m_NextClipValue = 1;
    m_NextClipSample = 0;
    m_LoadedClipCount = 0;
    m_LoadedStreamClipCount = 0;
    m_FramesPerBuffer = 1024;
    m_SampleRate = 48000;
    m_ChannelCount = 2;
    m_StreamFileSystem = fs::FileSystemInterface{};
    m_HasStreamFileSystem = false;
    m_BusGains[0] = 1.0f;
    m_BusGains[1] = 1.0f;
    m_BusGains[2] = 1.0f;
    m_IsInitialized = false;
    m_UnderrunCount = 0;
    m_SubmitErrorCount = 0;
    std::memset(m_WaveHeaders, 0, sizeof(m_WaveHeaders));
    std::memset(m_PcmBuffers, 0, sizeof(m_PcmBuffers));

    if (m_OwnsGlobalClipPool)
    {
        std::memset(s_ClipSamplePool, 0, sizeof(s_ClipSamplePool));
        std::memset(s_StreamClips, 0, sizeof(s_StreamClips));
        s_GlobalClipPoolInUse = false;
        m_OwnsGlobalClipPool = false;
    }

    std::memset(m_Clips, 0, sizeof(m_Clips));
    std::memset(m_Voices, 0, sizeof(m_Voices));
    std::memset(m_HeaderPrepared, 0, sizeof(m_HeaderPrepared));
    std::memset(m_InFlight, 0, sizeof(m_InFlight));

    for (dng::u32 i = 0; i < kMaxVoices; ++i)
    {
        m_Voices[i].currentGain = 0.0f;
        m_Voices[i].targetGain = 1.0f;
        m_Voices[i].gainStepPerFrame = 0.0f;
        m_Voices[i].pitch = 1.0f;
        m_Voices[i].gainRampFramesRemaining = 0;
        m_Voices[i].stopAfterGainRamp = false;
        m_Voices[i].generation = 1;
    }
}

AudioCaps WinMmAudio::GetCaps() const noexcept
{
    AudioCaps caps{};
    if (!m_IsInitialized)
    {
        return caps;
    }

    caps.determinism = dng::DeterminismMode::Off;
    caps.threadSafety = dng::ThreadSafetyMode::ExternalSync;
    caps.stableMixOrder = true;
    return caps;
}

AudioStatus WinMmAudio::Play(AudioVoiceId voice, const AudioPlayParams& params) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) ||
        voice.slot >= kMaxVoices ||
        !IsValid(params.clip) ||
        !HasClip(params.clip) ||
        !IsValid(params.bus) ||
        !(params.gain >= 0.0f) ||
        !(params.pitch > 0.0f))
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    voiceState = VoiceState{};
    voiceState.clip = params.clip;
    voiceState.frameCursor = 0.0;
    voiceState.currentGain = 0.0f;
    voiceState.targetGain = params.gain;
    voiceState.gainRampFramesRemaining = kGainRampFrames;
    voiceState.gainStepPerFrame = params.gain / static_cast<float>(kGainRampFrames);
    if (params.gain <= 0.0f)
    {
        voiceState.currentGain = params.gain;
        voiceState.gainRampFramesRemaining = 0;
        voiceState.gainStepPerFrame = 0.0f;
    }
    voiceState.pitch = params.pitch;
    voiceState.generation = voice.generation;
    voiceState.stopAfterGainRamp = false;
    voiceState.active = true;
    voiceState.paused = false;
    voiceState.loop = params.loop;
    voiceState.bus = params.bus;
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::Stop(AudioVoiceId voice) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) || voice.slot >= kMaxVoices)
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    if (!voiceState.active || voiceState.generation != voice.generation)
    {
        // Stop is idempotent for stale handles because async stream faults can
        // retire a voice before the queued stop command is flushed.
        return AudioStatus::Ok;
    }

    voiceState.paused = false;
    voiceState.stopAfterGainRamp = true;
    voiceState.targetGain = 0.0f;
    if (voiceState.currentGain <= 0.0f)
    {
        ResetVoiceForInvalidClip(voiceState);
        return AudioStatus::Ok;
    }

    voiceState.gainRampFramesRemaining = kGainRampFrames;
    voiceState.gainStepPerFrame = -voiceState.currentGain / static_cast<float>(kGainRampFrames);
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::Pause(AudioVoiceId voice) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) || voice.slot >= kMaxVoices)
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    if (!voiceState.active || voiceState.generation != voice.generation)
    {
        return AudioStatus::InvalidArg;
    }

    voiceState.paused = true;
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::Resume(AudioVoiceId voice) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) || voice.slot >= kMaxVoices)
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    if (!voiceState.active || voiceState.generation != voice.generation)
    {
        return AudioStatus::InvalidArg;
    }

    voiceState.paused = false;
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::Seek(AudioVoiceId voice, dng::u32 frameIndex) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) || voice.slot >= kMaxVoices)
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    if (!voiceState.active || voiceState.generation != voice.generation)
    {
        return AudioStatus::InvalidArg;
    }

    if (!IsValid(voiceState.clip) || voiceState.clip.value > kMaxClips)
    {
        return AudioStatus::InvalidArg;
    }

    const ClipState& clip = m_Clips[voiceState.clip.value - 1u];
    if (!clip.valid || clip.channelCount == 0u)
    {
        return AudioStatus::InvalidArg;
    }

    dng::u32 clipFrameCount = 0;
    if (clip.storage == ClipStorageKind::Memory)
    {
        clipFrameCount = clip.sampleCount / static_cast<dng::u32>(clip.channelCount);
    }
    else if (clip.storage == ClipStorageKind::Stream)
    {
        if (clip.streamSlot >= kMaxStreamClips || !s_StreamClips[clip.streamSlot].valid)
        {
            return AudioStatus::InvalidArg;
        }
        clipFrameCount = s_StreamClips[clip.streamSlot].frameCount;
    }
    else
    {
        return AudioStatus::InvalidArg;
    }

    if (clipFrameCount == 0u || frameIndex >= clipFrameCount)
    {
        return AudioStatus::InvalidArg;
    }

    voiceState.frameCursor = static_cast<double>(frameIndex);
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::SetGain(AudioVoiceId voice, float gain) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) || voice.slot >= kMaxVoices || !(gain >= 0.0f))
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    if (!voiceState.active || voiceState.generation != voice.generation)
    {
        return AudioStatus::InvalidArg;
    }

    voiceState.targetGain = gain;
    voiceState.stopAfterGainRamp = false;
    voiceState.gainRampFramesRemaining = kGainRampFrames;
    voiceState.gainStepPerFrame =
        (voiceState.targetGain - voiceState.currentGain) / static_cast<float>(kGainRampFrames);
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::SetBusGain(AudioBus bus, float gain) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(bus) || !(gain >= 0.0f))
    {
        return AudioStatus::InvalidArg;
    }

    m_BusGains[ToBusIndex(bus)] = gain;
    return AudioStatus::Ok;
}

void WinMmAudio::MixVoicesToBuffer(float* outSamples,
                                   dng::u16 outChannelCount,
                                   dng::u32 requestedFrames) noexcept
{
    if (outSamples == nullptr || outChannelCount == 0 || requestedFrames == 0)
    {
        return;
    }

    auto sampleStreamFrame = [this](StreamClipState& streamClip,
                                    dng::u32 frameIndex,
                                    float& outLeft,
                                    float& outRight) noexcept -> bool
    {
        if (!EnsureStreamCache(streamClip, frameIndex))
        {
            return false;
        }

        const dng::u32 localFrame = frameIndex - streamClip.cacheStartFrame;
        if (localFrame >= streamClip.cacheFrameCount)
        {
            return false;
        }

        const dng::u32 base = localFrame * static_cast<dng::u32>(streamClip.channelCount);
        outLeft = Pcm16ToFloat(streamClip.cacheSamples[base]);
        outRight = (streamClip.channelCount > 1u)
            ? Pcm16ToFloat(streamClip.cacheSamples[base + 1u])
            : outLeft;
        return true;
    };

    const double outputSampleRate = static_cast<double>(m_SampleRate);

    for (dng::u32 voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
    {
        VoiceState& voice = m_Voices[voiceIndex];
        if (!voice.active || voice.paused || !IsValid(voice.clip) || voice.clip.value > kMaxClips)
        {
            continue;
        }

        const ClipState& clip = m_Clips[voice.clip.value - 1u];
        if (!clip.valid || clip.channelCount == 0 || clip.sampleRate == 0)
        {
            ResetVoiceForInvalidClip(voice);
            continue;
        }

        StreamClipState* streamState = nullptr;
        dng::u32 clipFrameCount = 0;
        if (clip.storage == ClipStorageKind::Memory)
        {
            clipFrameCount = clip.sampleCount / static_cast<dng::u32>(clip.channelCount);
        }
        else if (clip.storage == ClipStorageKind::Stream)
        {
            if (clip.streamSlot >= kMaxStreamClips)
            {
                ResetVoiceForInvalidClip(voice);
                continue;
            }

            streamState = &s_StreamClips[clip.streamSlot];
            if (!streamState->valid ||
                streamState->channelCount != clip.channelCount ||
                streamState->sampleRate != clip.sampleRate)
            {
                ResetVoiceForInvalidClip(voice);
                continue;
            }

            clipFrameCount = streamState->frameCount;
        }
        else
        {
            ResetVoiceForInvalidClip(voice);
            continue;
        }

        if (clipFrameCount == 0)
        {
            ResetVoiceForInvalidClip(voice);
            continue;
        }

        const double sourceStep = static_cast<double>(voice.pitch) *
            (static_cast<double>(clip.sampleRate) / outputSampleRate);
        if (!(sourceStep > 0.0))
        {
            ResetVoiceForInvalidClip(voice);
            continue;
        }

        const double clipFrameCountD = static_cast<double>(clipFrameCount);
        for (dng::u32 frame = 0; frame < requestedFrames; ++frame)
        {
            while (voice.frameCursor >= clipFrameCountD)
            {
                if (voice.loop)
                {
                    voice.frameCursor -= clipFrameCountD;
                }
                else
                {
                    ResetVoiceForInvalidClip(voice);
                    break;
                }
            }
            if (!voice.active)
            {
                break;
            }

            dng::u32 srcFrameA = static_cast<dng::u32>(voice.frameCursor);
            if (srcFrameA >= clipFrameCount)
            {
                srcFrameA = clipFrameCount - 1u;
            }
            const double fracD = voice.frameCursor - static_cast<double>(srcFrameA);
            const float frac = static_cast<float>(fracD);

            dng::u32 srcFrameB = srcFrameA + 1u;
            if (srcFrameB >= clipFrameCount)
            {
                srcFrameB = voice.loop ? 0u : srcFrameA;
            }

            float srcLeftA = 0.0f;
            float srcRightA = 0.0f;
            float srcLeftB = 0.0f;
            float srcRightB = 0.0f;
            bool sampleOk = true;

            if (clip.storage == ClipStorageKind::Memory)
            {
                const dng::u32 srcBaseA = clip.sampleOffset + srcFrameA * static_cast<dng::u32>(clip.channelCount);
                const dng::u32 srcBaseB = clip.sampleOffset + srcFrameB * static_cast<dng::u32>(clip.channelCount);

                srcLeftA = Pcm16ToFloat(s_ClipSamplePool[srcBaseA]);
                srcRightA = (clip.channelCount > 1u)
                    ? Pcm16ToFloat(s_ClipSamplePool[srcBaseA + 1u])
                    : srcLeftA;

                srcLeftB = Pcm16ToFloat(s_ClipSamplePool[srcBaseB]);
                srcRightB = (clip.channelCount > 1u)
                    ? Pcm16ToFloat(s_ClipSamplePool[srcBaseB + 1u])
                    : srcLeftB;
            }
            else
            {
                sampleOk = sampleStreamFrame(*streamState, srcFrameA, srcLeftA, srcRightA) &&
                           sampleStreamFrame(*streamState, srcFrameB, srcLeftB, srcRightB);
            }

            if (!sampleOk)
            {
                ResetVoiceForInvalidClip(voice);
                break;
            }

            const float srcLeft = Lerp(srcLeftA, srcLeftB, frac);
            const float srcRight = Lerp(srcRightA, srcRightB, frac);
            const float masterGain = m_BusGains[ToBusIndex(AudioBus::Master)];
            float scopedBusGain = 1.0f;
            if (voice.bus != AudioBus::Master)
            {
                scopedBusGain = m_BusGains[ToBusIndex(voice.bus)];
            }
            const float gain = voice.currentGain * scopedBusGain * masterGain;

            if (outChannelCount == 1u)
            {
                const float mono = (srcLeft + srcRight) * 0.5f * gain;
                outSamples[frame] = ClampUnit(outSamples[frame] + mono);
            }
            else
            {
                const dng::u32 outBase = frame * static_cast<dng::u32>(outChannelCount);
                outSamples[outBase] = ClampUnit(outSamples[outBase] + (srcLeft * gain));
                outSamples[outBase + 1u] = ClampUnit(outSamples[outBase + 1u] + (srcRight * gain));
            }

            if (voice.gainRampFramesRemaining > 0)
            {
                voice.currentGain += voice.gainStepPerFrame;
                --voice.gainRampFramesRemaining;
                if (voice.gainRampFramesRemaining == 0)
                {
                    voice.currentGain = voice.targetGain;
                    voice.gainStepPerFrame = 0.0f;
                }
            }

            if (voice.stopAfterGainRamp &&
                voice.gainRampFramesRemaining == 0 &&
                voice.currentGain <= 0.0f)
            {
                ResetVoiceForInvalidClip(voice);
                break;
            }

            voice.frameCursor += sourceStep;
        }
    }
}

AudioStatus WinMmAudio::Mix(AudioMixParams& params) noexcept
{
    params.writtenSamples = 0;

    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (params.outSamples == nullptr ||
        params.sampleRate != m_SampleRate ||
        params.channelCount != m_ChannelCount ||
        params.requestedFrames == 0)
    {
        if (params.requestedFrames == 0 &&
            params.sampleRate == m_SampleRate &&
            params.channelCount == m_ChannelCount)
        {
            return AudioStatus::Ok;
        }
        return AudioStatus::InvalidArg;
    }

    const dng::u64 requestedSamples64 =
        static_cast<dng::u64>(params.requestedFrames) * static_cast<dng::u64>(params.channelCount);
    if (requestedSamples64 > static_cast<dng::u64>(params.outputCapacitySamples) ||
        requestedSamples64 > static_cast<dng::u64>(MaxU32()) ||
        params.requestedFrames > m_FramesPerBuffer)
    {
        return AudioStatus::InvalidArg;
    }

    const dng::u32 requestedSamples = static_cast<dng::u32>(requestedSamples64);

    dng::i32 deviceBufferIndex = -1;
#if DNG_PLATFORM_WINDOWS
    for (dng::u32 attempt = 0; attempt < kBufferCount; ++attempt)
    {
        const dng::u32 index = (m_NextBufferIndex + attempt) % kBufferCount;
        WAVEHDR* header = reinterpret_cast<WAVEHDR*>(m_WaveHeaders[index]);
        if (!m_InFlight[index] || ((header->dwFlags & WHDR_DONE) != 0u))
        {
            deviceBufferIndex = static_cast<dng::i32>(index);
            m_InFlight[index] = false;
            m_NextBufferIndex = (index + 1u) % kBufferCount;
            break;
        }
    }
#endif

    dng::i16* deviceSamples = nullptr;
    if (deviceBufferIndex >= 0)
    {
        deviceSamples = m_PcmBuffers[static_cast<dng::u32>(deviceBufferIndex)];
    }
    else
    {
        ++m_UnderrunCount;
    }

    for (dng::u32 i = 0; i < requestedSamples; ++i)
    {
        params.outSamples[i] = 0.0f;
    }

    MixVoicesToBuffer(params.outSamples, params.channelCount, params.requestedFrames);

    if (deviceSamples != nullptr)
    {
        for (dng::u32 i = 0; i < requestedSamples; ++i)
        {
            deviceSamples[i] = FloatToPcm16(params.outSamples[i]);
        }
    }

    params.writtenSamples = requestedSamples;

#if DNG_PLATFORM_WINDOWS
    if (deviceBufferIndex >= 0)
    {
        WAVEHDR* header = reinterpret_cast<WAVEHDR*>(m_WaveHeaders[static_cast<dng::u32>(deviceBufferIndex)]);
        header->dwBufferLength = static_cast<DWORD>(requestedSamples * static_cast<dng::u32>(sizeof(dng::i16)));
        header->dwFlags &= ~WHDR_DONE;

        const MMRESULT writeResult =
            ::waveOutWrite(static_cast<HWAVEOUT>(m_Device), header, static_cast<UINT>(sizeof(WAVEHDR)));
        if (writeResult != MMSYSERR_NOERROR)
        {
            ++m_SubmitErrorCount;
            m_InFlight[static_cast<dng::u32>(deviceBufferIndex)] = false;
            return AudioStatus::Ok;
        }

        m_InFlight[static_cast<dng::u32>(deviceBufferIndex)] = true;
    }
#endif

    return AudioStatus::Ok;
}

} // namespace dng::audio
