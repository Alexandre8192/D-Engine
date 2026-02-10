// ============================================================================
// D-Engine - Source/Core/Audio/WinMmAudio.hpp
// ----------------------------------------------------------------------------
// Purpose : Minimal Windows audio backend using WinMM waveOut for real device
//           output while preserving the engine Audio contract shape.
// Contract: No exceptions/RTTI. Caller supplies output buffers through
//           AudioMixParams. Backend owns device handle and fixed-size queue
//           buffers. External synchronization is required.
// Notes   : M1 backend intended for bring-up and validation. It can generate
//           a low-amplitude sine tone and submit it to the default output
//           device. If queue buffers are busy, samples are still written to
//           caller output and device submit is skipped for that mix call.
// ============================================================================

#pragma once

#include "Core/Contracts/Audio.hpp"

#include <cstddef>

namespace dng::audio
{
    struct WinMmAudioConfig
    {
        dng::u32 sampleRate = 48000;
        dng::u16 channelCount = 2;
        dng::u16 reserved = 0;
        dng::u32 framesPerBuffer = 1024;
        bool     enableTone = true;
    };

    struct WinMmAudio
    {
        [[nodiscard]] bool Init(const WinMmAudioConfig& config) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] constexpr bool IsInitialized() const noexcept
        {
            return m_IsInitialized;
        }

        [[nodiscard]] AudioCaps GetCaps() const noexcept;
        [[nodiscard]] AudioStatus Mix(AudioMixParams& params) noexcept;

    private:
        static constexpr dng::u32 kBufferCount = 3;
        static constexpr dng::u32 kMaxChannels = 2;
        static constexpr dng::u32 kMaxFramesPerBuffer = 4096;
        static constexpr dng::u32 kMaxSamplesPerBuffer = kMaxFramesPerBuffer * kMaxChannels;
        static constexpr dng::u32 kWaveHeaderStorageBytes = 128;
        static constexpr dng::u32 kWaveHeaderStorageAlign = 16;

        void* m_Device = nullptr;
        alignas(kWaveHeaderStorageAlign) dng::u8 m_WaveHeaders[kBufferCount][kWaveHeaderStorageBytes]{};
        dng::i16 m_PcmBuffers[kBufferCount][kMaxSamplesPerBuffer]{};
        bool     m_HeaderPrepared[kBufferCount]{};
        bool     m_InFlight[kBufferCount]{};
        dng::u32 m_NextBufferIndex = 0;
        dng::u32 m_FramesPerBuffer = 1024;
        dng::u32 m_SampleRate = 48000;
        dng::u16 m_ChannelCount = 2;
        bool     m_EnableTone = true;
        bool     m_IsInitialized = false;
        double   m_Phase = 0.0;
    };

    static_assert(AudioBackend<WinMmAudio>, "WinMmAudio must satisfy audio backend concept.");

    [[nodiscard]] inline AudioInterface MakeWinMmAudioInterface(WinMmAudio& backend) noexcept
    {
        return MakeAudioInterface(backend);
    }

} // namespace dng::audio

