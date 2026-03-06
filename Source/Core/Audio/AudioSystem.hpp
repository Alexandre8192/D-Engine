// ============================================================================
// D-Engine - Source/Core/Audio/AudioSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : High-level audio system facade that owns built-in backends and
//           exposes unified frame mixing to the rest of the engine.
// Contract: Self-contained public header, no exceptions/RTTI. Built-in backend
//           ownership lives in opaque fixed-size storage carried by
//           AudioSystemState; platform-specific work is implemented in
//           AudioSystem.cpp. Hot paths remain allocation-free once initialized.
// Notes   : Defaults to NullAudio. Platform backend can be selected via a
//           generic audio-platform config with optional fallback to NullAudio
//           when initialization fails. Voice control is command-queued through
//           a fixed-capacity pool to avoid allocations in Mix(). WAV loading
//           uses thread-local scratch storage and supports in-memory clips and
//           streamed clips via FileSystem. Copying an initialized
//           AudioSystemState is unsupported.
// ============================================================================

#pragma once

#include "Core/Contracts/Audio.hpp"
#include "Core/Contracts/FileSystem.hpp"

#include <type_traits>

namespace dng::audio
{
    inline constexpr dng::u32 kAudioSystemMaxVoices = 64;
    inline constexpr dng::u32 kAudioSystemMaxCommands = 256;
    inline constexpr dng::u32 kAudioSystemBusCount = static_cast<dng::u32>(AudioBus::Count);
    inline constexpr dng::u32 kAudioSystemMaxInlineClipSamples = 65536u;
    inline constexpr dng::u32 kAudioSystemWavLoadScratchBytes =
        (kAudioSystemMaxInlineClipSamples * static_cast<dng::u32>(sizeof(dng::i16))) + 4096u;
    inline constexpr dng::u32 kAudioSystemOwnedBackendStorageBytes = 65536u;
    inline constexpr dng::u32 kAudioSystemOwnedBackendStorageAlign = 16u;

    enum class AudioSystemBackend : dng::u8
    {
        Null,
        Platform,
        External
    };

    struct AudioPlatformConfig
    {
        dng::u32 sampleRate = 48000;
        dng::u16 channelCount = 2;
        dng::u16 reserved = 0;
        dng::u32 framesPerBuffer = 1024;
    };

    struct AudioSystemConfig
    {
        AudioSystemBackend backend = AudioSystemBackend::Null;
        AudioPlatformConfig platform{};
        bool               fallbackToNullOnInitFailure = true;
    };

    enum class AudioCommandType : dng::u8
    {
        Play = 0,
        Stop,
        Pause,
        Resume,
        Seek,
        SetGain,
        SetBusGain
    };

    struct AudioCommand
    {
        AudioCommandType type = AudioCommandType::Play;
        AudioVoiceId     voice{};
        AudioPlayParams  play{};
        float            gain = 1.0f;
        AudioBus         bus = AudioBus::Master;
        dng::u32         seekFrameIndex = 0;
    };

    struct AudioVoiceState
    {
        AudioClipId clip{};
        float       gain = 1.0f;
        AudioBus    bus = AudioBus::Master;
        bool        isActive = false;
        bool        isPaused = false;
        bool        loop = false;
        dng::u8     reserved[2]{};
        dng::u32    generation = 1;
    };

    struct alignas(kAudioSystemOwnedBackendStorageAlign) AudioSystemOwnedBackendStorage
    {
        dng::u8 bytes[kAudioSystemOwnedBackendStorageBytes]{};
    };

    static_assert(std::is_trivially_copyable_v<AudioCommand>);
    static_assert(std::is_trivially_copyable_v<AudioVoiceState>);
    static_assert(std::is_trivially_copyable_v<AudioSystemOwnedBackendStorage>);

    struct AudioSystemState
    {
        AudioInterface                 interface{};
        AudioSystemBackend             backend = AudioSystemBackend::Null;
        AudioSystemBackend             ownedBackend = AudioSystemBackend::External;
        AudioSystemOwnedBackendStorage ownedBackendStorage{};
        AudioVoiceState                voices[kAudioSystemMaxVoices]{};
        AudioCommand                   commandQueue[kAudioSystemMaxCommands]{};
        dng::u32                       commandReadIndex = 0;
        dng::u32                       commandWriteIndex = 0;
        dng::u32                       commandCount = 0;
        dng::u32                       activeVoiceCount = 0;
        fs::FileSystemInterface        streamFileSystem{};
        float                          busGains[kAudioSystemBusCount]{1.0f, 1.0f, 1.0f};
        bool                           hasStreamFileSystem = false;
        bool                           isInitialized = false;
        dng::u8                        reserved[2]{};
    };

    [[nodiscard]] inline bool IsVoiceHandleInRange(AudioVoiceId voice) noexcept
    {
        return voice.slot < kAudioSystemMaxVoices && voice.generation != 0;
    }

    [[nodiscard]] inline bool IsVoiceActive(const AudioSystemState& state, AudioVoiceId voice) noexcept
    {
        if (!IsVoiceHandleInRange(voice))
        {
            return false;
        }

        const AudioVoiceState& voiceState = state.voices[voice.slot];
        return voiceState.isActive && voiceState.generation == voice.generation;
    }

    [[nodiscard]] inline dng::u32 GetActiveVoiceCount(const AudioSystemState& state) noexcept
    {
        return state.activeVoiceCount;
    }

    [[nodiscard]] inline dng::u32 GetPendingCommandCount(const AudioSystemState& state) noexcept
    {
        return state.commandCount;
    }

    [[nodiscard]] inline bool HasBoundStreamFileSystem(const AudioSystemState& state) noexcept
    {
        return state.hasStreamFileSystem;
    }

    [[nodiscard]] inline float GetBusGain(const AudioSystemState& state, AudioBus bus) noexcept
    {
        if (!IsValid(bus))
        {
            return 0.0f;
        }

        return state.busGains[static_cast<dng::u32>(bus)];
    }

    [[nodiscard]] dng::u64 GetUnderrunCount(const AudioSystemState& state) noexcept;
    [[nodiscard]] dng::u64 GetSubmitErrorCount(const AudioSystemState& state) noexcept;
    [[nodiscard]] dng::u32 GetLoadedClipCount(const AudioSystemState& state) noexcept;
    [[nodiscard]] dng::u32 GetLoadedStreamClipCount(const AudioSystemState& state) noexcept;
    [[nodiscard]] dng::u32 GetMaxStreamClipCount(const AudioSystemState& state) noexcept;
    [[nodiscard]] dng::u32 GetClipPoolUsageSamples(const AudioSystemState& state) noexcept;
    [[nodiscard]] dng::u32 GetClipPoolCapacitySamples(const AudioSystemState& state) noexcept;
    [[nodiscard]] dng::u32 GetMaxClipCount(const AudioSystemState& state) noexcept;

    [[nodiscard]] AudioStatus LoadWavPcm16Clip(AudioSystemState& state,
                                               fs::FileSystemInterface& fileSystem,
                                               fs::PathView path,
                                               AudioClipId& outClip) noexcept;
    [[nodiscard]] AudioStatus LoadWavPcm16Clip(AudioSystemState& state,
                                               fs::FileSystemInterface& fileSystem,
                                               const char* path,
                                               AudioClipId& outClip) noexcept;
    [[nodiscard]] AudioStatus BindStreamFileSystem(AudioSystemState& state,
                                                   fs::FileSystemInterface& fileSystem) noexcept;
    [[nodiscard]] AudioStatus UnbindStreamFileSystem(AudioSystemState& state) noexcept;
    [[nodiscard]] AudioStatus LoadWavPcm16StreamClip(AudioSystemState& state,
                                                     fs::FileSystemInterface& fileSystem,
                                                     fs::PathView path,
                                                     AudioClipId& outClip) noexcept;
    [[nodiscard]] AudioStatus LoadWavPcm16StreamClip(AudioSystemState& state,
                                                     fs::FileSystemInterface& fileSystem,
                                                     const char* path,
                                                     AudioClipId& outClip) noexcept;
    [[nodiscard]] bool InitAudioSystemWithInterface(AudioSystemState& state,
                                                    AudioInterface interface,
                                                    AudioSystemBackend backend) noexcept;
    [[nodiscard]] bool InitAudioSystem(AudioSystemState& state,
                                       const AudioSystemConfig& config) noexcept;
    void ShutdownAudioSystem(AudioSystemState& state) noexcept;

    [[nodiscard]] inline AudioCaps QueryCaps(const AudioSystemState& state) noexcept
    {
        return state.isInitialized ? dng::audio::QueryCaps(state.interface) : AudioCaps{};
    }

    [[nodiscard]] AudioStatus UnloadClip(AudioSystemState& state, AudioClipId clip) noexcept;
    [[nodiscard]] AudioStatus Play(AudioSystemState& state,
                                   const AudioPlayParams& params,
                                   AudioVoiceId& outVoice) noexcept;
    [[nodiscard]] AudioStatus Stop(AudioSystemState& state, AudioVoiceId voice) noexcept;
    [[nodiscard]] AudioStatus SetGain(AudioSystemState& state,
                                      AudioVoiceId voice,
                                      float gain) noexcept;
    [[nodiscard]] AudioStatus Pause(AudioSystemState& state, AudioVoiceId voice) noexcept;
    [[nodiscard]] AudioStatus Resume(AudioSystemState& state, AudioVoiceId voice) noexcept;
    [[nodiscard]] AudioStatus Seek(AudioSystemState& state,
                                   AudioVoiceId voice,
                                   dng::u32 frameIndex) noexcept;
    [[nodiscard]] AudioStatus SetBusGain(AudioSystemState& state,
                                         AudioBus bus,
                                         float gain) noexcept;

    [[nodiscard]] inline AudioStatus SetMasterGain(AudioSystemState& state, float gain) noexcept
    {
        return SetBusGain(state, AudioBus::Master, gain);
    }

    [[nodiscard]] AudioStatus Mix(AudioSystemState& state, AudioMixParams& params) noexcept;

} // namespace dng::audio
