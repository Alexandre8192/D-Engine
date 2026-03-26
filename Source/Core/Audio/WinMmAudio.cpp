// ============================================================================
// D-Engine - Source/Core/Audio/WinMmAudio.cpp
// ----------------------------------------------------------------------------
// Purpose : Core clip/stream state management for the WinMM audio backend.
// Contract: No exceptions/RTTI. Device-facing code lives in separate TUs.
// Notes   : Keeps WAV parsing and clip lifetime logic grouped together.
// ============================================================================

#include "Core/Audio/WinMmAudioInternal.hpp"

namespace dng::audio
{

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
    if (m_StreamClips == nullptr)
    {
        return kInvalidStreamSlot;
    }

    for (dng::u16 i = 0; i < kMaxStreamClips; ++i)
    {
        if (!m_StreamClips[i].valid)
        {
            return i;
        }
    }
    return kInvalidStreamSlot;
}

bool WinMmAudio::EnsureStreamCache(StreamClipState& streamState, dng::u32 sourceFrame) noexcept
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

    const dng::u32 loadStartFrame = (sourceFrame / kStreamCacheFrames) * kStreamCacheFrames;
    dng::u32 loadFrameCount = streamState.frameCount - loadStartFrame;
    if (loadFrameCount > kStreamCacheFrames)
    {
        loadFrameCount = kStreamCacheFrames;
    }
    if (loadFrameCount == 0u)
    {
        return false;
    }

    const dng::u64 bytesPerFrame = static_cast<dng::u64>(streamState.channelCount) * sizeof(dng::i16);
    const dng::u64 byteOffset =
        streamState.dataOffsetBytes + (static_cast<dng::u64>(loadStartFrame) * bytesPerFrame);
    const dng::u64 byteCount = static_cast<dng::u64>(loadFrameCount) * bytesPerFrame;

    fs::PathView path{};
    path.data = streamState.path;
    path.size = streamState.pathSize;

    dng::u64 bytesRead = 0;
    const fs::FsStatus readStatus = fs::ReadFileRange(
        m_StreamFileSystem, path, byteOffset, streamState.cacheSamples, byteCount, bytesRead);
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

    return m_Clips[clip.value - 1u].valid;
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

    detail::WavPcm16Info info{};
    const AudioStatus parseStatus = detail::ParseWavPcm16FromMemory(fileData, fileSizeBytes, info);
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
    if (m_ClipSamplePool == nullptr)
    {
        return AudioStatus::UnknownError;
    }

    const dng::u32 clipIndex = clip.value - 1u;
    const dng::u8* pcmData = fileData + static_cast<size_t>(info.dataOffsetBytes);
    std::memcpy(&m_ClipSamplePool[sampleOffset], pcmData, static_cast<size_t>(info.dataSizeBytes));

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
        return AudioStatus::NotSupported;
    }

    m_StreamFileSystem = fs::FileSystemInterface{};
    m_HasStreamFileSystem = false;
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::LoadWavPcm16StreamClip(fs::PathView path, AudioClipId& outClip) noexcept
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

    detail::WavPcm16Info info{};
    const AudioStatus parseStatus = detail::ParseWavPcm16FromFile(m_StreamFileSystem, path, info);
    if (parseStatus != AudioStatus::Ok)
    {
        return parseStatus;
    }

    const dng::u64 sampleCount64 = static_cast<dng::u64>(info.dataSizeBytes / 2u);
    const dng::u64 frameCount64 = sampleCount64 / static_cast<dng::u64>(info.channelCount);
    if (frameCount64 == 0u || frameCount64 > static_cast<dng::u64>(detail::MaxU32()))
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
    if (m_StreamClips == nullptr)
    {
        return AudioStatus::UnknownError;
    }

    StreamClipState& streamState = m_StreamClips[streamSlot];
    streamState = StreamClipState{};
    streamState.valid = true;
    streamState.channelCount = info.channelCount;
    streamState.pathSize = static_cast<dng::u16>(path.size);
    streamState.sampleRate = info.sampleRate;
    streamState.frameCount = static_cast<dng::u32>(frameCount64);
    streamState.dataOffsetBytes = info.dataOffsetBytes;
    streamState.dataSizeBytes = info.dataSizeBytes;
    std::memcpy(streamState.path, path.data, static_cast<size_t>(path.size));
    streamState.path[path.size] = '\0';

    ClipState& clipState = m_Clips[clip.value - 1u];
    clipState.valid = true;
    clipState.storage = ClipStorageKind::Stream;
    clipState.channelCount = info.channelCount;
    clipState.streamSlot = streamSlot;
    clipState.sampleRate = info.sampleRate;
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
            std::memmove(&m_ClipSamplePool[removeOffset],
                         &m_ClipSamplePool[tailOffset],
                         static_cast<size_t>(tailSamples) * sizeof(dng::i16));
        }

        if (removeSamples > 0 && m_NextClipSample >= removeSamples)
        {
            std::memset(&m_ClipSamplePool[m_NextClipSample - removeSamples],
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
            m_StreamClips == nullptr ||
            !m_StreamClips[removedClip.streamSlot].valid)
        {
            return AudioStatus::UnknownError;
        }

        m_StreamClips[removedClip.streamSlot] = StreamClipState{};
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

} // namespace dng::audio
