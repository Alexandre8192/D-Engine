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

#include <cstdio>
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

    [[nodiscard]] inline bool SkipFileBytes(std::FILE* file, dng::u32 byteCount) noexcept
    {
        if (file == nullptr || byteCount == 0)
        {
            return true;
        }

        return std::fseek(file, static_cast<long>(byteCount), SEEK_CUR) == 0;
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

bool WinMmAudio::HasClip(AudioClipId clip) const noexcept
{
    if (!IsValid(clip) || clip.value > kMaxClips)
    {
        return false;
    }

    const dng::u32 index = clip.value - 1u;
    return m_Clips[index].valid;
}

AudioStatus WinMmAudio::LoadWavPcm16Clip(const char* path, AudioClipId& outClip) noexcept
{
    outClip = AudioClipId{};
    if (!m_IsInitialized || path == nullptr)
    {
        return AudioStatus::InvalidArg;
    }

    std::FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0)
    {
        file = nullptr;
    }
#else
    file = std::fopen(path, "rb");
#endif
    if (file == nullptr)
    {
        return AudioStatus::NotSupported;
    }

    AudioStatus status = AudioStatus::UnknownError;
    do
    {
        dng::u8 riffHeader[12]{};
        if (std::fread(riffHeader, 1, sizeof(riffHeader), file) != sizeof(riffHeader))
        {
            status = AudioStatus::InvalidArg;
            break;
        }

        if (std::memcmp(riffHeader, "RIFF", 4) != 0 ||
            std::memcmp(&riffHeader[8], "WAVE", 4) != 0)
        {
            status = AudioStatus::InvalidArg;
            break;
        }

        bool fmtFound = false;
        dng::u16 fmtTag = 0;
        dng::u16 fmtChannels = 0;
        dng::u32 fmtSampleRate = 0;
        dng::u16 fmtBitsPerSample = 0;

        bool done = false;
        while (!done)
        {
            dng::u8 chunkHeader[8]{};
            const size_t headerRead = std::fread(chunkHeader, 1, sizeof(chunkHeader), file);
            if (headerRead != sizeof(chunkHeader))
            {
                status = AudioStatus::InvalidArg;
                break;
            }

            const dng::u32 chunkSize = ReadLe32(&chunkHeader[4]);
            const bool hasPadByte = (chunkSize & 1u) != 0u;

            if (std::memcmp(chunkHeader, "fmt ", 4) == 0)
            {
                if (chunkSize < 16u)
                {
                    status = AudioStatus::InvalidArg;
                    break;
                }

                dng::u8 fmtData[40]{};
                const dng::u32 readSize = (chunkSize < static_cast<dng::u32>(sizeof(fmtData)))
                    ? chunkSize
                    : static_cast<dng::u32>(sizeof(fmtData));

                if (std::fread(fmtData, 1, readSize, file) != readSize)
                {
                    status = AudioStatus::InvalidArg;
                    break;
                }

                if (chunkSize > readSize && !SkipFileBytes(file, chunkSize - readSize))
                {
                    status = AudioStatus::InvalidArg;
                    break;
                }

                if (hasPadByte && !SkipFileBytes(file, 1u))
                {
                    status = AudioStatus::InvalidArg;
                    break;
                }

                fmtTag = ReadLe16(&fmtData[0]);
                fmtChannels = ReadLe16(&fmtData[2]);
                fmtSampleRate = ReadLe32(&fmtData[4]);
                fmtBitsPerSample = ReadLe16(&fmtData[14]);
                fmtFound = true;
                continue;
            }

            if (std::memcmp(chunkHeader, "data", 4) == 0)
            {
                if (!fmtFound ||
                    fmtTag != 1u ||
                    fmtBitsPerSample != 16u ||
                    (fmtChannels != 1u && fmtChannels != 2u) ||
                    fmtSampleRate == 0u ||
                    (chunkSize % 2u) != 0u)
                {
                    status = AudioStatus::InvalidArg;
                    break;
                }

                const dng::u32 sampleCount = chunkSize / 2u;
                if (sampleCount == 0u ||
                    (sampleCount % static_cast<dng::u32>(fmtChannels)) != 0u)
                {
                    status = AudioStatus::InvalidArg;
                    break;
                }

                AudioClipId clip = AllocateClipId();
                if (!IsValid(clip))
                {
                    status = AudioStatus::NotSupported;
                    break;
                }

                const dng::u32 clipIndex = clip.value - 1u;
                if (sampleCount > (kMaxClipSamplePool - m_NextClipSample))
                {
                    status = AudioStatus::NotSupported;
                    break;
                }

                const dng::u32 sampleOffset = m_NextClipSample;
                if (std::fread(&s_ClipSamplePool[sampleOffset], 1, chunkSize, file) != chunkSize)
                {
                    status = AudioStatus::InvalidArg;
                    break;
                }

                if (hasPadByte && !SkipFileBytes(file, 1u))
                {
                    status = AudioStatus::InvalidArg;
                    break;
                }

                ClipState& clipState = m_Clips[clipIndex];
                clipState.valid = true;
                clipState.channelCount = fmtChannels;
                clipState.sampleRate = fmtSampleRate;
                clipState.sampleOffset = sampleOffset;
                clipState.sampleCount = sampleCount;
                m_NextClipSample += sampleCount;
                ++m_LoadedClipCount;

                outClip = clip;
                status = AudioStatus::Ok;
                done = true;
                continue;
            }

            if (!SkipFileBytes(file, chunkSize) || (hasPadByte && !SkipFileBytes(file, 1u)))
            {
                status = AudioStatus::InvalidArg;
                break;
            }
        }
    } while (false);

    std::fclose(file);
    return status;
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

        voice.active = false;
        voice.loop = false;
        voice.clip = AudioClipId{};
        voice.frameCursor = 0.0;
        voice.currentGain = 0.0f;
        voice.targetGain = 1.0f;
        voice.gainStepPerFrame = 0.0f;
        voice.gainRampFramesRemaining = 0;
        voice.stopAfterGainRamp = false;
        voice.pitch = 1.0f;
        ++voice.generation;
        if (voice.generation == 0)
        {
            voice.generation = 1;
        }
    }

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
        if (!clipState.valid || clipState.sampleOffset <= removeOffset)
        {
            continue;
        }
        clipState.sampleOffset -= removeSamples;
    }

    m_Clips[clipIndex] = ClipState{};
    m_NextClipSample -= removeSamples;
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

    m_Device = device;
    m_SampleRate = config.sampleRate;
    m_ChannelCount = config.channelCount;
    m_FramesPerBuffer = config.framesPerBuffer;
    m_IsInitialized = true;
    m_NextBufferIndex = 0;
    m_NextClipValue = 1;
    m_NextClipSample = 0;
    m_LoadedClipCount = 0;

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
    m_FramesPerBuffer = 1024;
    m_SampleRate = 48000;
    m_ChannelCount = 2;
    m_IsInitialized = false;
    m_UnderrunCount = 0;
    m_SubmitErrorCount = 0;
    std::memset(m_WaveHeaders, 0, sizeof(m_WaveHeaders));
    std::memset(m_PcmBuffers, 0, sizeof(m_PcmBuffers));

    if (m_OwnsGlobalClipPool)
    {
        std::memset(s_ClipSamplePool, 0, sizeof(s_ClipSamplePool));
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
    voiceState.loop = params.loop;
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
        return AudioStatus::InvalidArg;
    }

    voiceState.stopAfterGainRamp = true;
    voiceState.targetGain = 0.0f;
    if (voiceState.currentGain <= 0.0f)
    {
        voiceState.active = false;
        voiceState.loop = false;
        voiceState.clip = AudioClipId{};
        voiceState.frameCursor = 0.0;
        voiceState.currentGain = 0.0f;
        voiceState.gainStepPerFrame = 0.0f;
        voiceState.gainRampFramesRemaining = 0;
        voiceState.pitch = 1.0f;
        voiceState.stopAfterGainRamp = false;
        return AudioStatus::Ok;
    }

    voiceState.gainRampFramesRemaining = kGainRampFrames;
    voiceState.gainStepPerFrame = -voiceState.currentGain / static_cast<float>(kGainRampFrames);
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

void WinMmAudio::MixVoicesToBuffer(float* outSamples,
                                   dng::u16 outChannelCount,
                                   dng::u32 requestedFrames) noexcept
{
    if (outSamples == nullptr || outChannelCount == 0 || requestedFrames == 0)
    {
        return;
    }

    auto resetVoice = [](VoiceState& voice) noexcept
    {
        voice.active = false;
        voice.loop = false;
        voice.stopAfterGainRamp = false;
        voice.clip = AudioClipId{};
        voice.frameCursor = 0.0;
        voice.currentGain = 0.0f;
        voice.targetGain = 1.0f;
        voice.gainStepPerFrame = 0.0f;
        voice.gainRampFramesRemaining = 0;
        voice.pitch = 1.0f;
    };

    const double outputSampleRate = static_cast<double>(m_SampleRate);

    for (dng::u32 voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
    {
        VoiceState& voice = m_Voices[voiceIndex];
        if (!voice.active || !IsValid(voice.clip) || voice.clip.value > kMaxClips)
        {
            continue;
        }

        const ClipState& clip = m_Clips[voice.clip.value - 1u];
        if (!clip.valid || clip.channelCount == 0 || clip.sampleRate == 0)
        {
            resetVoice(voice);
            continue;
        }

        const dng::u32 clipFrameCount = clip.sampleCount / static_cast<dng::u32>(clip.channelCount);
        if (clipFrameCount == 0)
        {
            resetVoice(voice);
            continue;
        }

        const double sourceStep = static_cast<double>(voice.pitch) *
            (static_cast<double>(clip.sampleRate) / outputSampleRate);
        if (!(sourceStep > 0.0))
        {
            resetVoice(voice);
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
                    resetVoice(voice);
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

            const dng::u32 srcBaseA = clip.sampleOffset + srcFrameA * static_cast<dng::u32>(clip.channelCount);
            const dng::u32 srcBaseB = clip.sampleOffset + srcFrameB * static_cast<dng::u32>(clip.channelCount);

            const float srcLeftA = Pcm16ToFloat(s_ClipSamplePool[srcBaseA]);
            const float srcRightA = (clip.channelCount > 1u)
                ? Pcm16ToFloat(s_ClipSamplePool[srcBaseA + 1u])
                : srcLeftA;

            const float srcLeftB = Pcm16ToFloat(s_ClipSamplePool[srcBaseB]);
            const float srcRightB = (clip.channelCount > 1u)
                ? Pcm16ToFloat(s_ClipSamplePool[srcBaseB + 1u])
                : srcLeftB;

            const float srcLeft = Lerp(srcLeftA, srcLeftB, frac);
            const float srcRight = Lerp(srcRightA, srcRightB, frac);
            const float gain = voice.currentGain;

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
                resetVoice(voice);
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
