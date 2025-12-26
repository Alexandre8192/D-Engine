// ============================================================================
// D-Engine - Source/Core/Audio/AudioSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : Audio orchestrator that stores an injected AudioInterface, caches
//           capabilities, and manages engine-side bus volumes.
// Contract: Header-only, no exceptions/RTTI, no allocations. Does not include
//           or own any concrete backend; lifetime of the backend is controlled
//           by the caller. Thread-safety is delegated to the backend owner.
// Notes   : Master volume is treated as the Master bus. Bus volumes are cached
//           engine-side and pushed to the backend when supported.
// ============================================================================

#pragma once

#include "Core/Contracts/Audio.hpp"

namespace dng::audio
{
    struct AudioSystemState
    {
        AudioInterface iface{};
        AudioCaps      caps{};
        float          busVolumes[static_cast<dng::u32>(AudioBus::Count)]{};
        bool           isInitialized = false;
    };

    // Purpose : Initialize the audio system with an external interface.
    // Contract: Returns false if the interface is incomplete (missing userData
    //           or required function pointers). No allocations performed. On
    //           success, bus volumes are reset to 1.0 and applied when supported.
    // Notes   : Backend ownership stays with the caller; this function only
    //           stores the non-owning interface and cached caps.
    [[nodiscard]] inline bool InitAudioSystem(AudioSystemState& state, AudioInterface iface) noexcept
    {
        state = AudioSystemState{};

        if (iface.userData == nullptr ||
            iface.vtable.play == nullptr ||
            iface.vtable.stop == nullptr ||
            iface.vtable.getCaps == nullptr)
        {
            return false;
        }

        state.iface     = iface;
        state.caps      = QueryCaps(iface);

        for (dng::u32 i = 0; i < static_cast<dng::u32>(AudioBus::Count); ++i)
        {
            state.busVolumes[i] = 1.0f;
            if (state.iface.vtable.setBusVolume != nullptr)
            {
                SetBusVolume(state.iface, static_cast<AudioBus>(i), 1.0f);
            }
        }

        state.isInitialized = true;
        return true;
    }

    // Purpose : Reset the audio system to an uninitialized state.
    // Contract: Releases no resources (non-owning); clears cached state.
    // Notes   : Safe to call on an already-shutdown system.
    inline void ShutdownAudioSystem(AudioSystemState& state) noexcept
    {
        state = AudioSystemState{};
    }

    // Purpose : Retrieve cached capabilities after initialization.
    // Contract: Returns default AudioCaps when uninitialized.
    // Notes   : Caps are cached from QueryCaps during initialization.
    [[nodiscard]] inline AudioCaps GetAudioCaps(const AudioSystemState& state) noexcept
    {
        return state.isInitialized ? state.caps : AudioCaps{};
    }

    // Purpose : Forward Play to the injected backend.
    // Contract: Returns VoiceHandle::Invalid when uninitialized. No allocations.
    // Notes   : Determinism is backend-defined; uses cached interface only.
    [[nodiscard]] inline VoiceHandle SysPlay(AudioSystemState& state, SoundHandle sound, const PlayParams& params) noexcept
    {
#if DNG_DEBUG
        DNG_ASSERT(state.isInitialized);
#endif
        if (!state.isInitialized)
        {
            return VoiceHandle::Invalid();
        }
        return Play(state.iface, sound, params);
    }

    // Purpose : Forward Stop to the injected backend.
    // Contract: No-op when uninitialized; backend decides behavior for invalid
    //           handles.
    // Notes   : Allocation-free pass-through.
    inline void SysStop(AudioSystemState& state, VoiceHandle voice) noexcept
    {
#if DNG_DEBUG
        DNG_ASSERT(state.isInitialized);
#endif
        if (!state.isInitialized)
        {
            return;
        }
        Stop(state.iface, voice);
    }

    // Purpose : Set engine-side bus volume and apply to backend if supported.
    // Contract: Debug validation occurs in the contract wrapper. No-op when the
    //           system is uninitialized; cached value is stored regardless.
    // Notes   : Master bus uses AudioBus::Master; backend call is conditional on
    //           setBusVolume presence.
    inline void SysSetBusVolume(AudioSystemState& state, AudioBus bus, float value) noexcept
    {
        const dng::u32 idx = static_cast<dng::u32>(bus);
        if (idx >= static_cast<dng::u32>(AudioBus::Count))
        {
            return;
        }

        const float clamped = detail::ClampGain01(value);
        state.busVolumes[idx] = clamped;

#if DNG_DEBUG
        DNG_ASSERT(state.isInitialized);
#endif
        if (!state.isInitialized)
        {
            return;
        }

        SetBusVolume(state.iface, bus, clamped);
    }

    // Purpose : Alias for setting master volume via the Master bus.
    // Contract: Same validation as SetBusVolume; cached state updated.
    // Notes   : Provided for API ergonomics.
    inline void SysSetMasterVolume(AudioSystemState& state, float value) noexcept
    {
        SysSetBusVolume(state, AudioBus::Master, value);
    }

} // namespace dng::audio
