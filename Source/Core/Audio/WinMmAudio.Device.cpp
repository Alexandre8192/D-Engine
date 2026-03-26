// ============================================================================
// D-Engine - Source/Core/Audio/WinMmAudio.Device.cpp
// ----------------------------------------------------------------------------
// Purpose : WinMM device lifecycle for the audio backend.
// Contract: No exceptions/RTTI. Initialization is explicit and idempotent.
// ============================================================================

#include "Core/Audio/WinMmAudioInternal.hpp"
#include "Core/Platform/PlatformCrt.hpp"

namespace dng::audio
{

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

    const size_t clipSamplePoolBytes = static_cast<size_t>(kMaxClipSamplePool) * sizeof(dng::i16);
    const size_t streamClipsBytes = static_cast<size_t>(kMaxStreamClips) * sizeof(StreamClipState);
    dng::i16* clipSamplePool = static_cast<dng::i16*>(
        dng::platform::AllocAligned(clipSamplePoolBytes, alignof(dng::i16)));
    StreamClipState* streamClips = static_cast<StreamClipState*>(
        dng::platform::AllocAligned(streamClipsBytes, alignof(StreamClipState)));
    if (clipSamplePool == nullptr || streamClips == nullptr)
    {
        dng::platform::FreeAligned(clipSamplePool);
        dng::platform::FreeAligned(streamClips);
        return false;
    }
    std::memset(clipSamplePool, 0, clipSamplePoolBytes);
    std::memset(streamClips, 0, streamClipsBytes);

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(config.channelCount);
    format.nSamplesPerSec = config.sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>((format.wBitsPerSample / 8u) * format.nChannels);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HWAVEOUT device = nullptr;
    const MMRESULT openResult = ::waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (openResult != MMSYSERR_NOERROR)
    {
        dng::platform::FreeAligned(clipSamplePool);
        dng::platform::FreeAligned(streamClips);
        return false;
    }

    m_Device = device;
    m_SampleRate = config.sampleRate;
    m_ChannelCount = config.channelCount;
    m_FramesPerBuffer = config.framesPerBuffer;
    m_ClipSamplePool = clipSamplePool;
    m_StreamClips = streamClips;
    m_IsInitialized = true;
    m_NextBufferIndex = 0;
    m_NextClipValue = 1;
    m_NextClipSample = 0;
    m_LoadedClipCount = 0;
    m_LoadedStreamClipCount = 0;
    m_StreamFileSystem = fs::FileSystemInterface{};
    m_HasStreamFileSystem = false;
    m_UnderrunCount = 0;
    m_SubmitErrorCount = 0;
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
    dng::platform::FreeAligned(m_ClipSamplePool);
    dng::platform::FreeAligned(m_StreamClips);
    m_ClipSamplePool = nullptr;
    m_StreamClips = nullptr;
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

} // namespace dng::audio
