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

        [[nodiscard]] AudioStatus SetGain(AudioVoiceId voice, float gain) noexcept
        {
            (void)voice;
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
        (void)SetGain(state, voice, 0.5f);
        (void)Stop(state, voice);
        (void)Mix(state, params);
        ShutdownAudioSystem(state);
        return 0;
    }
}
