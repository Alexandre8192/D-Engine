#include "Core/Audio/AudioSystem.hpp"

int RunAudioSmoke()
{
    using namespace dng::audio;

    AudioSystemState uninitialized{};
    const AudioCaps uninitCaps = QueryCaps(uninitialized);
    if (uninitCaps.determinism != dng::DeterminismMode::Unknown ||
        uninitCaps.threadSafety != dng::ThreadSafetyMode::Unknown ||
        uninitCaps.stableMixOrder)
    {
        return 1;
    }

    AudioMixParams uninitMix{};
    uninitMix.sampleRate = 48000;
    uninitMix.channelCount = 2;
    if (Mix(uninitialized, uninitMix) != AudioStatus::InvalidArg || uninitMix.writtenSamples != 0)
    {
        return 2;
    }

    NullAudio nullBackendForValidation{};
    AudioInterface brokenInterface = MakeNullAudioInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    AudioSystemState rejected{};
    if (InitAudioSystemWithInterface(rejected, brokenInterface, AudioSystemBackend::External))
    {
        return 3;
    }

    AudioSystemState state{};
    AudioSystemConfig config{};
    if (!InitAudioSystem(state, config))
    {
        return 4;
    }

    const AudioCaps caps = QueryCaps(state);
    if (caps.determinism != dng::DeterminismMode::Replay ||
        caps.threadSafety != dng::ThreadSafetyMode::ExternalSync ||
        !caps.stableMixOrder)
    {
        ShutdownAudioSystem(state);
        return 5;
    }

    float buffer[256]{};
    for (float& sample : buffer)
    {
        sample = 1.0f;
    }

    AudioMixParams mix{};
    mix.outSamples = buffer;
    mix.outputCapacitySamples = 256;
    mix.sampleRate = 48000;
    mix.channelCount = 2;
    mix.requestedFrames = 64;
    mix.frameIndex = 3;
    mix.deltaTimeSec = 0.016f;

    if (Mix(state, mix) != AudioStatus::Ok || mix.writtenSamples != 128)
    {
        ShutdownAudioSystem(state);
        return 6;
    }

    for (dng::u32 i = 0; i < mix.writtenSamples; ++i)
    {
        if (buffer[i] != 0.0f)
        {
            ShutdownAudioSystem(state);
            return 7;
        }
    }

    if (state.nullBackend.lastFrameIndex != 3)
    {
        ShutdownAudioSystem(state);
        return 8;
    }

    ShutdownAudioSystem(state);
    return 0;
}
