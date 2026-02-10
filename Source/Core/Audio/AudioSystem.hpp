// ============================================================================
// D-Engine - Source/Core/Audio/AudioSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : High-level audio system that owns a backend instance and exposes
//           unified frame mixing to the rest of the engine.
// Contract: Header-only, no exceptions/RTTI, no allocations in this layer.
//           Lifetime of the backend is tied to AudioSystemState.
//           Thread-safety and determinism follow AudioCaps from the backend;
//           callers must serialize access per instance.
// Notes   : Defaults to NullAudio. Platform backend (WinMM M2) can be
//           selected via config with optional fallback to NullAudio when
//           platform initialization fails. Voice control is command-queued
//           through a fixed-capacity pool to avoid allocations in Mix(). WAV
//           loading supports in-memory clips and streamed clips via FileSystem.
// ============================================================================

#pragma once

#include "Core/Contracts/Audio.hpp"
#include "Core/Contracts/FileSystem.hpp"
#include "Core/Audio/NullAudio.hpp"
#include "Core/Audio/WinMmAudio.hpp"

namespace dng::audio
{
    inline constexpr dng::u32 kAudioSystemMaxVoices = 64;
    inline constexpr dng::u32 kAudioSystemMaxCommands = 256;
    inline constexpr dng::u32 kAudioSystemWavLoadScratchBytes =
        (WinMmAudio::GetClipPoolCapacitySamples() * static_cast<dng::u32>(sizeof(dng::i16))) + 4096u;

    enum class AudioSystemBackend : dng::u8
    {
        Null,
        Platform,
        External
    };

    struct AudioSystemConfig
    {
        AudioSystemBackend backend = AudioSystemBackend::Null;
        WinMmAudioConfig   platform{};
        bool               fallbackToNullOnInitFailure = true;
    };

    enum class AudioCommandType : dng::u8
    {
        Play = 0,
        Stop,
        SetGain
    };

    struct AudioCommand
    {
        AudioCommandType type = AudioCommandType::Play;
        AudioVoiceId     voice{};
        AudioPlayParams  play{};
        float            gain = 1.0f;
    };

    struct AudioVoiceState
    {
        AudioClipId clip{};
        float       gain = 1.0f;
        bool        isActive = false;
        bool        loop = false;
        dng::u16    reserved = 0;
        dng::u32    generation = 1;
    };

    static_assert(std::is_trivially_copyable_v<AudioCommand>);
    static_assert(std::is_trivially_copyable_v<AudioVoiceState>);

    struct AudioSystemState
    {
        AudioInterface     interface{};
        AudioSystemBackend backend = AudioSystemBackend::Null;
        NullAudio          nullBackend{};
        WinMmAudio         platformBackend{};
        AudioVoiceState    voices[kAudioSystemMaxVoices]{};
        AudioCommand       commandQueue[kAudioSystemMaxCommands]{};
        dng::u32           commandReadIndex = 0;
        dng::u32           commandWriteIndex = 0;
        dng::u32           commandCount = 0;
        dng::u32           activeVoiceCount = 0;
        bool               isInitialized = false;
    };

    inline void ShutdownAudioSystem(AudioSystemState& state) noexcept;

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

    [[nodiscard]] inline dng::u64 GetUnderrunCount(const AudioSystemState& state) noexcept
    {
        return (state.backend == AudioSystemBackend::Platform)
            ? state.platformBackend.GetUnderrunCount()
            : 0;
    }

    [[nodiscard]] inline dng::u64 GetSubmitErrorCount(const AudioSystemState& state) noexcept
    {
        return (state.backend == AudioSystemBackend::Platform)
            ? state.platformBackend.GetSubmitErrorCount()
            : 0;
    }

    [[nodiscard]] inline dng::u32 GetLoadedClipCount(const AudioSystemState& state) noexcept
    {
        return (state.backend == AudioSystemBackend::Platform)
            ? state.platformBackend.GetLoadedClipCount()
            : 0u;
    }

    [[nodiscard]] inline dng::u32 GetLoadedStreamClipCount(const AudioSystemState& state) noexcept
    {
        return (state.backend == AudioSystemBackend::Platform)
            ? state.platformBackend.GetLoadedStreamClipCount()
            : 0u;
    }

    [[nodiscard]] inline dng::u32 GetClipPoolUsageSamples(const AudioSystemState& state) noexcept
    {
        return (state.backend == AudioSystemBackend::Platform)
            ? state.platformBackend.GetClipPoolUsageSamples()
            : 0u;
    }

    [[nodiscard]] inline dng::u32 GetClipPoolCapacitySamples(const AudioSystemState& state) noexcept
    {
        return (state.backend == AudioSystemBackend::Platform)
            ? state.platformBackend.GetClipPoolCapacitySamples()
            : 0u;
    }

    [[nodiscard]] inline dng::u32 GetMaxClipCount(const AudioSystemState& state) noexcept
    {
        return (state.backend == AudioSystemBackend::Platform)
            ? state.platformBackend.GetMaxClipCount()
            : 0u;
    }

    namespace detail
    {
        inline dng::u8 g_AudioWavLoadScratch[kAudioSystemWavLoadScratchBytes]{};

        [[nodiscard]] inline AudioStatus MapFsStatus(fs::FsStatus status) noexcept
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

        [[nodiscard]] inline bool EnqueueCommand(AudioSystemState& state, const AudioCommand& command) noexcept
        {
            if (state.commandCount >= kAudioSystemMaxCommands)
            {
                return false;
            }

            state.commandQueue[state.commandWriteIndex] = command;
            state.commandWriteIndex = (state.commandWriteIndex + 1u) % kAudioSystemMaxCommands;
            ++state.commandCount;
            return true;
        }

        [[nodiscard]] inline bool DequeueCommand(AudioSystemState& state, AudioCommand& outCommand) noexcept
        {
            if (state.commandCount == 0)
            {
                return false;
            }

            outCommand = state.commandQueue[state.commandReadIndex];
            state.commandReadIndex = (state.commandReadIndex + 1u) % kAudioSystemMaxCommands;
            --state.commandCount;
            return true;
        }

        [[nodiscard]] inline bool AcquireVoice(AudioSystemState& state,
                                               const AudioPlayParams& params,
                                               AudioVoiceId& outVoice) noexcept
        {
            for (dng::u32 slot = 0; slot < kAudioSystemMaxVoices; ++slot)
            {
                AudioVoiceState& voiceState = state.voices[slot];
                if (voiceState.isActive)
                {
                    continue;
                }

                if (voiceState.generation == 0)
                {
                    voiceState.generation = 1;
                }

                voiceState.clip = params.clip;
                voiceState.gain = params.gain;
                voiceState.isActive = true;
                voiceState.loop = params.loop;

                outVoice.slot = slot;
                outVoice.generation = voiceState.generation;
                ++state.activeVoiceCount;
                return true;
            }

            outVoice = AudioVoiceId{};
            return false;
        }

        inline void ReleaseVoice(AudioSystemState& state, AudioVoiceId voice) noexcept
        {
            if (!IsVoiceHandleInRange(voice))
            {
                return;
            }

            AudioVoiceState& voiceState = state.voices[voice.slot];
            if (!voiceState.isActive || voiceState.generation != voice.generation)
            {
                return;
            }

            voiceState.clip = AudioClipId{};
            voiceState.gain = 1.0f;
            voiceState.isActive = false;
            voiceState.loop = false;
            ++voiceState.generation;
            if (voiceState.generation == 0)
            {
                voiceState.generation = 1;
            }

            if (state.activeVoiceCount > 0)
            {
                --state.activeVoiceCount;
            }
        }

        [[nodiscard]] inline AudioStatus FlushCommands(AudioSystemState& state) noexcept
        {
            AudioStatus firstFailure = AudioStatus::Ok;
            AudioCommand command{};
            while (DequeueCommand(state, command))
            {
                AudioStatus commandStatus = AudioStatus::Ok;
                switch (command.type)
                {
                    case AudioCommandType::Play:
                    {
                        commandStatus = dng::audio::Play(state.interface, command.voice, command.play);
                        break;
                    }
                    case AudioCommandType::Stop:
                    {
                        commandStatus = dng::audio::Stop(state.interface, command.voice);
                        break;
                    }
                    case AudioCommandType::SetGain:
                    {
                        commandStatus = dng::audio::SetGain(state.interface, command.voice, command.gain);
                        break;
                    }
                    default:
                    {
                        commandStatus = AudioStatus::InvalidArg;
                        break;
                    }
                }

                if (commandStatus != AudioStatus::Ok && firstFailure == AudioStatus::Ok)
                {
                    firstFailure = commandStatus;
                }
            }

            return firstFailure;
        }

        [[nodiscard]] inline bool MakePathView(const char* path, fs::PathView& outPath) noexcept
        {
            outPath = fs::PathView{};
            if (path == nullptr)
            {
                return false;
            }

            dng::u32 length = 0;
            while (path[length] != '\0')
            {
                ++length;
            }
            if (length == 0)
            {
                return false;
            }

            outPath.data = path;
            outPath.size = length;
            return true;
        }
    } // namespace detail

    [[nodiscard]] inline AudioStatus LoadWavPcm16Clip(AudioSystemState& state,
                                                      fs::FileSystemInterface& fileSystem,
                                                      fs::PathView path,
                                                      AudioClipId& outClip) noexcept
    {
        outClip = AudioClipId{};
        if (!state.isInitialized || path.data == nullptr || path.size == 0)
        {
            return AudioStatus::InvalidArg;
        }

        if (state.backend != AudioSystemBackend::Platform)
        {
            return AudioStatus::NotSupported;
        }

        dng::u64 fileSize = 0;
        const fs::FsStatus sizeStatus = fs::FileSize(fileSystem, path, fileSize);
        if (sizeStatus != fs::FsStatus::Ok)
        {
            return detail::MapFsStatus(sizeStatus);
        }

        if (fileSize == 0 ||
            fileSize > static_cast<dng::u64>(kAudioSystemWavLoadScratchBytes) ||
            fileSize > static_cast<dng::u64>(~dng::u32{0}))
        {
            return AudioStatus::NotSupported;
        }

        dng::u64 bytesRead = 0;
        const fs::FsStatus readStatus =
            fs::ReadFile(fileSystem, path, detail::g_AudioWavLoadScratch, fileSize, bytesRead);
        if (readStatus != fs::FsStatus::Ok)
        {
            return detail::MapFsStatus(readStatus);
        }

        if (bytesRead != fileSize)
        {
            return AudioStatus::UnknownError;
        }

        return state.platformBackend.LoadWavPcm16Clip(detail::g_AudioWavLoadScratch,
                                                      static_cast<dng::u32>(bytesRead),
                                                      outClip);
    }

    [[nodiscard]] inline AudioStatus LoadWavPcm16Clip(AudioSystemState& state,
                                                      fs::FileSystemInterface& fileSystem,
                                                      const char* path,
                                                      AudioClipId& outClip) noexcept
    {
        outClip = AudioClipId{};
        fs::PathView pathView{};
        if (!detail::MakePathView(path, pathView))
        {
            return AudioStatus::InvalidArg;
        }
        return LoadWavPcm16Clip(state, fileSystem, pathView, outClip);
    }

    [[nodiscard]] inline AudioStatus LoadWavPcm16StreamClip(AudioSystemState& state,
                                                            fs::FileSystemInterface& fileSystem,
                                                            fs::PathView path,
                                                            AudioClipId& outClip) noexcept
    {
        outClip = AudioClipId{};
        if (!state.isInitialized || path.data == nullptr || path.size == 0u)
        {
            return AudioStatus::InvalidArg;
        }

        if (state.backend != AudioSystemBackend::Platform)
        {
            return AudioStatus::NotSupported;
        }

        return state.platformBackend.LoadWavPcm16StreamClip(fileSystem, path, outClip);
    }

    [[nodiscard]] inline AudioStatus LoadWavPcm16StreamClip(AudioSystemState& state,
                                                            fs::FileSystemInterface& fileSystem,
                                                            const char* path,
                                                            AudioClipId& outClip) noexcept
    {
        outClip = AudioClipId{};
        fs::PathView pathView{};
        if (!detail::MakePathView(path, pathView))
        {
            return AudioStatus::InvalidArg;
        }
        return LoadWavPcm16StreamClip(state, fileSystem, pathView, outClip);
    }

    [[nodiscard]] inline bool InitAudioSystemWithInterface(AudioSystemState& state,
                                                           AudioInterface interface,
                                                           AudioSystemBackend backend) noexcept
    {
        if (state.isInitialized)
        {
            ShutdownAudioSystem(state);
        }

        if (interface.userData == nullptr ||
            interface.vtable.play == nullptr ||
            interface.vtable.stop == nullptr ||
            interface.vtable.setGain == nullptr ||
            interface.vtable.getCaps == nullptr ||
            interface.vtable.mix == nullptr)
        {
            return false;
        }

        state.interface = interface;
        state.backend = backend;
        state.isInitialized = true;
        return true;
    }

    [[nodiscard]] inline bool InitAudioSystem(AudioSystemState& state,
                                              const AudioSystemConfig& config) noexcept
    {
        ShutdownAudioSystem(state);

        switch (config.backend)
        {
            case AudioSystemBackend::Null:
            {
                AudioInterface iface = MakeNullAudioInterface(state.nullBackend);
                return InitAudioSystemWithInterface(state, iface, AudioSystemBackend::Null);
            }
            case AudioSystemBackend::Platform:
            {
                if (state.platformBackend.Init(config.platform))
                {
                    AudioInterface iface = MakeWinMmAudioInterface(state.platformBackend);
                    return InitAudioSystemWithInterface(state, iface, AudioSystemBackend::Platform);
                }

                if (config.fallbackToNullOnInitFailure)
                {
                    AudioInterface iface = MakeNullAudioInterface(state.nullBackend);
                    return InitAudioSystemWithInterface(state, iface, AudioSystemBackend::Null);
                }

                return false;
            }
            case AudioSystemBackend::External:
            {
                return false; // Must be injected via InitAudioSystemWithInterface.
            }
            default:
            {
                return false;
            }
        }
    }

    inline void ShutdownAudioSystem(AudioSystemState& state) noexcept
    {
        state.platformBackend.Shutdown();
        state = AudioSystemState{};
    }

    [[nodiscard]] inline AudioCaps QueryCaps(const AudioSystemState& state) noexcept
    {
        return state.isInitialized ? QueryCaps(state.interface) : AudioCaps{};
    }

    [[nodiscard]] inline AudioStatus UnloadClip(AudioSystemState& state, AudioClipId clip) noexcept
    {
        if (!state.isInitialized || !IsValid(clip))
        {
            return AudioStatus::InvalidArg;
        }

        if (state.backend != AudioSystemBackend::Platform)
        {
            return AudioStatus::NotSupported;
        }

        const AudioStatus flushStatus = detail::FlushCommands(state);

        for (dng::u32 slot = 0; slot < kAudioSystemMaxVoices; ++slot)
        {
            const AudioVoiceState& voiceState = state.voices[slot];
            if (!voiceState.isActive || voiceState.clip.value != clip.value)
            {
                continue;
            }

            AudioVoiceId voice{};
            voice.slot = slot;
            voice.generation = voiceState.generation;
            (void)dng::audio::Stop(state.interface, voice);
            detail::ReleaseVoice(state, voice);
        }

        const AudioStatus unloadStatus = state.platformBackend.UnloadClip(clip);
        if (unloadStatus != AudioStatus::Ok)
        {
            return unloadStatus;
        }

        return flushStatus;
    }

    [[nodiscard]] inline AudioStatus Play(AudioSystemState& state,
                                          const AudioPlayParams& params,
                                          AudioVoiceId& outVoice) noexcept
    {
        outVoice = AudioVoiceId{};
        if (!state.isInitialized)
        {
            return AudioStatus::InvalidArg;
        }

        if (!IsValid(params.clip) || !(params.gain >= 0.0f) || !(params.pitch > 0.0f))
        {
            return AudioStatus::InvalidArg;
        }

        if (state.backend == AudioSystemBackend::Platform &&
            !state.platformBackend.HasClip(params.clip))
        {
            return AudioStatus::InvalidArg;
        }

        AudioVoiceId voice{};
        if (!detail::AcquireVoice(state, params, voice))
        {
            return AudioStatus::NotSupported;
        }

        AudioCommand command{};
        command.type = AudioCommandType::Play;
        command.voice = voice;
        command.play = params;
        if (!detail::EnqueueCommand(state, command))
        {
            detail::ReleaseVoice(state, voice);
            return AudioStatus::NotSupported;
        }

        outVoice = voice;
        return AudioStatus::Ok;
    }

    [[nodiscard]] inline AudioStatus Stop(AudioSystemState& state, AudioVoiceId voice) noexcept
    {
        if (!state.isInitialized || !IsVoiceActive(state, voice))
        {
            return AudioStatus::InvalidArg;
        }

        AudioCommand command{};
        command.type = AudioCommandType::Stop;
        command.voice = voice;
        if (!detail::EnqueueCommand(state, command))
        {
            return AudioStatus::NotSupported;
        }

        detail::ReleaseVoice(state, voice);
        return AudioStatus::Ok;
    }

    [[nodiscard]] inline AudioStatus SetGain(AudioSystemState& state,
                                             AudioVoiceId voice,
                                             float gain) noexcept
    {
        if (!state.isInitialized || !IsVoiceActive(state, voice) || !(gain >= 0.0f))
        {
            return AudioStatus::InvalidArg;
        }

        AudioCommand command{};
        command.type = AudioCommandType::SetGain;
        command.voice = voice;
        command.gain = gain;
        if (!detail::EnqueueCommand(state, command))
        {
            return AudioStatus::NotSupported;
        }

        state.voices[voice.slot].gain = gain;
        return AudioStatus::Ok;
    }

    [[nodiscard]] inline AudioStatus Mix(AudioSystemState& state, AudioMixParams& params) noexcept
    {
        if (!state.isInitialized)
        {
            params.writtenSamples = 0;
            return AudioStatus::InvalidArg;
        }

        const AudioStatus commandStatus = detail::FlushCommands(state);
        const AudioStatus mixStatus = dng::audio::Mix(state.interface, params);
        if (mixStatus != AudioStatus::Ok)
        {
            return mixStatus;
        }
        return commandStatus;
    }

} // namespace dng::audio
