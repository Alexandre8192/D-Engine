// ============================================================================
// D-Engine - Source/Core/Contracts/Audio.hpp
// ----------------------------------------------------------------------------
// Purpose : Audio contract describing backend-agnostic playback handles,
//           bus-aware play parameters, and tiny dynamic/static faces so
//           multiple audio backends can plug into Core without leaking
//           implementation details.
// Contract: Header-only, no exceptions/RTTI, engine-absolute includes only.
//           All types are POD or trivially copyable; no allocations occur in
//           this layer. Thread-safety is left to the backend; callers must
//           externally synchronize per backend instance.
// Notes   : Volume values are linear gain in [0..1]; pitch is a playback rate
//           ratio (1.0f = normal, must stay > 0). Debug builds assert invalid
//           ranges and null interfaces; release clamps at the contract wrapper
//           (source of truth) and forwards the clamped value. Master volume is
//           an alias for the Master bus, not a separate backend concept. Backends
//           may re-clamp as a seatbelt but the contract wrapper defines the
//           canonical clamp behavior.
// ============================================================================

#pragma once

#define DNG_DEFINE_MINIMAL_ASSERT 1
#include "Core/Diagnostics/Check.hpp"
#include "Core/Types.hpp"
#undef DNG_DEFINE_MINIMAL_ASSERT

#include <algorithm>
#include <cmath>
#include <concepts>
#include <type_traits>

namespace dng::audio
{
    using HandleValue = dng::u32;

    // ------------------------------------------------------------------------
    // Handles (opaque, non-owning views over backend resources)
    // ------------------------------------------------------------------------

    // Purpose : Identifies a backend-managed sound resource.
    // Contract: Value 0 is invalid; no ownership or lifetime extension.
    // Notes   : Backend decides how ids map to decoded/loaded data.
    struct SoundHandle
    {
        HandleValue value = 0;

        constexpr SoundHandle() noexcept = default;
        explicit constexpr SoundHandle(HandleValue raw) noexcept : value(raw) {}

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] static constexpr SoundHandle Invalid() noexcept { return SoundHandle{}; }
    };

    // Purpose : Identifies an active playback instance.
    // Contract: Value 0 is invalid; lifetime is backend-defined and non-owning.
    // Notes   : Deterministic backends increment monotonically.
    struct VoiceHandle
    {
        HandleValue value = 0;

        constexpr VoiceHandle() noexcept = default;
        explicit constexpr VoiceHandle(HandleValue raw) noexcept : value(raw) {}

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] static constexpr VoiceHandle Invalid() noexcept { return VoiceHandle{}; }
    };

    static_assert(sizeof(SoundHandle) == sizeof(HandleValue), "SoundHandle must stay compact.");
    static_assert(sizeof(VoiceHandle) == sizeof(HandleValue), "VoiceHandle must stay compact.");
    static_assert(std::is_trivially_copyable_v<SoundHandle>);
    static_assert(std::is_trivially_copyable_v<VoiceHandle>);

    // ------------------------------------------------------------------------
    // Enumerations and capability flags
    // ------------------------------------------------------------------------

    // Purpose : Labels logical volume buses for routing and gain control.
    // Contract: Values are stable; Master is the canonical bus for global gain.
    // Notes   : Count sentinel enables fixed-size arrays on the engine side.
    enum class AudioBus : dng::u8
    {
        Master,
        Music,
        Sfx,
        Ui,
        Voice,
        Count
    };

    // Purpose : Capability hints returned by every backend.
    // Contract: Flags are immutable after initialization; callers still must
    //           provide fallbacks when a feature is unavailable.
    // Notes   : Buses are considered supported when has_buses is true or when
    //           setBusVolume is present in the v-table.
    struct AudioCaps
    {
        bool   deterministic        = false;
        bool   has_buses            = false;
        bool   has_spatial          = false;
        bool   has_streaming        = false;
        bool   has_effects          = false;
        dng::u32 max_voices         = 0; // 0 = unknown/unbounded.
        dng::u32 preferred_rate_hz  = 0; // 0 = unknown.
        dng::u8  output_channels_min = 0; // 0 = unknown.
        dng::u8  output_channels_max = 0; // 0 = unknown.
    };

    static_assert(std::is_trivially_copyable_v<AudioCaps>, "AudioCaps must stay POD for telemetry dumps.");

    // ------------------------------------------------------------------------
    // Playback parameters
    // ------------------------------------------------------------------------

    // Purpose : Describes how to start playback of a sound.
    // Contract: volume is linear gain in [0..1]; pitch is a playback rate ratio
    //           (1.0f = normal, must be > 0); startTimeSeconds is in seconds and
    //           >= 0.0f; bus selects the logical routing target. Debug asserts
    //           on invalid ranges and null interfaces.
    // Notes   : All fields are POD; contract wrapper clamps gain to [0..1] in
    //           release; backend may re-clamp as a seatbelt.
    struct PlayParams
    {
        float     volume           = 1.0f;
        float     pitch            = 1.0f;
        float     startTimeSeconds = 0.0f;
        bool      loop             = false;
        AudioBus  bus              = AudioBus::Master;
    };

    static_assert(std::is_trivially_copyable_v<PlayParams>);

    // ------------------------------------------------------------------------
    // Dynamic face (tiny v-table for late binding)
    // ------------------------------------------------------------------------

    // Purpose : Function pointer table mirroring the static audio concept.
    // Contract: All function pointers are either null or point to noexcept
    //           functions. userData is owned by the caller/backend.
    // Notes   : SetMasterVolume is expressed via SetBusVolume(AudioBus::Master).
    struct AudioVTable
    {
        using GetCapsFunc     = AudioCaps(*)(const void* userData) noexcept;
        using PlayFunc        = VoiceHandle(*)(void* userData, SoundHandle, const PlayParams&) noexcept;
        using StopFunc        = void(*)(void* userData, VoiceHandle) noexcept;
        using SetBusVolumeFunc = void(*)(void* userData, AudioBus, float) noexcept;

        GetCapsFunc      getCaps      = nullptr;
        PlayFunc         play         = nullptr;
        StopFunc         stop         = nullptr;
        SetBusVolumeFunc setBusVolume = nullptr;
    };

    // Purpose : Non-owning binding to a concrete backend instance.
    // Contract: userData must remain valid for the lifetime of the interface;
    //           no ownership transfer occurs.
    // Notes   : Interface is trivially copyable to ease storage in systems.
    struct AudioInterface
    {
        AudioVTable vtable{};
        void*       userData = nullptr; // Non-owning backend instance pointer.
    };

    static_assert(std::is_trivially_copyable_v<AudioInterface>);

    // ------------------------------------------------------------------------
    // Runtime wrappers (dynamic dispatch helpers)
    // ------------------------------------------------------------------------

    namespace detail
    {
        [[nodiscard]] inline bool IsValidGain(float value) noexcept
        {
            return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
        }

        [[nodiscard]] inline float ClampGain01(float value) noexcept
        {
            if (!std::isfinite(value))
            {
                return 0.0f;
            }
            return std::clamp(value, 0.0f, 1.0f);
        }

        [[nodiscard]] inline bool IsValidPitch(float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }

        [[nodiscard]] inline bool IsValidBus(AudioBus bus) noexcept
        {
            return static_cast<dng::u32>(bus) < static_cast<dng::u32>(AudioBus::Count);
        }
    } // namespace detail

    // Purpose : Query backend capabilities in a safe, allocation-free manner.
    // Contract: Returns default-initialized caps if interface is invalid;
    //           thread-safety is backend-defined.
    // Notes   : has_buses is promoted to true when the v-table exposes
    //           setBusVolume even if the backend did not set has_buses itself.
    [[nodiscard]] inline AudioCaps QueryCaps(const AudioInterface& iface) noexcept
    {
#if DNG_DEBUG
        DNG_ASSERT(iface.userData != nullptr);
        DNG_ASSERT(iface.vtable.getCaps != nullptr);
#endif
        AudioCaps caps = (iface.vtable.getCaps && iface.userData)
            ? iface.vtable.getCaps(iface.userData)
            : AudioCaps{};

        if (iface.vtable.setBusVolume != nullptr)
        {
            caps.has_buses = true;
        }
        return caps;
    }

    // Purpose : Begin playback of a sound with explicit parameters.
    // Contract: Returns VoiceHandle::Invalid on invalid interface. In debug,
    //           asserts on invalid volume/pitch/bus ranges. No allocations
    //           occur at the contract boundary.
    // Notes   : Backend defines determinism and thread-safety guarantees.
    [[nodiscard]] inline VoiceHandle Play(AudioInterface& iface, SoundHandle sound, const PlayParams& params) noexcept
    {
#if DNG_DEBUG
        DNG_ASSERT(iface.userData != nullptr);
        DNG_ASSERT(iface.vtable.play != nullptr);
        DNG_ASSERT(detail::IsValidGain(params.volume));
        DNG_ASSERT(detail::IsValidPitch(params.pitch));
        DNG_ASSERT(detail::IsValidBus(params.bus));
        DNG_ASSERT(std::isfinite(params.startTimeSeconds) && params.startTimeSeconds >= 0.0f);
#endif
        return (iface.vtable.play && iface.userData)
            ? iface.vtable.play(iface.userData, sound, params)
            : VoiceHandle::Invalid();
    }

    // Purpose : Request stop of a playing voice.
    // Contract: No-op when interface is invalid; backend decides how invalid
    //           handles are handled (should be benign).
    // Notes   : Determinism is backend-defined; contract remains allocation-free.
    inline void Stop(AudioInterface& iface, VoiceHandle voice) noexcept
    {
#if DNG_DEBUG
        DNG_ASSERT(iface.userData != nullptr);
        DNG_ASSERT(iface.vtable.stop != nullptr);
#endif
        if (iface.vtable.stop && iface.userData)
        {
            iface.vtable.stop(iface.userData, voice);
        }
    }

    // Purpose : Set volume for a specific bus using linear gain in [0..1].
    // Contract: Debug asserts on invalid bus/value and null interface; release
    //           clamps via ClampGain01 at the contract boundary (source of
    //           truth) and no-ops when the backend lacks setBusVolume.
    // Notes   : Master volume is expressed by calling this with AudioBus::Master.
    inline void SetBusVolume(AudioInterface& iface, AudioBus bus, float value) noexcept
    {
#if DNG_DEBUG
        DNG_ASSERT(iface.userData != nullptr);
        DNG_ASSERT(detail::IsValidBus(bus));
        DNG_ASSERT(detail::IsValidGain(value));
#endif
        if (!detail::IsValidBus(bus))
        {
            return;
        }
        const float clamped = detail::ClampGain01(value);

        if (iface.vtable.setBusVolume && iface.userData)
        {
            iface.vtable.setBusVolume(iface.userData, bus, clamped);
        }
    }

    // Purpose : Alias for setting master volume via the Master bus.
    // Contract: Same as SetBusVolume; no dedicated backend entry.
    // Notes   : Provided for user ergonomics.
    inline void SetMasterVolume(AudioInterface& iface, float value) noexcept
    {
        SetBusVolume(iface, AudioBus::Master, value);
    }

    // ------------------------------------------------------------------------
    // Static face (concept + adapter to dynamic v-table)
    // ------------------------------------------------------------------------

    template <typename Backend>
    concept AudioBackend = requires(Backend& backend, const Backend& constBackend, SoundHandle sound, PlayParams params, VoiceHandle voice, AudioBus bus, float gain)
    {
        { constBackend.GetCaps() } noexcept -> std::same_as<AudioCaps>;
        { backend.Play(sound, params) } noexcept -> std::same_as<VoiceHandle>;
        { backend.Stop(voice) } noexcept -> std::same_as<void>;
        { backend.SetBusVolume(bus, gain) } noexcept -> std::same_as<void>;
    };

    namespace detail
    {
        template <typename Backend>
        struct AudioInterfaceAdapter
        {
            static AudioCaps GetCaps(const void* userData) noexcept
            {
                return static_cast<const Backend*>(userData)->GetCaps();
            }

            static VoiceHandle Play(void* userData, SoundHandle sound, const PlayParams& params) noexcept
            {
                return static_cast<Backend*>(userData)->Play(sound, params);
            }

            static void Stop(void* userData, VoiceHandle voice) noexcept
            {
                static_cast<Backend*>(userData)->Stop(voice);
            }

            static void SetBusVolume(void* userData, AudioBus bus, float value) noexcept
            {
                static_cast<Backend*>(userData)->SetBusVolume(bus, value);
            }
        };
    } // namespace detail

    // Purpose : Adapt a static AudioBackend into a dynamic AudioInterface.
    // Contract: Backend must satisfy AudioBackend; userData remains owned by
    //           caller; no allocations performed.
    // Notes   : Enables late binding without exposing backend headers.
    template <typename Backend>
    [[nodiscard]] inline AudioInterface MakeAudioInterface(Backend& backend) noexcept
    {
        static_assert(AudioBackend<Backend>, "Backend must satisfy AudioBackend concept.");

        AudioInterface iface{};
        iface.userData           = &backend;
        iface.vtable.getCaps     = &detail::AudioInterfaceAdapter<Backend>::GetCaps;
        iface.vtable.play        = &detail::AudioInterfaceAdapter<Backend>::Play;
        iface.vtable.stop        = &detail::AudioInterfaceAdapter<Backend>::Stop;
        iface.vtable.setBusVolume = &detail::AudioInterfaceAdapter<Backend>::SetBusVolume;
        return iface;
    }

} // namespace dng::audio
