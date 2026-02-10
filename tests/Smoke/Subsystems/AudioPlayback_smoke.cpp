#include "Core/Audio/AudioSystem.hpp"

#include <cstdio>
#include <cstring>

namespace
{
    [[nodiscard]] bool WriteLe16(std::FILE* file, dng::u16 value) noexcept
    {
        const dng::u8 bytes[2] = {
            static_cast<dng::u8>(value & 0xFFu),
            static_cast<dng::u8>((value >> 8u) & 0xFFu)
        };
        return std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    }

    [[nodiscard]] bool WriteLe32(std::FILE* file, dng::u32 value) noexcept
    {
        const dng::u8 bytes[4] = {
            static_cast<dng::u8>(value & 0xFFu),
            static_cast<dng::u8>((value >> 8u) & 0xFFu),
            static_cast<dng::u8>((value >> 16u) & 0xFFu),
            static_cast<dng::u8>((value >> 24u) & 0xFFu)
        };
        return std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    }

    [[nodiscard]] bool WritePcm16WavForSmoke(const char* path) noexcept
    {
        if (path == nullptr)
        {
            return false;
        }

        std::FILE* file = nullptr;
#if defined(_MSC_VER)
        if (fopen_s(&file, path, "wb") != 0)
        {
            file = nullptr;
        }
#else
        file = std::fopen(path, "wb");
#endif
        if (file == nullptr)
        {
            return false;
        }

        constexpr dng::u32 kSampleRate = 48000;
        constexpr dng::u16 kChannels = 2;
        constexpr dng::u16 kBitsPerSample = 16;
        constexpr dng::u32 kFrameCount = 16;
        constexpr dng::u32 kDataBytes = kFrameCount * static_cast<dng::u32>(kChannels) * sizeof(dng::i16);
        constexpr dng::u32 kFmtChunkBytes = 16;
        constexpr dng::u32 kRiffSize = 4 + 8 + kFmtChunkBytes + 8 + kDataBytes;

        bool ok = true;
        ok = ok && (std::fwrite("RIFF", 1, 4, file) == 4);
        ok = ok && WriteLe32(file, kRiffSize);
        ok = ok && (std::fwrite("WAVE", 1, 4, file) == 4);

        ok = ok && (std::fwrite("fmt ", 1, 4, file) == 4);
        ok = ok && WriteLe32(file, kFmtChunkBytes);
        ok = ok && WriteLe16(file, 1u); // PCM
        ok = ok && WriteLe16(file, kChannels);
        ok = ok && WriteLe32(file, kSampleRate);
        ok = ok && WriteLe32(file, kSampleRate * static_cast<dng::u32>(kChannels) * sizeof(dng::i16));
        ok = ok && WriteLe16(file, static_cast<dng::u16>(kChannels * sizeof(dng::i16)));
        ok = ok && WriteLe16(file, kBitsPerSample);

        ok = ok && (std::fwrite("data", 1, 4, file) == 4);
        ok = ok && WriteLe32(file, kDataBytes);

        for (dng::u32 frame = 0; ok && frame < kFrameCount; ++frame)
        {
            const dng::i16 left = (frame < (kFrameCount / 2u)) ? 12000 : -12000;
            const dng::i16 right = static_cast<dng::i16>(-left);
            ok = ok && (std::fwrite(&left, sizeof(left), 1, file) == 1);
            ok = ok && (std::fwrite(&right, sizeof(right), 1, file) == 1);
        }

        std::fclose(file);
        return ok;
    }

    [[nodiscard]] bool HasNonZero(const float* samples, dng::u32 sampleCount) noexcept
    {
        if (samples == nullptr)
        {
            return false;
        }

        constexpr float kEpsilon = 0.00001f;
        for (dng::u32 i = 0; i < sampleCount; ++i)
        {
            if (samples[i] > kEpsilon || samples[i] < -kEpsilon)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] dng::u64 HashSamples(const float* samples, dng::u32 sampleCount) noexcept
    {
        constexpr dng::u64 kFnvOffset = 1469598103934665603ull;
        constexpr dng::u64 kFnvPrime = 1099511628211ull;

        dng::u64 hash = kFnvOffset;
        for (dng::u32 i = 0; i < sampleCount; ++i)
        {
            dng::u32 bits = 0;
            std::memcpy(&bits, &samples[i], sizeof(bits));
            hash ^= static_cast<dng::u64>(bits);
            hash *= kFnvPrime;
        }
        return hash;
    }
}

int RunAudioPlaybackSmoke()
{
    using namespace dng::audio;

    constexpr const char* kTestWavPath = "AudioPlayback_test.wav";
    if (!WritePcm16WavForSmoke(kTestWavPath))
    {
        return 1;
    }

    AudioSystemState state{};
    struct SmokeCleanup
    {
        AudioSystemState* state = nullptr;
        const char* wavPath = nullptr;
        ~SmokeCleanup() noexcept
        {
            if (state != nullptr)
            {
                ShutdownAudioSystem(*state);
            }
            if (wavPath != nullptr)
            {
                (void)std::remove(wavPath);
            }
        }
    } cleanup{&state, kTestWavPath};
    (void)cleanup;

    AudioSystemConfig config{};
    config.backend = AudioSystemBackend::Platform;
    config.fallbackToNullOnInitFailure = true;

    if (!InitAudioSystem(state, config))
    {
        return 2;
    }

    if (state.backend != AudioSystemBackend::Platform)
    {
        // Platform backend unavailable on this host; fallback path is covered elsewhere.
        return 0;
    }

    AudioClipId clip{};
    if (LoadWavPcm16Clip(state, kTestWavPath, clip) != AudioStatus::Ok || !IsValid(clip))
    {
        return 3;
    }

    const dng::u32 clipPoolCapacity = GetClipPoolCapacitySamples(state);
    const dng::u32 clipPoolUsageAfterLoad = GetClipPoolUsageSamples(state);
    if (clipPoolCapacity == 0 ||
        clipPoolUsageAfterLoad == 0 ||
        clipPoolUsageAfterLoad > clipPoolCapacity ||
        GetLoadedClipCount(state) != 1)
    {
        return 20;
    }

    {
        AudioPlayParams oneShot{};
        oneShot.clip = clip;
        oneShot.gain = 1.0f;
        oneShot.pitch = 1.0f;
        oneShot.loop = false;

        AudioVoiceId voice{};
        if (Play(state, oneShot, voice) != AudioStatus::Ok)
        {
            return 4;
        }

        float out[512]{};
        AudioMixParams mix{};
        mix.outSamples = out;
        mix.outputCapacitySamples = 512;
        mix.sampleRate = config.platform.sampleRate;
        mix.channelCount = config.platform.channelCount;
        mix.requestedFrames = 64;

        if (Mix(state, mix) != AudioStatus::Ok || mix.writtenSamples != 128)
        {
            return 5;
        }

        if (!HasNonZero(out, 32))
        {
            return 6;
        }

        if (HasNonZero(&out[64], 64))
        {
            return 7;
        }
    }

    {
        AudioPlayParams looping{};
        looping.clip = clip;
        looping.gain = 0.5f;
        looping.pitch = 1.0f;
        looping.loop = true;

        AudioVoiceId voice{};
        if (Play(state, looping, voice) != AudioStatus::Ok)
        {
            return 8;
        }

        float out[512]{};
        AudioMixParams mix{};
        mix.outSamples = out;
        mix.outputCapacitySamples = 512;
        mix.sampleRate = config.platform.sampleRate;
        mix.channelCount = config.platform.channelCount;
        mix.requestedFrames = 64;

        if (Mix(state, mix) != AudioStatus::Ok || mix.writtenSamples != 128)
        {
            return 9;
        }

        if (!HasNonZero(&out[96], 32))
        {
            return 10;
        }

        if (Stop(state, voice) != AudioStatus::Ok)
        {
            return 11;
        }

        float afterStop[256]{};
        AudioMixParams stopMix{};
        stopMix.outSamples = afterStop;
        stopMix.outputCapacitySamples = 256;
        stopMix.sampleRate = config.platform.sampleRate;
        stopMix.channelCount = config.platform.channelCount;
        stopMix.requestedFrames = 32;
        if (Mix(state, stopMix) != AudioStatus::Ok)
        {
            return 12;
        }

        if (HasNonZero(afterStop, stopMix.writtenSamples))
        {
            return 13;
        }
    }

    auto runDeterministicPass = [&](dng::u64& outHash) -> bool
    {
        AudioPlayParams voiceA{};
        voiceA.clip = clip;
        voiceA.gain = 0.25f;
        voiceA.pitch = 1.0f;
        voiceA.loop = true;

        AudioPlayParams voiceB = voiceA;
        voiceB.gain = 0.75f;

        AudioVoiceId a{};
        AudioVoiceId b{};
        if (Play(state, voiceA, a) != AudioStatus::Ok)
        {
            return false;
        }
        if (Play(state, voiceB, b) != AudioStatus::Ok)
        {
            return false;
        }
        if (SetGain(state, a, 0.5f) != AudioStatus::Ok)
        {
            return false;
        }

        float out[256]{};
        AudioMixParams mix{};
        mix.outSamples = out;
        mix.outputCapacitySamples = 256;
        mix.sampleRate = config.platform.sampleRate;
        mix.channelCount = config.platform.channelCount;
        mix.requestedFrames = 32;

        if (Mix(state, mix) != AudioStatus::Ok || mix.writtenSamples != 64)
        {
            return false;
        }

        outHash = HashSamples(out, mix.writtenSamples);

        if (Stop(state, a) != AudioStatus::Ok || Stop(state, b) != AudioStatus::Ok)
        {
            return false;
        }

        float flush[128]{};
        AudioMixParams flushMix{};
        flushMix.outSamples = flush;
        flushMix.outputCapacitySamples = 128;
        flushMix.sampleRate = config.platform.sampleRate;
        flushMix.channelCount = config.platform.channelCount;
        flushMix.requestedFrames = 16;
        if (Mix(state, flushMix) != AudioStatus::Ok)
        {
            return false;
        }

        return true;
    };

    dng::u64 hashA = 0;
    dng::u64 hashB = 0;
    if (!runDeterministicPass(hashA) || !runDeterministicPass(hashB))
    {
        return 14;
    }

    if (hashA != hashB)
    {
        return 15;
    }

    {
        const dng::u64 underrunBefore = GetUnderrunCount(state);
        const dng::u64 submitBefore = GetSubmitErrorCount(state);

        AudioPlayParams stressPlay{};
        stressPlay.clip = clip;
        stressPlay.gain = 0.5f;
        stressPlay.pitch = 1.0f;
        stressPlay.loop = true;

        AudioVoiceId stressVoice{};
        if (Play(state, stressPlay, stressVoice) != AudioStatus::Ok)
        {
            return 16;
        }

        float out[4096]{};
        AudioMixParams mix{};
        mix.outSamples = out;
        mix.outputCapacitySamples = 4096;
        mix.sampleRate = config.platform.sampleRate;
        mix.channelCount = config.platform.channelCount;
        mix.requestedFrames = config.platform.framesPerBuffer;

        for (dng::u32 i = 0; i < 32; ++i)
        {
            if (Mix(state, mix) != AudioStatus::Ok)
            {
                return 17;
            }
        }

        (void)Stop(state, stressVoice);
        (void)Mix(state, mix);

        const dng::u64 underrunAfter = GetUnderrunCount(state);
        const dng::u64 submitAfter = GetSubmitErrorCount(state);

        if (underrunAfter < underrunBefore || submitAfter < submitBefore)
        {
            return 18;
        }

        if (underrunAfter == underrunBefore && submitAfter == submitBefore)
        {
            return 19;
        }
    }

    if (UnloadClip(state, clip) != AudioStatus::Ok)
    {
        return 21;
    }

    if (GetLoadedClipCount(state) != 0 || GetClipPoolUsageSamples(state) != 0)
    {
        return 22;
    }

    AudioPlayParams afterUnload{};
    afterUnload.clip = clip;
    afterUnload.gain = 1.0f;
    afterUnload.pitch = 1.0f;
    AudioVoiceId staleVoice{};
    if (Play(state, afterUnload, staleVoice) != AudioStatus::InvalidArg)
    {
        return 23;
    }

    if (UnloadClip(state, clip) != AudioStatus::InvalidArg)
    {
        return 24;
    }

    AudioClipId clipReloaded{};
    if (LoadWavPcm16Clip(state, kTestWavPath, clipReloaded) != AudioStatus::Ok || !IsValid(clipReloaded))
    {
        return 25;
    }

    if (GetLoadedClipCount(state) != 1 || GetClipPoolUsageSamples(state) == 0)
    {
        return 26;
    }

    if (UnloadClip(state, clipReloaded) != AudioStatus::Ok ||
        GetLoadedClipCount(state) != 0 ||
        GetClipPoolUsageSamples(state) != 0)
    {
        return 27;
    }

    return 0;
}
