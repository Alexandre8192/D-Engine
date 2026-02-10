// ============================================================================
// D-Engine - Source/Core/Audio/WinMmAudio.hpp
// ----------------------------------------------------------------------------
// Purpose : Minimal Windows audio backend using WinMM waveOut for real device
//           output while preserving the engine Audio contract shape.
// Contract: No exceptions/RTTI. Caller supplies output buffers through
//           AudioMixParams. Backend owns device handle and fixed-size queue
//           buffers. External synchronization is required.
// Notes   : Supports PCM16 WAV clips loaded either in memory or as streamed
//           sources (chunked reads through FileSystem contract), then software
//           mixes voices before submitting to the default output device.
//           Uses short gain ramps on play/stop/set-gain to reduce clicks and
//           linear resampling when clip sample-rate differs from device rate.
//           Clip samples use a shared static pool, so only one initialized
//           WinMmAudio instance can own the clip pool at a time.
// ============================================================================

#pragma once

#include "Core/Contracts/Audio.hpp"
#include "Core/Contracts/FileSystem.hpp"

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
        [[nodiscard]] AudioStatus LoadWavPcm16Clip(const dng::u8* fileData,
                                                   dng::u32 fileSizeBytes,
                                                   AudioClipId& outClip) noexcept;
        [[nodiscard]] AudioStatus LoadWavPcm16StreamClip(fs::FileSystemInterface& fileSystem,
                                                         fs::PathView path,
                                                         AudioClipId& outClip) noexcept;
        [[nodiscard]] AudioStatus UnloadClip(AudioClipId clip) noexcept;
        [[nodiscard]] bool HasClip(AudioClipId clip) const noexcept;
        [[nodiscard]] static constexpr dng::u32 GetMaxClipCount() noexcept
        {
            return kMaxClips;
        }
        [[nodiscard]] constexpr dng::u32 GetLoadedStreamClipCount() const noexcept
        {
            return m_LoadedStreamClipCount;
        }
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
        static constexpr dng::u32 kMaxStreamClips = 8;
        static constexpr dng::u32 kMaxClipSamplePool = 65536;
        static constexpr dng::u32 kMaxChannels = 2;
        static constexpr dng::u32 kMaxFramesPerBuffer = 4096;
        static constexpr dng::u32 kMaxSamplesPerBuffer = kMaxFramesPerBuffer * kMaxChannels;
        static constexpr dng::u32 kStreamCacheFrames = 2048;
        static constexpr dng::u32 kStreamCacheSamples = kStreamCacheFrames * kMaxChannels;
        static constexpr dng::u32 kMaxStreamPathBytes = 260;
        static constexpr dng::u16 kGainRampFrames = 128;
        static constexpr dng::u16 kInvalidStreamSlot = 0xFFFFu;
        static constexpr dng::u32 kWaveHeaderStorageBytes = 128;
        static constexpr dng::u32 kWaveHeaderStorageAlign = 16;

        enum class ClipStorageKind : dng::u8
        {
            None = 0,
            Memory,
            Stream
        };

        struct ClipState
        {
            bool     valid = false;
            ClipStorageKind storage = ClipStorageKind::None;
            dng::u16 channelCount = 0;
            dng::u16 streamSlot = kInvalidStreamSlot;
            dng::u32 sampleRate = 0;
            dng::u32 sampleOffset = 0;
            dng::u32 sampleCount = 0;
            dng::u32 streamFrameCount = 0;
        };

        struct StreamClipState
        {
            bool                    valid = false;
            dng::u16                channelCount = 0;
            dng::u16                pathSize = 0;
            dng::u32                sampleRate = 0;
            dng::u32                frameCount = 0;
            dng::u64                dataOffsetBytes = 0;
            dng::u32                dataSizeBytes = 0;
            dng::u32                cacheStartFrame = 0;
            dng::u32                cacheFrameCount = 0;
            bool                    cacheValid = false;
            dng::u8                 reserved[3]{};
            fs::FileSystemInterface fileSystem{};
            char                    path[kMaxStreamPathBytes]{};
            dng::i16                cacheSamples[kStreamCacheSamples]{};
        };

        struct VoiceState
        {
            AudioClipId clip{};
            double      frameCursor = 0.0;
            float       currentGain = 0.0f;
            float       targetGain = 1.0f;
            float       gainStepPerFrame = 0.0f;
            float       pitch = 1.0f;
            dng::u32    generation = 1;
            dng::u16    gainRampFramesRemaining = 0;
            bool        stopAfterGainRamp = false;
            bool        active = false;
            bool        loop = false;
            dng::u8     reserved = 0;
        };

        void MixVoicesToBuffer(float* outSamples,
                               dng::u16 outChannelCount,
                               dng::u32 requestedFrames) noexcept;

        [[nodiscard]] AudioClipId AllocateClipId() noexcept;
        [[nodiscard]] dng::u16 AllocateStreamSlot() noexcept;
        [[nodiscard]] bool EnsureStreamCache(StreamClipState& streamState,
                                             dng::u32 sourceFrame) noexcept;
        static void ResetVoiceForInvalidClip(VoiceState& voice) noexcept;

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
        dng::u32 m_LoadedStreamClipCount = 0;
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
        static StreamClipState s_StreamClips[kMaxStreamClips];
        static bool     s_GlobalClipPoolInUse;
    };

    static_assert(AudioBackend<WinMmAudio>, "WinMmAudio must satisfy audio backend concept.");

    [[nodiscard]] inline AudioInterface MakeWinMmAudioInterface(WinMmAudio& backend) noexcept
    {
        return MakeAudioInterface(backend);
    }

} // namespace dng::audio
