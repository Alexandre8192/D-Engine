// ============================================================================
// D-Engine - Source/Core/Audio/AudioSystem.cpp
// ----------------------------------------------------------------------------
// Purpose : Out-of-line AudioSystem implementation that keeps backend details
//           and heavy logic out of the public facade.
// Contract: No exceptions/RTTI. Built-in backends are heap-allocated during
//           initialization behind an opaque state pointer. Shutdown is
//           idempotent and always releases any owned backend before zeroing the
//           state.
// Notes   : External interfaces remain injectable through
//           InitAudioSystemWithInterface(); built-in backend-specific helpers
//           only operate when AudioSystemState owns that backend instance.
// ============================================================================

#include "Core/Audio/AudioSystem.hpp"

#include "Core/Audio/NullAudio.hpp"
#include "Core/Audio/WinMmAudio.hpp"

#include <new>

namespace dng::audio
{
    namespace
    {
        [[nodiscard]] NullAudio* GetOwnedNullBackend(AudioSystemState& state) noexcept
        {
            return (state.backend == AudioSystemBackend::Null)
                ? static_cast<NullAudio*>(state.ownedBackendState)
                : nullptr;
        }

        [[nodiscard]] const NullAudio* GetOwnedNullBackend(const AudioSystemState& state) noexcept
        {
            return (state.backend == AudioSystemBackend::Null)
                ? static_cast<const NullAudio*>(state.ownedBackendState)
                : nullptr;
        }

        [[nodiscard]] WinMmAudio* GetOwnedPlatformBackend(AudioSystemState& state) noexcept
        {
            return (state.backend == AudioSystemBackend::Platform)
                ? static_cast<WinMmAudio*>(state.ownedBackendState)
                : nullptr;
        }

        [[nodiscard]] const WinMmAudio* GetOwnedPlatformBackend(const AudioSystemState& state) noexcept
        {
            return (state.backend == AudioSystemBackend::Platform)
                ? static_cast<const WinMmAudio*>(state.ownedBackendState)
                : nullptr;
        }

        [[nodiscard]] bool HasOwnedPlatformBackend(const AudioSystemState& state) noexcept
        {
            return state.backend == AudioSystemBackend::Platform &&
                   state.ownedBackendState != nullptr;
        }

        [[nodiscard]] bool IsValidInterface(const AudioInterface& interface) noexcept
        {
            return interface.userData != nullptr &&
                   interface.vtable.play != nullptr &&
                   interface.vtable.stop != nullptr &&
                   interface.vtable.pause != nullptr &&
                   interface.vtable.resume != nullptr &&
                   interface.vtable.seek != nullptr &&
                   interface.vtable.setGain != nullptr &&
                   interface.vtable.setBusGain != nullptr &&
                   interface.vtable.getCaps != nullptr &&
                   interface.vtable.mix != nullptr;
        }

        inline void DestroyOwnedBackend(AudioSystemState& state) noexcept
        {
            switch (state.backend)
            {
                case AudioSystemBackend::Null:
                {
                    if (NullAudio* backend = GetOwnedNullBackend(state))
                    {
                        delete backend;
                    }
                    break;
                }
                case AudioSystemBackend::Platform:
                {
                    if (WinMmAudio* backend = GetOwnedPlatformBackend(state))
                    {
                        backend->Shutdown();
                        delete backend;
                    }
                    break;
                }
                case AudioSystemBackend::External:
                default:
                {
                    break;
                }
            }
            state.ownedBackendState = nullptr;
        }

        [[nodiscard]] NullAudio* ConstructOwnedNullBackend(AudioSystemState& state) noexcept
        {
            NullAudio* backend = new (std::nothrow) NullAudio{};
            state.ownedBackendState = backend;
            return backend;
        }

        [[nodiscard]] WinMmAudio* ConstructOwnedPlatformBackend(AudioSystemState& state) noexcept
        {
            WinMmAudio* backend = new (std::nothrow) WinMmAudio{};
            state.ownedBackendState = backend;
            return backend;
        }

        [[nodiscard]] AudioStatus MapFsStatus(fs::FsStatus status) noexcept
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

        [[nodiscard]] bool IsSameFileSystemInterface(const fs::FileSystemInterface& a,
                                                     const fs::FileSystemInterface& b) noexcept
        {
            return a.userData == b.userData &&
                   a.vtable.exists == b.vtable.exists &&
                   a.vtable.fileSize == b.vtable.fileSize &&
                   a.vtable.readFile == b.vtable.readFile &&
                   a.vtable.readFileRange == b.vtable.readFileRange &&
                   a.vtable.getCaps == b.vtable.getCaps;
        }

        [[nodiscard]] WinMmAudioConfig ToWinMmAudioConfig(const AudioPlatformConfig& config) noexcept
        {
            WinMmAudioConfig backendConfig{};
            backendConfig.sampleRate = config.sampleRate;
            backendConfig.channelCount = config.channelCount;
            backendConfig.reserved = config.reserved;
            backendConfig.framesPerBuffer = config.framesPerBuffer;
            return backendConfig;
        }

        [[nodiscard]] bool EnqueueCommand(AudioSystemState& state, const AudioCommand& command) noexcept
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

        [[nodiscard]] bool DequeueCommand(AudioSystemState& state, AudioCommand& outCommand) noexcept
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

        [[nodiscard]] bool AcquireVoice(AudioSystemState& state,
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
                voiceState.bus = params.bus;
                voiceState.isActive = true;
                voiceState.isPaused = false;
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
            voiceState.bus = AudioBus::Master;
            voiceState.isActive = false;
            voiceState.isPaused = false;
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

        [[nodiscard]] AudioStatus FlushCommands(AudioSystemState& state) noexcept
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
                    case AudioCommandType::Pause:
                    {
                        commandStatus = dng::audio::Pause(state.interface, command.voice);
                        break;
                    }
                    case AudioCommandType::Resume:
                    {
                        commandStatus = dng::audio::Resume(state.interface, command.voice);
                        break;
                    }
                    case AudioCommandType::Seek:
                    {
                        commandStatus = dng::audio::Seek(state.interface, command.voice, command.seekFrameIndex);
                        break;
                    }
                    case AudioCommandType::SetGain:
                    {
                        commandStatus = dng::audio::SetGain(state.interface, command.voice, command.gain);
                        break;
                    }
                    case AudioCommandType::SetBusGain:
                    {
                        commandStatus = dng::audio::SetBusGain(state.interface, command.bus, command.gain);
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

        [[nodiscard]] bool MakePathView(const char* path, fs::PathView& outPath) noexcept
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

        inline thread_local dng::u8 g_AudioWavLoadScratch[kAudioSystemWavLoadScratchBytes]{};
    } // namespace

    dng::u64 GetUnderrunCount(const AudioSystemState& state) noexcept
    {
        const WinMmAudio* backend = GetOwnedPlatformBackend(state);
        return backend ? backend->GetUnderrunCount() : 0u;
    }

    dng::u64 GetSubmitErrorCount(const AudioSystemState& state) noexcept
    {
        const WinMmAudio* backend = GetOwnedPlatformBackend(state);
        return backend ? backend->GetSubmitErrorCount() : 0u;
    }

    dng::u32 GetLoadedClipCount(const AudioSystemState& state) noexcept
    {
        const WinMmAudio* backend = GetOwnedPlatformBackend(state);
        return backend ? backend->GetLoadedClipCount() : 0u;
    }

    dng::u32 GetLoadedStreamClipCount(const AudioSystemState& state) noexcept
    {
        const WinMmAudio* backend = GetOwnedPlatformBackend(state);
        return backend ? backend->GetLoadedStreamClipCount() : 0u;
    }

    dng::u32 GetMaxStreamClipCount(const AudioSystemState& state) noexcept
    {
        const WinMmAudio* backend = GetOwnedPlatformBackend(state);
        return backend ? backend->GetMaxStreamClipCount() : 0u;
    }

    dng::u32 GetClipPoolUsageSamples(const AudioSystemState& state) noexcept
    {
        const WinMmAudio* backend = GetOwnedPlatformBackend(state);
        return backend ? backend->GetClipPoolUsageSamples() : 0u;
    }

    dng::u32 GetClipPoolCapacitySamples(const AudioSystemState& state) noexcept
    {
        const WinMmAudio* backend = GetOwnedPlatformBackend(state);
        return backend ? backend->GetClipPoolCapacitySamples() : 0u;
    }

    dng::u32 GetMaxClipCount(const AudioSystemState& state) noexcept
    {
        const WinMmAudio* backend = GetOwnedPlatformBackend(state);
        return backend ? backend->GetMaxClipCount() : 0u;
    }

    AudioStatus LoadWavPcm16Clip(AudioSystemState& state,
                                 fs::FileSystemInterface& fileSystem,
                                 fs::PathView path,
                                 AudioClipId& outClip) noexcept
    {
        outClip = AudioClipId{};
        if (!state.isInitialized || path.data == nullptr || path.size == 0u)
        {
            return AudioStatus::InvalidArg;
        }

        WinMmAudio* backend = GetOwnedPlatformBackend(state);
        if (backend == nullptr)
        {
            return AudioStatus::NotSupported;
        }

        dng::u64 fileSize = 0;
        const fs::FsStatus sizeStatus = fs::FileSize(fileSystem, path, fileSize);
        if (sizeStatus != fs::FsStatus::Ok)
        {
            return MapFsStatus(sizeStatus);
        }

        if (fileSize == 0 ||
            fileSize > static_cast<dng::u64>(kAudioSystemWavLoadScratchBytes) ||
            fileSize > static_cast<dng::u64>(~dng::u32{0}))
        {
            return AudioStatus::NotSupported;
        }

        dng::u64 bytesRead = 0;
        const fs::FsStatus readStatus =
            fs::ReadFile(fileSystem, path, g_AudioWavLoadScratch, fileSize, bytesRead);
        if (readStatus != fs::FsStatus::Ok)
        {
            return MapFsStatus(readStatus);
        }

        if (bytesRead != fileSize)
        {
            return AudioStatus::UnknownError;
        }

        return backend->LoadWavPcm16Clip(g_AudioWavLoadScratch,
                                         static_cast<dng::u32>(bytesRead),
                                         outClip);
    }

    AudioStatus LoadWavPcm16Clip(AudioSystemState& state,
                                 fs::FileSystemInterface& fileSystem,
                                 const char* path,
                                 AudioClipId& outClip) noexcept
    {
        outClip = AudioClipId{};
        fs::PathView pathView{};
        if (!MakePathView(path, pathView))
        {
            return AudioStatus::InvalidArg;
        }

        return LoadWavPcm16Clip(state, fileSystem, pathView, outClip);
    }

    AudioStatus BindStreamFileSystem(AudioSystemState& state,
                                     fs::FileSystemInterface& fileSystem) noexcept
    {
        if (!state.isInitialized)
        {
            return AudioStatus::InvalidArg;
        }

        WinMmAudio* backend = GetOwnedPlatformBackend(state);
        if (backend == nullptr)
        {
            return AudioStatus::NotSupported;
        }

        const AudioStatus bindStatus = backend->BindStreamFileSystem(fileSystem);
        if (bindStatus != AudioStatus::Ok)
        {
            return bindStatus;
        }

        state.streamFileSystem = fileSystem;
        state.hasStreamFileSystem = true;
        return AudioStatus::Ok;
    }

    AudioStatus UnbindStreamFileSystem(AudioSystemState& state) noexcept
    {
        if (!state.isInitialized)
        {
            return AudioStatus::InvalidArg;
        }

        WinMmAudio* backend = GetOwnedPlatformBackend(state);
        if (backend == nullptr)
        {
            return AudioStatus::NotSupported;
        }

        const AudioStatus unbindStatus = backend->UnbindStreamFileSystem();
        if (unbindStatus != AudioStatus::Ok)
        {
            return unbindStatus;
        }

        state.streamFileSystem = fs::FileSystemInterface{};
        state.hasStreamFileSystem = false;
        return AudioStatus::Ok;
    }

    AudioStatus LoadWavPcm16StreamClip(AudioSystemState& state,
                                       fs::FileSystemInterface& fileSystem,
                                       fs::PathView path,
                                       AudioClipId& outClip) noexcept
    {
        outClip = AudioClipId{};
        if (!state.isInitialized || path.data == nullptr || path.size == 0u)
        {
            return AudioStatus::InvalidArg;
        }

        WinMmAudio* backend = GetOwnedPlatformBackend(state);
        if (backend == nullptr)
        {
            return AudioStatus::NotSupported;
        }

        if (!state.hasStreamFileSystem ||
            !IsSameFileSystemInterface(state.streamFileSystem, fileSystem))
        {
            return AudioStatus::NotSupported;
        }

        return backend->LoadWavPcm16StreamClip(path, outClip);
    }

    AudioStatus LoadWavPcm16StreamClip(AudioSystemState& state,
                                       fs::FileSystemInterface& fileSystem,
                                       const char* path,
                                       AudioClipId& outClip) noexcept
    {
        outClip = AudioClipId{};
        fs::PathView pathView{};
        if (!MakePathView(path, pathView))
        {
            return AudioStatus::InvalidArg;
        }

        return LoadWavPcm16StreamClip(state, fileSystem, pathView, outClip);
    }

    bool InitAudioSystemWithInterface(AudioSystemState& state,
                                      AudioInterface interface) noexcept
    {
        ShutdownAudioSystem(state);

        if (!IsValidInterface(interface))
        {
            return false;
        }

        state.interface = interface;
        state.backend = AudioSystemBackend::External;
        state.isInitialized = true;
        return true;
    }

    bool InitAudioSystem(AudioSystemState& state, const AudioSystemConfig& config) noexcept
    {
        ShutdownAudioSystem(state);

        switch (config.backend)
        {
            case AudioSystemBackend::Null:
            {
                NullAudio* backend = ConstructOwnedNullBackend(state);
                if (backend == nullptr)
                {
                    state = AudioSystemState{};
                    return false;
                }
                state.interface = MakeNullAudioInterface(*backend);
                state.backend = AudioSystemBackend::Null;
                state.isInitialized = true;
                return true;
            }
            case AudioSystemBackend::Platform:
            {
                WinMmAudio* backend = ConstructOwnedPlatformBackend(state);
                if (backend != nullptr && backend->Init(ToWinMmAudioConfig(config.platform)))
                {
                    state.interface = MakeWinMmAudioInterface(*backend);
                    state.backend = AudioSystemBackend::Platform;
                    state.isInitialized = true;
                    return true;
                }

                if (backend != nullptr)
                {
                    delete backend;
                    state.ownedBackendState = nullptr;
                }

                if (config.fallbackToNullOnInitFailure)
                {
                    NullAudio* nullBackend = ConstructOwnedNullBackend(state);
                    if (nullBackend == nullptr)
                    {
                        state = AudioSystemState{};
                        return false;
                    }
                    state.interface = MakeNullAudioInterface(*nullBackend);
                    state.backend = AudioSystemBackend::Null;
                    state.isInitialized = true;
                    return true;
                }

                state = AudioSystemState{};
                return false;
            }
            case AudioSystemBackend::External:
            default:
            {
                return false;
            }
        }
    }

    void ShutdownAudioSystem(AudioSystemState& state) noexcept
    {
        DestroyOwnedBackend(state);
        state = AudioSystemState{};
    }

    AudioStatus UnloadClip(AudioSystemState& state, AudioClipId clip) noexcept
    {
        if (!state.isInitialized || !IsValid(clip))
        {
            return AudioStatus::InvalidArg;
        }

        WinMmAudio* backend = GetOwnedPlatformBackend(state);
        if (backend == nullptr)
        {
            return AudioStatus::NotSupported;
        }

        const AudioStatus flushStatus = FlushCommands(state);

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
            ReleaseVoice(state, voice);
        }

        const AudioStatus unloadStatus = backend->UnloadClip(clip);
        if (unloadStatus != AudioStatus::Ok)
        {
            return unloadStatus;
        }

        return flushStatus;
    }

    AudioStatus Play(AudioSystemState& state,
                     const AudioPlayParams& params,
                     AudioVoiceId& outVoice) noexcept
    {
        outVoice = AudioVoiceId{};
        if (!state.isInitialized)
        {
            return AudioStatus::InvalidArg;
        }

        if (!IsValid(params.clip) || !IsValid(params.bus) || !(params.gain >= 0.0f) || !(params.pitch > 0.0f))
        {
            return AudioStatus::InvalidArg;
        }

        if (HasOwnedPlatformBackend(state))
        {
            const WinMmAudio* backend = GetOwnedPlatformBackend(state);
            if (backend != nullptr && !backend->HasClip(params.clip))
            {
                return AudioStatus::InvalidArg;
            }
        }

        AudioVoiceId voice{};
        if (!AcquireVoice(state, params, voice))
        {
            return AudioStatus::NotSupported;
        }

        AudioCommand command{};
        command.type = AudioCommandType::Play;
        command.voice = voice;
        command.play = params;
        if (!EnqueueCommand(state, command))
        {
            ReleaseVoice(state, voice);
            return AudioStatus::NotSupported;
        }

        outVoice = voice;
        return AudioStatus::Ok;
    }

    AudioStatus Stop(AudioSystemState& state, AudioVoiceId voice) noexcept
    {
        if (!state.isInitialized || !IsVoiceActive(state, voice))
        {
            return AudioStatus::InvalidArg;
        }

        AudioCommand command{};
        command.type = AudioCommandType::Stop;
        command.voice = voice;
        if (!EnqueueCommand(state, command))
        {
            return AudioStatus::NotSupported;
        }

        ReleaseVoice(state, voice);
        return AudioStatus::Ok;
    }

    AudioStatus SetGain(AudioSystemState& state,
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
        if (!EnqueueCommand(state, command))
        {
            return AudioStatus::NotSupported;
        }

        state.voices[voice.slot].gain = gain;
        return AudioStatus::Ok;
    }

    AudioStatus Pause(AudioSystemState& state, AudioVoiceId voice) noexcept
    {
        if (!state.isInitialized || !IsVoiceActive(state, voice))
        {
            return AudioStatus::InvalidArg;
        }

        AudioCommand command{};
        command.type = AudioCommandType::Pause;
        command.voice = voice;
        if (!EnqueueCommand(state, command))
        {
            return AudioStatus::NotSupported;
        }

        state.voices[voice.slot].isPaused = true;
        return AudioStatus::Ok;
    }

    AudioStatus Resume(AudioSystemState& state, AudioVoiceId voice) noexcept
    {
        if (!state.isInitialized || !IsVoiceActive(state, voice))
        {
            return AudioStatus::InvalidArg;
        }

        AudioCommand command{};
        command.type = AudioCommandType::Resume;
        command.voice = voice;
        if (!EnqueueCommand(state, command))
        {
            return AudioStatus::NotSupported;
        }

        state.voices[voice.slot].isPaused = false;
        return AudioStatus::Ok;
    }

    AudioStatus Seek(AudioSystemState& state,
                     AudioVoiceId voice,
                     dng::u32 frameIndex) noexcept
    {
        if (!state.isInitialized || !IsVoiceActive(state, voice))
        {
            return AudioStatus::InvalidArg;
        }

        AudioCommand command{};
        command.type = AudioCommandType::Seek;
        command.voice = voice;
        command.seekFrameIndex = frameIndex;
        if (!EnqueueCommand(state, command))
        {
            return AudioStatus::NotSupported;
        }

        return AudioStatus::Ok;
    }

    AudioStatus SetBusGain(AudioSystemState& state,
                           AudioBus bus,
                           float gain) noexcept
    {
        if (!state.isInitialized || !IsValid(bus) || !(gain >= 0.0f))
        {
            return AudioStatus::InvalidArg;
        }

        AudioCommand command{};
        command.type = AudioCommandType::SetBusGain;
        command.bus = bus;
        command.gain = gain;
        if (!EnqueueCommand(state, command))
        {
            return AudioStatus::NotSupported;
        }

        state.busGains[static_cast<dng::u32>(bus)] = gain;
        return AudioStatus::Ok;
    }

    AudioStatus Mix(AudioSystemState& state, AudioMixParams& params) noexcept
    {
        if (!state.isInitialized)
        {
            params.writtenSamples = 0;
            return AudioStatus::InvalidArg;
        }

        const AudioStatus commandStatus = FlushCommands(state);
        const AudioStatus mixStatus = dng::audio::Mix(state.interface, params);
        if (mixStatus != AudioStatus::Ok)
        {
            return mixStatus;
        }

        return commandStatus;
    }

} // namespace dng::audio
