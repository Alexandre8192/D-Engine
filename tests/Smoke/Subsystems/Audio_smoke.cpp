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

    if (GetLoadedClipCount(state) != 0 ||
        GetClipPoolUsageSamples(state) != 0 ||
        GetClipPoolCapacitySamples(state) != 0)
    {
        ShutdownAudioSystem(state);
        return 23;
    }

    if (UnloadClip(state, MakeAudioClipId(1)) != AudioStatus::NotSupported)
    {
        ShutdownAudioSystem(state);
        return 24;
    }

    AudioPlayParams play{};
    play.clip = MakeAudioClipId(7);
    play.gain = 0.75f;
    play.pitch = 1.0f;
    play.loop = true;

    AudioVoiceId voice{};
    if (Play(state, play, voice) != AudioStatus::Ok || !IsValid(voice))
    {
        ShutdownAudioSystem(state);
        return 16;
    }

    if (!IsVoiceActive(state, voice) ||
        GetActiveVoiceCount(state) != 1 ||
        GetPendingCommandCount(state) != 1)
    {
        ShutdownAudioSystem(state);
        return 17;
    }

    if (SetGain(state, voice, 0.5f) != AudioStatus::Ok || GetPendingCommandCount(state) != 2)
    {
        ShutdownAudioSystem(state);
        return 18;
    }

    if (Stop(state, voice) != AudioStatus::Ok ||
        GetActiveVoiceCount(state) != 0 ||
        GetPendingCommandCount(state) != 3)
    {
        ShutdownAudioSystem(state);
        return 19;
    }

    if (SetGain(state, voice, 0.25f) != AudioStatus::InvalidArg)
    {
        ShutdownAudioSystem(state);
        return 20;
    }

    AudioVoiceId invalidVoice{};
    AudioPlayParams invalidPlay{};
    if (Play(state, invalidPlay, invalidVoice) != AudioStatus::InvalidArg)
    {
        ShutdownAudioSystem(state);
        return 21;
    }

    {
        // Saturate the command queue with SetGain commands on one active voice.
        AudioPlayParams queuePlay{};
        queuePlay.clip = MakeAudioClipId(9);
        queuePlay.gain = 1.0f;
        queuePlay.pitch = 1.0f;
        queuePlay.loop = true;

        AudioVoiceId queueVoice{};
        if (Play(state, queuePlay, queueVoice) != AudioStatus::Ok)
        {
            ShutdownAudioSystem(state);
            return 25;
        }

        float flushBuffer[4]{};
        AudioMixParams flushMix{};
        flushMix.outSamples = flushBuffer;
        flushMix.outputCapacitySamples = 4;
        flushMix.sampleRate = 48000;
        flushMix.channelCount = 2;
        flushMix.requestedFrames = 1;
        if (Mix(state, flushMix) != AudioStatus::Ok)
        {
            ShutdownAudioSystem(state);
            return 26;
        }

        for (dng::u32 i = 0; i < kAudioSystemMaxCommands; ++i)
        {
            const float gain = ((i & 1u) == 0u) ? 0.25f : 0.75f;
            if (SetGain(state, queueVoice, gain) != AudioStatus::Ok)
            {
                ShutdownAudioSystem(state);
                return 27;
            }
        }

        if (SetGain(state, queueVoice, 0.5f) != AudioStatus::NotSupported)
        {
            ShutdownAudioSystem(state);
            return 28;
        }

        if (GetPendingCommandCount(state) != kAudioSystemMaxCommands)
        {
            ShutdownAudioSystem(state);
            return 29;
        }

        if (Mix(state, flushMix) != AudioStatus::Ok || GetPendingCommandCount(state) != 0)
        {
            ShutdownAudioSystem(state);
            return 30;
        }

        if (Stop(state, queueVoice) != AudioStatus::Ok || Mix(state, flushMix) != AudioStatus::Ok)
        {
            ShutdownAudioSystem(state);
            return 31;
        }
    }

    {
        AudioPlayParams controlPlay{};
        controlPlay.clip = MakeAudioClipId(11);
        controlPlay.gain = 1.0f;
        controlPlay.pitch = 1.0f;
        controlPlay.bus = AudioBus::Music;
        controlPlay.loop = true;

        AudioVoiceId controlVoice{};
        if (Play(state, controlPlay, controlVoice) != AudioStatus::Ok)
        {
            ShutdownAudioSystem(state);
            return 34;
        }

        if (Pause(state, controlVoice) != AudioStatus::Ok ||
            Seek(state, controlVoice, 7u) != AudioStatus::Ok ||
            Resume(state, controlVoice) != AudioStatus::Ok)
        {
            ShutdownAudioSystem(state);
            return 35;
        }

        if (SetBusGain(state, AudioBus::Music, 0.35f) != AudioStatus::Ok ||
            SetMasterGain(state, 0.85f) != AudioStatus::Ok)
        {
            ShutdownAudioSystem(state);
            return 36;
        }

        if (GetBusGain(state, AudioBus::Music) != 0.35f || GetBusGain(state, AudioBus::Master) != 0.85f)
        {
            ShutdownAudioSystem(state);
            return 37;
        }

        float flushBuffer[4]{};
        AudioMixParams flushMix{};
        flushMix.outSamples = flushBuffer;
        flushMix.outputCapacitySamples = 4;
        flushMix.sampleRate = 48000;
        flushMix.channelCount = 2;
        flushMix.requestedFrames = 1;
        if (Mix(state, flushMix) != AudioStatus::Ok || GetPendingCommandCount(state) != 0)
        {
            ShutdownAudioSystem(state);
            return 38;
        }

        if (SetBusGain(state, static_cast<AudioBus>(99), 1.0f) != AudioStatus::InvalidArg)
        {
            ShutdownAudioSystem(state);
            return 39;
        }

        if (Stop(state, controlVoice) != AudioStatus::Ok || Mix(state, flushMix) != AudioStatus::Ok)
        {
            ShutdownAudioSystem(state);
            return 40;
        }

        if (SetMasterGain(state, 1.0f) != AudioStatus::Ok || Mix(state, flushMix) != AudioStatus::Ok)
        {
            ShutdownAudioSystem(state);
            return 41;
        }
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

    if (GetPendingCommandCount(state) != 0)
    {
        ShutdownAudioSystem(state);
        return 22;
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

    AudioSystemConfig platformFallbackConfig{};
    platformFallbackConfig.backend = AudioSystemBackend::Platform;
    platformFallbackConfig.platform.sampleRate = 0; // Force backend init failure.
    platformFallbackConfig.fallbackToNullOnInitFailure = true;

    AudioSystemState fallbackState{};
    if (!InitAudioSystem(fallbackState, platformFallbackConfig))
    {
        return 9;
    }

    if (fallbackState.backend != AudioSystemBackend::Null)
    {
        ShutdownAudioSystem(fallbackState);
        return 10;
    }

    ShutdownAudioSystem(fallbackState);

    AudioSystemConfig platformStrictConfig{};
    platformStrictConfig.backend = AudioSystemBackend::Platform;
    platformStrictConfig.platform.sampleRate = 0; // Force backend init failure.
    platformStrictConfig.fallbackToNullOnInitFailure = false;

    AudioSystemState strictState{};
    if (InitAudioSystem(strictState, platformStrictConfig))
    {
        ShutdownAudioSystem(strictState);
        return 11;
    }

    if (strictState.isInitialized)
    {
        ShutdownAudioSystem(strictState);
        return 12;
    }

    AudioSystemConfig platformAutoConfig{};
    platformAutoConfig.backend = AudioSystemBackend::Platform;
    platformAutoConfig.fallbackToNullOnInitFailure = true;

    AudioSystemState platformAutoState{};
    if (!InitAudioSystem(platformAutoState, platformAutoConfig))
    {
        return 13;
    }

    if (platformAutoState.backend == AudioSystemBackend::Platform)
    {
        float platformBuffer[256]{};
        AudioMixParams platformMix{};
        platformMix.outSamples = platformBuffer;
        platformMix.outputCapacitySamples = 256;
        platformMix.sampleRate = platformAutoConfig.platform.sampleRate;
        platformMix.channelCount = platformAutoConfig.platform.channelCount;
        platformMix.requestedFrames = 64;

        if (Mix(platformAutoState, platformMix) != AudioStatus::Ok)
        {
            ShutdownAudioSystem(platformAutoState);
            return 14;
        }

        if (platformMix.writtenSamples != 128)
        {
            ShutdownAudioSystem(platformAutoState);
            return 15;
        }

        AudioSystemConfig secondPlatformConfig{};
        secondPlatformConfig.backend = AudioSystemBackend::Platform;
        secondPlatformConfig.fallbackToNullOnInitFailure = false;

        AudioSystemState secondPlatformState{};
        if (InitAudioSystem(secondPlatformState, secondPlatformConfig))
        {
            ShutdownAudioSystem(secondPlatformState);
            ShutdownAudioSystem(platformAutoState);
            return 32;
        }

        if (secondPlatformState.isInitialized)
        {
            ShutdownAudioSystem(secondPlatformState);
            ShutdownAudioSystem(platformAutoState);
            return 33;
        }
    }

    ShutdownAudioSystem(platformAutoState);

    return 0;
}
