// ============================================================================
// D-Engine - Source/Core/Contracts/Audio.hpp
// ----------------------------------------------------------------------------
// Purpose : Audio contract describing backend-agnostic, frame-based mixing
//           without exposing platform audio APIs or introducing allocations.
// Contract: Header-only, no exceptions/RTTI, engine-absolute includes only.
//           All public data is POD/trivially copyable. Caller owns output
//           buffers; backends only write within declared capacity.
// Notes   : M0 focuses on deterministic, pull-based mixing (caller requests
//           samples each frame). Real backends can map this to WASAPI/XAudio/etc.
// ============================================================================

#pragma once

#include "Core/Types.hpp"

#include <concepts>
#include <type_traits>

namespace dng::audio
{
    enum class AudioStatus : dng::u8
    {
        Ok = 0,
        InvalidArg,
        NotSupported,
        UnknownError
    };

    struct AudioCaps
    {
        dng::DeterminismMode determinism = dng::DeterminismMode::Unknown;
        dng::ThreadSafetyMode threadSafety = dng::ThreadSafetyMode::Unknown;
        bool stableMixOrder = false;
    };

    struct AudioMixParams
    {
        float*   outSamples = nullptr;      // Interleaved float output buffer (caller-owned).
        dng::u32 outputCapacitySamples = 0; // Total writable float samples in outSamples.
        dng::u32 sampleRate = 48000;        // Requested output sample rate.
        dng::u16 channelCount = 2;          // Output channels (interleaved).
        dng::u16 reserved = 0;
        dng::u32 requestedFrames = 0;       // Requested output frame count.
        dng::u64 frameIndex = 0;            // Runtime frame index for determinism/replay.
        float    deltaTimeSec = 0.0f;       // Runtime delta time of the owning frame.
        dng::u32 writtenSamples = 0;        // Out: number of float samples produced.
    };

    static_assert(std::is_trivially_copyable_v<AudioCaps>);
    static_assert(std::is_trivially_copyable_v<AudioMixParams>);

    struct AudioVTable
    {
        using GetCapsFunc = AudioCaps(*)(const void* userData) noexcept;
        using MixFunc     = AudioStatus(*)(void* userData, AudioMixParams& params) noexcept;

        GetCapsFunc getCaps = nullptr;
        MixFunc     mix     = nullptr;
    };

    struct AudioInterface
    {
        AudioVTable vtable{};
        void*       userData = nullptr; // Non-owning backend instance pointer.
    };

    [[nodiscard]] inline AudioCaps QueryCaps(const AudioInterface& iface) noexcept
    {
        return (iface.vtable.getCaps && iface.userData)
            ? iface.vtable.getCaps(iface.userData)
            : AudioCaps{};
    }

    [[nodiscard]] inline AudioStatus Mix(AudioInterface& iface, AudioMixParams& params) noexcept
    {
        params.writtenSamples = 0;
        return (iface.vtable.mix && iface.userData)
            ? iface.vtable.mix(iface.userData, params)
            : AudioStatus::InvalidArg;
    }

    template <typename Backend>
    concept AudioBackend = requires(Backend& backend,
                                    const Backend& constBackend,
                                    AudioMixParams& params)
    {
        { constBackend.GetCaps() } noexcept -> std::same_as<AudioCaps>;
        { backend.Mix(params) } noexcept -> std::same_as<AudioStatus>;
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

            static AudioStatus Mix(void* userData, AudioMixParams& params) noexcept
            {
                return static_cast<Backend*>(userData)->Mix(params);
            }
        };
    } // namespace detail

    template <typename Backend>
    [[nodiscard]] inline AudioInterface MakeAudioInterface(Backend& backend) noexcept
    {
        static_assert(AudioBackend<Backend>, "Backend must satisfy AudioBackend concept.");

        AudioInterface iface{};
        iface.userData       = &backend;
        iface.vtable.getCaps = &detail::AudioInterfaceAdapter<Backend>::GetCaps;
        iface.vtable.mix     = &detail::AudioInterfaceAdapter<Backend>::Mix;
        return iface;
    }

} // namespace dng::audio
