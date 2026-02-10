// ============================================================================
// D-Engine - Source/Core/Audio/AudioSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : High-level audio system that owns a backend instance and exposes
//           unified frame mixing to the rest of the engine.
// Contract: Header-only, no exceptions/RTTI, no allocations in this layer.
//           Lifetime of the backend is tied to AudioSystemState.
//           Thread-safety and determinism follow AudioCaps from the backend;
//           callers must serialize access per instance.
// Notes   : Defaults to the NullAudio backend but accepts external backends
//           via interface injection.
// ============================================================================

#pragma once

#include "Core/Contracts/Audio.hpp"
#include "Core/Audio/NullAudio.hpp"

namespace dng::audio
{
    enum class AudioSystemBackend : dng::u8
    {
        Null,
        External
    };

    struct AudioSystemConfig
    {
        AudioSystemBackend backend = AudioSystemBackend::Null;
    };

    struct AudioSystemState
    {
        AudioInterface     interface{};
        AudioSystemBackend backend = AudioSystemBackend::Null;
        NullAudio          nullBackend{};
        bool               isInitialized = false;
    };

    [[nodiscard]] inline bool InitAudioSystemWithInterface(AudioSystemState& state,
                                                           AudioInterface interface,
                                                           AudioSystemBackend backend) noexcept
    {
        if (interface.userData == nullptr ||
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
        state = AudioSystemState{};

        switch (config.backend)
        {
            case AudioSystemBackend::Null:
            {
                AudioInterface iface = MakeNullAudioInterface(state.nullBackend);
                return InitAudioSystemWithInterface(state, iface, AudioSystemBackend::Null);
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
        state.interface = AudioInterface{};
        state.backend = AudioSystemBackend::Null;
        state.nullBackend = NullAudio{};
        state.isInitialized = false;
    }

    [[nodiscard]] inline AudioCaps QueryCaps(const AudioSystemState& state) noexcept
    {
        return state.isInitialized ? QueryCaps(state.interface) : AudioCaps{};
    }

    [[nodiscard]] inline AudioStatus Mix(AudioSystemState& state, AudioMixParams& params) noexcept
    {
        if (!state.isInitialized)
        {
            params.writtenSamples = 0;
            return AudioStatus::InvalidArg;
        }
        return Mix(state.interface, params);
    }

} // namespace dng::audio
