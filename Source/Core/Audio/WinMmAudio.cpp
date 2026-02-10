// ============================================================================
// D-Engine - Source/Core/Audio/WinMmAudio.cpp
// ----------------------------------------------------------------------------
// Purpose : WinMM waveOut backend implementation for Audio contract M1 bring-up.
// Contract: No exceptions/RTTI. Uses fixed-size internal buffers only. Device
//           lifecycle is explicit via Init/Shutdown and is idempotent.
// Notes   : This implementation prioritizes predictable behavior and simple
//           fallback semantics over advanced latency tuning or resampling.
// ============================================================================

#include "Core/Audio/WinMmAudio.hpp"

#include "Core/Platform/PlatformDefines.hpp"

#include <cmath>
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
namespace
{
    [[nodiscard]] constexpr dng::u32 MaxU32() noexcept
    {
        return ~dng::u32{0};
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
} // namespace

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

    m_Device = device;
    m_SampleRate = config.sampleRate;
    m_ChannelCount = config.channelCount;
    m_FramesPerBuffer = config.framesPerBuffer;
    m_EnableTone = config.enableTone;
    m_IsInitialized = true;
    m_Phase = 0.0;
    m_NextBufferIndex = 0;

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
            m_HeaderPrepared[i] = false;
            m_InFlight[i] = false;
        }

        (void)::waveOutClose(device);
    }
#endif

    m_Device = nullptr;
    m_NextBufferIndex = 0;
    m_FramesPerBuffer = 1024;
    m_SampleRate = 48000;
    m_ChannelCount = 2;
    m_EnableTone = true;
    m_IsInitialized = false;
    m_Phase = 0.0;
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
        if (params.requestedFrames == 0 && params.sampleRate == m_SampleRate && params.channelCount == m_ChannelCount)
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

    constexpr double kTwoPi = 6.28318530717958647692;
    const double phaseStep = kTwoPi * 440.0 / static_cast<double>(m_SampleRate);
    constexpr float kAmplitude = 0.08f;

    for (dng::u32 i = 0; i < requestedSamples; ++i)
    {
        float sample = 0.0f;
        if (m_EnableTone)
        {
            sample = static_cast<float>(std::sin(m_Phase)) * kAmplitude;
            m_Phase += phaseStep;
            if (m_Phase >= kTwoPi)
            {
                m_Phase -= kTwoPi;
            }
        }

        params.outSamples[i] = sample;
        if (deviceSamples != nullptr)
        {
            deviceSamples[i] = FloatToPcm16(sample);
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
            // Keep CPU mix output valid even if device enqueue fails transiently.
            m_InFlight[static_cast<dng::u32>(deviceBufferIndex)] = false;
            return AudioStatus::Ok;
        }

        m_InFlight[static_cast<dng::u32>(deviceBufferIndex)] = true;
    }
#endif

    return AudioStatus::Ok;
}

} // namespace dng::audio
