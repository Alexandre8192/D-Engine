// ============================================================================
// D-Engine - Source/Core/Contracts/Audio.hpp
// ----------------------------------------------------------------------------
// Purpose : Audio contract describing backend-agnostic, frame-based mixing
//           without exposing platform audio APIs or introducing allocations.
// Contract: Header-only, no exceptions/RTTI, engine-absolute includes only.
//           All public data is POD/trivially copyable. Caller owns output
//           buffers; backends only write within declared capacity.
// Notes   : M0+ exposes deterministic pull-mix plus voice controls
//           (Play/Stop/Pause/Resume/Seek/SetGain) and simple bus gains
//           (Master/Music/Sfx) while keeping backend APIs hidden. Real
//           backends can map this to WASAPI/XAudio/etc.
// ============================================================================

#pragma once

#include "Core/Types.hpp"

#include <concepts>
#include <type_traits>

namespace dng::audio
{
    struct AudioClipId
    {
        dng::u32 value = 0;
    };

    struct AudioVoiceId
    {
        dng::u32 slot = 0;
        dng::u32 generation = 0;
    };

    [[nodiscard]] constexpr AudioClipId MakeAudioClipId(dng::u32 value) noexcept
    {
        AudioClipId id{};
        id.value = value;
        return id;
    }

    [[nodiscard]] constexpr bool IsValid(AudioClipId id) noexcept
    {
        return id.value != 0;
    }

    [[nodiscard]] constexpr bool IsValid(AudioVoiceId id) noexcept
    {
        return id.generation != 0;
    }

    enum class AudioStatus : dng::u8
    {
        Ok = 0,
        InvalidArg,
        NotSupported,
        UnknownError
    };

    enum class AudioBus : dng::u8
    {
        Master = 0,
        Music  = 1,
        Sfx    = 2,
        Count  = 3
    };

    [[nodiscard]] constexpr bool IsValid(AudioBus bus) noexcept
    {
        return bus == AudioBus::Master ||
               bus == AudioBus::Music ||
               bus == AudioBus::Sfx;
    }

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

    struct AudioPlayParams
    {
        AudioClipId clip{};
        float       gain = 1.0f;
        float       pitch = 1.0f;
        AudioBus    bus = AudioBus::Master;
        bool        loop = false;
        dng::u8     reserved[2]{};
    };

    static_assert(std::is_trivially_copyable_v<AudioClipId>);
    static_assert(std::is_trivially_copyable_v<AudioVoiceId>);
    static_assert(std::is_trivially_copyable_v<AudioCaps>);
    static_assert(std::is_trivially_copyable_v<AudioMixParams>);
    static_assert(std::is_trivially_copyable_v<AudioPlayParams>);

    struct AudioVTable
    {
        using PlayFunc    = AudioStatus(*)(void* userData, AudioVoiceId voice, const AudioPlayParams& params) noexcept;
        using StopFunc    = AudioStatus(*)(void* userData, AudioVoiceId voice) noexcept;
        using PauseFunc   = AudioStatus(*)(void* userData, AudioVoiceId voice) noexcept;
        using ResumeFunc  = AudioStatus(*)(void* userData, AudioVoiceId voice) noexcept;
        using SeekFunc    = AudioStatus(*)(void* userData, AudioVoiceId voice, dng::u32 frameIndex) noexcept;
        using SetGainFunc = AudioStatus(*)(void* userData, AudioVoiceId voice, float gain) noexcept;
        using SetBusGainFunc = AudioStatus(*)(void* userData, AudioBus bus, float gain) noexcept;
        using GetCapsFunc = AudioCaps(*)(const void* userData) noexcept;
        using MixFunc     = AudioStatus(*)(void* userData, AudioMixParams& params) noexcept;

        PlayFunc    play    = nullptr;
        StopFunc    stop    = nullptr;
        PauseFunc   pause   = nullptr;
        ResumeFunc  resume  = nullptr;
        SeekFunc    seek    = nullptr;
        SetGainFunc setGain = nullptr;
        SetBusGainFunc setBusGain = nullptr;
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

    [[nodiscard]] inline AudioStatus Play(AudioInterface& iface,
                                          AudioVoiceId voice,
                                          const AudioPlayParams& params) noexcept
    {
        return (iface.vtable.play && iface.userData)
            ? iface.vtable.play(iface.userData, voice, params)
            : AudioStatus::InvalidArg;
    }

    [[nodiscard]] inline AudioStatus Stop(AudioInterface& iface, AudioVoiceId voice) noexcept
    {
        return (iface.vtable.stop && iface.userData)
            ? iface.vtable.stop(iface.userData, voice)
            : AudioStatus::InvalidArg;
    }

    [[nodiscard]] inline AudioStatus SetGain(AudioInterface& iface,
                                             AudioVoiceId voice,
                                             float gain) noexcept
    {
        return (iface.vtable.setGain && iface.userData)
            ? iface.vtable.setGain(iface.userData, voice, gain)
            : AudioStatus::InvalidArg;
    }

    [[nodiscard]] inline AudioStatus Pause(AudioInterface& iface, AudioVoiceId voice) noexcept
    {
        return (iface.vtable.pause && iface.userData)
            ? iface.vtable.pause(iface.userData, voice)
            : AudioStatus::InvalidArg;
    }

    [[nodiscard]] inline AudioStatus Resume(AudioInterface& iface, AudioVoiceId voice) noexcept
    {
        return (iface.vtable.resume && iface.userData)
            ? iface.vtable.resume(iface.userData, voice)
            : AudioStatus::InvalidArg;
    }

    [[nodiscard]] inline AudioStatus Seek(AudioInterface& iface,
                                          AudioVoiceId voice,
                                          dng::u32 frameIndex) noexcept
    {
        return (iface.vtable.seek && iface.userData)
            ? iface.vtable.seek(iface.userData, voice, frameIndex)
            : AudioStatus::InvalidArg;
    }

    [[nodiscard]] inline AudioStatus SetBusGain(AudioInterface& iface,
                                                AudioBus bus,
                                                float gain) noexcept
    {
        return (iface.vtable.setBusGain && iface.userData)
            ? iface.vtable.setBusGain(iface.userData, bus, gain)
            : AudioStatus::InvalidArg;
    }

    template <typename Backend>
    concept AudioBackend = requires(Backend& backend,
                                    const Backend& constBackend,
                                    AudioVoiceId voice,
                                    const AudioPlayParams& playParams,
                                    AudioBus bus,
                                    float gain,
                                    dng::u32 frameIndex,
                                    AudioMixParams& params)
    {
        { constBackend.GetCaps() } noexcept -> std::same_as<AudioCaps>;
        { backend.Play(voice, playParams) } noexcept -> std::same_as<AudioStatus>;
        { backend.Stop(voice) } noexcept -> std::same_as<AudioStatus>;
        { backend.Pause(voice) } noexcept -> std::same_as<AudioStatus>;
        { backend.Resume(voice) } noexcept -> std::same_as<AudioStatus>;
        { backend.Seek(voice, frameIndex) } noexcept -> std::same_as<AudioStatus>;
        { backend.SetGain(voice, gain) } noexcept -> std::same_as<AudioStatus>;
        { backend.SetBusGain(bus, gain) } noexcept -> std::same_as<AudioStatus>;
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

            static AudioStatus Play(void* userData, AudioVoiceId voice, const AudioPlayParams& params) noexcept
            {
                return static_cast<Backend*>(userData)->Play(voice, params);
            }

            static AudioStatus Stop(void* userData, AudioVoiceId voice) noexcept
            {
                return static_cast<Backend*>(userData)->Stop(voice);
            }

            static AudioStatus Pause(void* userData, AudioVoiceId voice) noexcept
            {
                return static_cast<Backend*>(userData)->Pause(voice);
            }

            static AudioStatus Resume(void* userData, AudioVoiceId voice) noexcept
            {
                return static_cast<Backend*>(userData)->Resume(voice);
            }

            static AudioStatus Seek(void* userData, AudioVoiceId voice, dng::u32 frameIndex) noexcept
            {
                return static_cast<Backend*>(userData)->Seek(voice, frameIndex);
            }

            static AudioStatus SetGain(void* userData, AudioVoiceId voice, float gain) noexcept
            {
                return static_cast<Backend*>(userData)->SetGain(voice, gain);
            }

            static AudioStatus SetBusGain(void* userData, AudioBus bus, float gain) noexcept
            {
                return static_cast<Backend*>(userData)->SetBusGain(bus, gain);
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
        iface.vtable.play    = &detail::AudioInterfaceAdapter<Backend>::Play;
        iface.vtable.stop    = &detail::AudioInterfaceAdapter<Backend>::Stop;
        iface.vtable.pause   = &detail::AudioInterfaceAdapter<Backend>::Pause;
        iface.vtable.resume  = &detail::AudioInterfaceAdapter<Backend>::Resume;
        iface.vtable.seek    = &detail::AudioInterfaceAdapter<Backend>::Seek;
        iface.vtable.setGain = &detail::AudioInterfaceAdapter<Backend>::SetGain;
        iface.vtable.setBusGain = &detail::AudioInterfaceAdapter<Backend>::SetBusGain;
        iface.vtable.getCaps = &detail::AudioInterfaceAdapter<Backend>::GetCaps;
        iface.vtable.mix     = &detail::AudioInterfaceAdapter<Backend>::Mix;
        return iface;
    }

} // namespace dng::audio
