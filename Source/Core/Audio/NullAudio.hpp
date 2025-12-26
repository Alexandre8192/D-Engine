// ============================================================================
// D-Engine - Source/Core/Audio/NullAudio.hpp
// ----------------------------------------------------------------------------
// Purpose : Deterministic audio backend that satisfies the audio contract
//           without producing sound. Useful for tests, tools, and CI.
// Contract: Header-only, no exceptions/RTTI, no allocations. All methods are
//           noexcept and deterministic.
// Notes   : Tracks simple stats (plays/stops/active voices) and stores bus
//           volumes as functional state (not stats). Handles increment
//           monotonically to preserve determinism.
// ============================================================================

#pragma once

#include "Core/Contracts/Audio.hpp"

namespace dng::audio
{
    struct NullAudioStats
    {
        dng::u32 totalPlays   = 0;
        dng::u32 totalStops   = 0;
        dng::u32 activeVoices = 0;
    };

    struct NullAudio
    {
        AudioCaps      caps{.deterministic = true, .has_buses = true};
        NullAudioStats stats{};
        float          m_busVolumes[static_cast<dng::u32>(AudioBus::Count)]{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        HandleValue    nextVoiceValue = 1;

        constexpr NullAudio() noexcept = default;

        // Purpose : Report deterministic, bus-supporting capabilities.
        // Contract: Immutable after construction; no allocations or side effects.
        // Notes   : has_buses is true; other flags stay defaults.
        [[nodiscard]] AudioCaps GetCaps() const noexcept
        {
            return caps;
        }

        // Purpose : Issue a new deterministic voice handle and track stats.
        // Contract: No allocations; VoiceHandle::Invalid returned when sound
        //           handle is invalid. Active voice count increments on success.
        // Notes   : Deterministic sequence (1,2,3,...) independent of bus state.
        [[nodiscard]] VoiceHandle Play(SoundHandle sound, const PlayParams&) noexcept
        {
            if (!sound.IsValid())
            {
                return VoiceHandle::Invalid();
            }

            ++stats.totalPlays;
            ++stats.activeVoices;

            VoiceHandle voice{nextVoiceValue};
            ++nextVoiceValue;
            return voice;
        }

        // Purpose : Stop a voice and update stats deterministically.
        // Contract: No-op for invalid handles; activeVoices will not underflow.
        // Notes   : Stats reflect the call even when the handle was invalid.
        void Stop(VoiceHandle voice) noexcept
        {
            ++stats.totalStops;
            if (voice.IsValid() && stats.activeVoices > 0)
            {
                --stats.activeVoices;
            }
        }

        // Purpose : Store bus volume in backend state.
        // Contract: Ignores invalid buses; clamps gain to [0,1]; no allocations.
        // Notes   : Master volume is just the Master bus entry.
        void SetBusVolume(AudioBus bus, float value) noexcept
        {
            const dng::u32 idx = static_cast<dng::u32>(bus);
            if (idx >= static_cast<dng::u32>(AudioBus::Count))
            {
                return;
            }

            float clamped = value;
            if (!(clamped >= 0.0f))
            {
                clamped = 0.0f;
            }
            if (clamped > 1.0f)
            {
                clamped = 1.0f;
            }
            m_busVolumes[idx] = clamped;
        }

        [[nodiscard]] float GetBusVolume(AudioBus bus) const noexcept
        {
            const dng::u32 idx = static_cast<dng::u32>(bus);
            if (idx >= static_cast<dng::u32>(AudioBus::Count))
            {
                return 0.0f;
            }
            return m_busVolumes[idx];
        }
    };

    static_assert(AudioBackend<NullAudio>, "NullAudio must satisfy audio backend concept.");

    // Purpose : Wrap NullAudio into an AudioInterface without transferring ownership.
    // Contract: Backend lifetime must outlive the interface; no allocations.
    // Notes   : Enables injecting NullAudio into AudioSystem for tests.
    [[nodiscard]] inline AudioInterface MakeNullAudioInterface(NullAudio& backend) noexcept
    {
        // Initialize bus volumes to unity on interface creation to ensure
        // deterministic starting state even if the struct was value-initialized.
        for (dng::u32 i = 0; i < static_cast<dng::u32>(AudioBus::Count); ++i)
        {
            backend.m_busVolumes[i] = 1.0f;
        }
        return MakeAudioInterface(backend);
    }

} // namespace dng::audio
