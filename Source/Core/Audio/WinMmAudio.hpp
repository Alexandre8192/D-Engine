// ============================================================================
// D-Engine - Source/Core/Audio/WinMmAudio.hpp
// ----------------------------------------------------------------------------
// Purpose : Minimal Windows audio backend using WinMM waveOut for real device
//           output while preserving the engine Audio contract shape.
// Contract: No exceptions/RTTI. Caller supplies output buffers through
//           AudioMixParams. Backend owns device handle and fixed-size queue
//           buffers. External synchronization is required.
// Notes   : M2 backend supports loading PCM16 WAV clips and software mixing
//           of voices before submitting to the default output device.
//           Clip samples use a shared static pool, so only one initialized
//           WinMmAudio instance can own the clip pool at a time.
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
        [[nodiscard]] AudioStatus Play(AudioVoiceId voice, const AudioPlayParams& params) noexcept;
        [[nodiscard]] AudioStatus Stop(AudioVoiceId voice) noexcept;
        [[nodiscard]] AudioStatus SetGain(AudioVoiceId voice, float gain) noexcept;
        [[nodiscard]] AudioStatus Mix(AudioMixParams& params) noexcept;
        [[nodiscard]] AudioStatus LoadWavPcm16Clip(const char* path, AudioClipId& outClip) noexcept;
        [[nodiscard]] AudioStatus UnloadClip(AudioClipId clip) noexcept;
        [[nodiscard]] bool HasClip(AudioClipId clip) const noexcept;
        [[nodiscard]] constexpr dng::u32 GetLoadedClipCount() const noexcept
        {
            return m_LoadedClipCount;
        }
        [[nodiscard]] constexpr dng::u32 GetClipPoolUsageSamples() const noexcept
        {
            return m_NextClipSample;
        }
        [[nodiscard]] static constexpr dng::u32 GetClipPoolCapacitySamples() noexcept
        {
            return kMaxClipSamplePool;
        }
        [[nodiscard]] constexpr dng::u64 GetUnderrunCount() const noexcept
        {
            return m_UnderrunCount;
        }
        [[nodiscard]] constexpr dng::u64 GetSubmitErrorCount() const noexcept
        {
            return m_SubmitErrorCount;
        }

    private:
        static constexpr dng::u32 kBufferCount = 3;
        static constexpr dng::u32 kMaxVoices = 64;
        static constexpr dng::u32 kMaxClips = 64;
        static constexpr dng::u32 kMaxClipSamplePool = 65536;
        static constexpr dng::u32 kMaxChannels = 2;
        static constexpr dng::u32 kMaxFramesPerBuffer = 4096;
        static constexpr dng::u32 kMaxSamplesPerBuffer = kMaxFramesPerBuffer * kMaxChannels;
        static constexpr dng::u32 kWaveHeaderStorageBytes = 128;
        static constexpr dng::u32 kWaveHeaderStorageAlign = 16;

        struct ClipState
        {
            bool     valid = false;
            dng::u16 channelCount = 0;
            dng::u16 reserved = 0;
            dng::u32 sampleRate = 0;
            dng::u32 sampleOffset = 0;
            dng::u32 sampleCount = 0;
        };

        struct VoiceState
        {
            AudioClipId clip{};
            double      frameCursor = 0.0;
            float       gain = 1.0f;
            float       pitch = 1.0f;
            dng::u32    generation = 1;
            bool        active = false;
            bool        loop = false;
            dng::u16    reserved = 0;
        };

        void MixVoicesToBuffer(float* outSamples,
                               dng::u16 outChannelCount,
                               dng::u32 requestedFrames) noexcept;

        [[nodiscard]] AudioClipId AllocateClipId() noexcept;

        void* m_Device = nullptr;
        alignas(kWaveHeaderStorageAlign) dng::u8 m_WaveHeaders[kBufferCount][kWaveHeaderStorageBytes]{};
        dng::i16 m_PcmBuffers[kBufferCount][kMaxSamplesPerBuffer]{};
        ClipState m_Clips[kMaxClips]{};
        VoiceState m_Voices[kMaxVoices]{};
        bool     m_HeaderPrepared[kBufferCount]{};
        bool     m_InFlight[kBufferCount]{};
        dng::u32 m_NextBufferIndex = 0;
        dng::u32 m_NextClipValue = 1;
        dng::u32 m_NextClipSample = 0;
        dng::u32 m_LoadedClipCount = 0;
        dng::u32 m_FramesPerBuffer = 1024;
        dng::u32 m_SampleRate = 48000;
        dng::u16 m_ChannelCount = 2;
        dng::u16 m_Reserved = 0;
        bool     m_IsInitialized = false;
        bool     m_OwnsGlobalClipPool = false;
        dng::u8  m_Padding[6]{};
        dng::u64 m_UnderrunCount = 0;
        dng::u64 m_SubmitErrorCount = 0;

        static dng::i16 s_ClipSamplePool[kMaxClipSamplePool];
        static bool     s_GlobalClipPoolInUse;
    };

    static_assert(AudioBackend<WinMmAudio>, "WinMmAudio must satisfy audio backend concept.");

    [[nodiscard]] inline AudioInterface MakeWinMmAudioInterface(WinMmAudio& backend) noexcept
    {
        return MakeAudioInterface(backend);
    }

} // namespace dng::audio
