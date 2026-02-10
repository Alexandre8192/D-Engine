#include "Core/Audio/AudioSystem.hpp"

namespace
{
    using namespace dng::audio;

    struct DummyAudio
    {
        [[nodiscard]] constexpr AudioCaps GetCaps() const noexcept
        {
            AudioCaps caps{};
            caps.determinism = dng::DeterminismMode::Replay;
            caps.threadSafety = dng::ThreadSafetyMode::ExternalSync;
            caps.stableMixOrder = true;
            return caps;
        }

        [[nodiscard]] AudioStatus Play(AudioVoiceId voice, const AudioPlayParams& params) noexcept
        {
            (void)voice;
            (void)params;
            return AudioStatus::Ok;
        }

        [[nodiscard]] AudioStatus Stop(AudioVoiceId voice) noexcept
        {
            (void)voice;
            return AudioStatus::Ok;
        }

        [[nodiscard]] AudioStatus Pause(AudioVoiceId voice) noexcept
        {
            (void)voice;
            return AudioStatus::Ok;
        }

        [[nodiscard]] AudioStatus Resume(AudioVoiceId voice) noexcept
        {
            (void)voice;
            return AudioStatus::Ok;
        }

        [[nodiscard]] AudioStatus Seek(AudioVoiceId voice, dng::u32 frameIndex) noexcept
        {
            (void)voice;
            (void)frameIndex;
            return AudioStatus::Ok;
        }

        [[nodiscard]] AudioStatus SetGain(AudioVoiceId voice, float gain) noexcept
        {
            (void)voice;
            (void)gain;
            return AudioStatus::Ok;
        }

        [[nodiscard]] AudioStatus SetBusGain(AudioBus bus, float gain) noexcept
        {
            (void)bus;
            (void)gain;
            return AudioStatus::Ok;
        }

        [[nodiscard]] AudioStatus Mix(AudioMixParams& params) noexcept
        {
            params.writtenSamples = 0;
            return AudioStatus::Ok;
        }
    };

    int AudioHeaderOnly()
    {
        DummyAudio backend{};
        AudioInterface iface = MakeAudioInterface(backend);
        AudioSystemState state{};

        (void)InitAudioSystemWithInterface(state, iface, AudioSystemBackend::External);
        AudioMixParams params{};
        params.sampleRate = 48000;
        params.channelCount = 2;
        params.requestedFrames = 0;
        AudioPlayParams play{};
        play.clip = MakeAudioClipId(1);
        AudioVoiceId voice{};
        (void)Play(state, play, voice);
        (void)Pause(state, voice);
        (void)Seek(state, voice, 0);
        (void)Resume(state, voice);
        (void)SetGain(state, voice, 0.5f);
        (void)SetBusGain(state, AudioBus::Music, 0.75f);
        (void)SetMasterGain(state, 1.0f);
        (void)Stop(state, voice);
        (void)Mix(state, params);
        ShutdownAudioSystem(state);
        return 0;
    }
}
