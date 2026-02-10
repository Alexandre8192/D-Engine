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
        (void)Mix(state, params);
        ShutdownAudioSystem(state);
        return 0;
    }
}
