#include "Core/Audio/AudioSystem.hpp"
#include "Core/Contracts/FileSystem.hpp"

#include <cstdio>
#include <cstring>
#include <limits>

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

    [[nodiscard]] bool WritePcm16WavForSmoke(const char* path,
                                             dng::u32 sampleRate,
                                             dng::u32 frameCount) noexcept
    {
        if (path == nullptr || sampleRate == 0 || frameCount == 0)
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

        constexpr dng::u16 kChannels = 2;
        constexpr dng::u16 kBitsPerSample = 16;
        const dng::u32 kDataBytes = frameCount * static_cast<dng::u32>(kChannels) * sizeof(dng::i16);
        constexpr dng::u32 kFmtChunkBytes = 16;
        const dng::u32 kRiffSize = 4 + 8 + kFmtChunkBytes + 8 + kDataBytes;

        bool ok = true;
        ok = ok && (std::fwrite("RIFF", 1, 4, file) == 4);
        ok = ok && WriteLe32(file, kRiffSize);
        ok = ok && (std::fwrite("WAVE", 1, 4, file) == 4);

        ok = ok && (std::fwrite("fmt ", 1, 4, file) == 4);
        ok = ok && WriteLe32(file, kFmtChunkBytes);
        ok = ok && WriteLe16(file, 1u); // PCM
        ok = ok && WriteLe16(file, kChannels);
        ok = ok && WriteLe32(file, sampleRate);
        ok = ok && WriteLe32(file, sampleRate * static_cast<dng::u32>(kChannels) * sizeof(dng::i16));
        ok = ok && WriteLe16(file, static_cast<dng::u16>(kChannels * sizeof(dng::i16)));
        ok = ok && WriteLe16(file, kBitsPerSample);

        ok = ok && (std::fwrite("data", 1, 4, file) == 4);
        ok = ok && WriteLe32(file, kDataBytes);

        for (dng::u32 frame = 0; ok && frame < frameCount; ++frame)
        {
            const dng::i16 left = (frame < (frameCount / 2u)) ? 12000 : -12000;
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

    struct LocalFileSystemForSmoke
    {
        bool readRangeSupported = true;
        dng::u32 failAfterReadRangeCalls = ~dng::u32{0};
        dng::u32 readRangeCallCount = 0;

        [[nodiscard]] dng::fs::FileSystemCaps GetCaps() const noexcept
        {
            dng::fs::FileSystemCaps caps{};
            caps.determinism = dng::DeterminismMode::Off;
            caps.threadSafety = dng::ThreadSafetyMode::ExternalSync;
            caps.stableOrderingRequired = false;
            return caps;
        }

        [[nodiscard]] static bool PathToCString(dng::fs::PathView path,
                                                char* dst,
                                                dng::u32 dstCapacity) noexcept
        {
            if (dst == nullptr || dstCapacity == 0 || path.data == nullptr || path.size == 0)
            {
                return false;
            }

            if (path.size >= dstCapacity)
            {
                return false;
            }

            std::memcpy(dst, path.data, static_cast<size_t>(path.size));
            dst[path.size] = '\0';
            return true;
        }

        [[nodiscard]] dng::fs::FsStatus Exists(dng::fs::PathView path) noexcept
        {
            char cPath[512]{};
            if (!PathToCString(path, cPath, static_cast<dng::u32>(sizeof(cPath))))
            {
                return dng::fs::FsStatus::InvalidArg;
            }

            std::FILE* file = nullptr;
#if defined(_MSC_VER)
            if (fopen_s(&file, cPath, "rb") != 0)
            {
                file = nullptr;
            }
#else
            file = std::fopen(cPath, "rb");
#endif
            if (file == nullptr)
            {
                return dng::fs::FsStatus::NotFound;
            }

            std::fclose(file);
            return dng::fs::FsStatus::Ok;
        }

        [[nodiscard]] dng::fs::FsStatus FileSize(dng::fs::PathView path, dng::u64& outSize) noexcept
        {
            outSize = 0;
            char cPath[512]{};
            if (!PathToCString(path, cPath, static_cast<dng::u32>(sizeof(cPath))))
            {
                return dng::fs::FsStatus::InvalidArg;
            }

            std::FILE* file = nullptr;
#if defined(_MSC_VER)
            if (fopen_s(&file, cPath, "rb") != 0)
            {
                file = nullptr;
            }
#else
            file = std::fopen(cPath, "rb");
#endif
            if (file == nullptr)
            {
                return dng::fs::FsStatus::NotFound;
            }

            if (std::fseek(file, 0, SEEK_END) != 0)
            {
                std::fclose(file);
                return dng::fs::FsStatus::UnknownError;
            }

            const long endPos = std::ftell(file);
            std::fclose(file);
            if (endPos < 0)
            {
                return dng::fs::FsStatus::UnknownError;
            }

            outSize = static_cast<dng::u64>(endPos);
            return dng::fs::FsStatus::Ok;
        }

        [[nodiscard]] dng::fs::FsStatus ReadFile(dng::fs::PathView path,
                                                 void* dst,
                                                 dng::u64 dstSize,
                                                 dng::u64& outRead) noexcept
        {
            outRead = 0;
            if (dst == nullptr)
            {
                return dng::fs::FsStatus::InvalidArg;
            }

            char cPath[512]{};
            if (!PathToCString(path, cPath, static_cast<dng::u32>(sizeof(cPath))))
            {
                return dng::fs::FsStatus::InvalidArg;
            }

            std::FILE* file = nullptr;
#if defined(_MSC_VER)
            if (fopen_s(&file, cPath, "rb") != 0)
            {
                file = nullptr;
            }
#else
            file = std::fopen(cPath, "rb");
#endif
            if (file == nullptr)
            {
                return dng::fs::FsStatus::NotFound;
            }

            if (dstSize > static_cast<dng::u64>(~size_t{0}))
            {
                std::fclose(file);
                return dng::fs::FsStatus::InvalidArg;
            }

            const size_t toRead = static_cast<size_t>(dstSize);
            const size_t readCount = std::fread(dst, 1, toRead, file);
            if (std::ferror(file) != 0)
            {
                std::fclose(file);
                return dng::fs::FsStatus::UnknownError;
            }

            std::fclose(file);
            outRead = static_cast<dng::u64>(readCount);
            return dng::fs::FsStatus::Ok;
        }

        [[nodiscard]] dng::fs::FsStatus ReadFileRange(dng::fs::PathView path,
                                                      dng::u64 offsetBytes,
                                                      void* dst,
                                                      dng::u64 dstSize,
                                                      dng::u64& outRead) noexcept
        {
            outRead = 0;
            if (!readRangeSupported)
            {
                return dng::fs::FsStatus::NotSupported;
            }

            if (readRangeCallCount >= failAfterReadRangeCalls)
            {
                return dng::fs::FsStatus::UnknownError;
            }
            ++readRangeCallCount;

            if (dst == nullptr)
            {
                return dng::fs::FsStatus::InvalidArg;
            }

            char cPath[512]{};
            if (!PathToCString(path, cPath, static_cast<dng::u32>(sizeof(cPath))))
            {
                return dng::fs::FsStatus::InvalidArg;
            }

            std::FILE* file = nullptr;
#if defined(_MSC_VER)
            if (fopen_s(&file, cPath, "rb") != 0)
            {
                file = nullptr;
            }
#else
            file = std::fopen(cPath, "rb");
#endif
            if (file == nullptr)
            {
                return dng::fs::FsStatus::NotFound;
            }

            if (offsetBytes > static_cast<dng::u64>(std::numeric_limits<long>::max()) ||
                dstSize > static_cast<dng::u64>(~size_t{0}))
            {
                std::fclose(file);
                return dng::fs::FsStatus::InvalidArg;
            }

            if (std::fseek(file, static_cast<long>(offsetBytes), SEEK_SET) != 0)
            {
                std::fclose(file);
                return dng::fs::FsStatus::UnknownError;
            }

            const size_t toRead = static_cast<size_t>(dstSize);
            const size_t readCount = std::fread(dst, 1, toRead, file);
            if (std::ferror(file) != 0)
            {
                std::fclose(file);
                return dng::fs::FsStatus::UnknownError;
            }

            std::fclose(file);
            outRead = static_cast<dng::u64>(readCount);
            return dng::fs::FsStatus::Ok;
        }
    };
}

int RunAudioPlaybackSmoke()
{
    using namespace dng::audio;

    LocalFileSystemForSmoke localFileSystem{};
    dng::fs::FileSystemInterface fileSystem = dng::fs::MakeFileSystemInterface(localFileSystem);

    constexpr const char* kTestWavPath = "AudioPlayback_test.wav";
    constexpr const char* kResampledWavPath = "AudioPlayback_test_24k.wav";
    constexpr const char* kStreamedWavPath = "AudioPlayback_stream_long.wav";
    if (!WritePcm16WavForSmoke(kTestWavPath, 48000u, 16u) ||
        !WritePcm16WavForSmoke(kResampledWavPath, 24000u, 32u) ||
        !WritePcm16WavForSmoke(kStreamedWavPath, 48000u, 48000u))
    {
        return 1;
    }

    AudioSystemState state{};
    struct SmokeCleanup
    {
        AudioSystemState* state = nullptr;
        const char* wavPathA = nullptr;
        const char* wavPathB = nullptr;
        const char* wavPathC = nullptr;
        ~SmokeCleanup() noexcept
        {
            if (state != nullptr)
            {
                ShutdownAudioSystem(*state);
            }
            if (wavPathA != nullptr)
            {
                (void)std::remove(wavPathA);
            }
            if (wavPathB != nullptr)
            {
                (void)std::remove(wavPathB);
            }
            if (wavPathC != nullptr)
            {
                (void)std::remove(wavPathC);
            }
        }
    } cleanup{&state, kTestWavPath, kResampledWavPath, kStreamedWavPath};
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
    if (LoadWavPcm16Clip(state, fileSystem, kTestWavPath, clip) != AudioStatus::Ok || !IsValid(clip))
    {
        return 3;
    }

    AudioClipId missingClip{};
    if (LoadWavPcm16Clip(state, fileSystem, "AudioPlayback_missing.wav", missingClip) != AudioStatus::NotSupported)
    {
        return 39;
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

        constexpr float kStartEpsilon = 0.0001f;
        if (out[0] > kStartEpsilon || out[0] < -kStartEpsilon)
        {
            return 28;
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

        AudioMixParams stopMix{};
        stopMix.sampleRate = config.platform.sampleRate;
        stopMix.channelCount = config.platform.channelCount;
        stopMix.requestedFrames = 32;

        bool observedFadeOutEnergy = false;
        for (dng::u32 i = 0; i < 6; ++i)
        {
            float afterStop[256]{};
            stopMix.outSamples = afterStop;
            stopMix.outputCapacitySamples = 256;
            if (Mix(state, stopMix) != AudioStatus::Ok)
            {
                return 12;
            }

            if (HasNonZero(afterStop, stopMix.writtenSamples))
            {
                observedFadeOutEnergy = true;
            }
        }

        if (!observedFadeOutEnergy)
        {
            return 13;
        }

        float afterFade[256]{};
        stopMix.outSamples = afterFade;
        stopMix.outputCapacitySamples = 256;
        if (Mix(state, stopMix) != AudioStatus::Ok)
        {
            return 29;
        }

        if (HasNonZero(afterFade, stopMix.writtenSamples))
        {
            return 30;
        }
    }

    {
        AudioPlayParams controlled{};
        controlled.clip = clip;
        controlled.gain = 1.0f;
        controlled.pitch = 1.0f;
        controlled.bus = AudioBus::Music;
        controlled.loop = true;

        AudioVoiceId voice{};
        if (Play(state, controlled, voice) != AudioStatus::Ok)
        {
            return 54;
        }

        float out[512]{};
        AudioMixParams mix{};
        mix.outSamples = out;
        mix.outputCapacitySamples = 512;
        mix.sampleRate = config.platform.sampleRate;
        mix.channelCount = config.platform.channelCount;
        mix.requestedFrames = 64;

        if (Mix(state, mix) != AudioStatus::Ok || !HasNonZero(out, mix.writtenSamples))
        {
            return 55;
        }

        if (Pause(state, voice) != AudioStatus::Ok || Mix(state, mix) != AudioStatus::Ok)
        {
            return 56;
        }

        if (HasNonZero(out, mix.writtenSamples))
        {
            return 57;
        }

        if (Resume(state, voice) != AudioStatus::Ok || Mix(state, mix) != AudioStatus::Ok)
        {
            return 58;
        }

        if (!HasNonZero(out, mix.writtenSamples))
        {
            return 59;
        }

        if (Seek(state, voice, 0u) != AudioStatus::Ok || Mix(state, mix) != AudioStatus::Ok)
        {
            return 60;
        }

        if (!HasNonZero(out, mix.writtenSamples))
        {
            return 61;
        }

        if (SetBusGain(state, AudioBus::Music, 0.0f) != AudioStatus::Ok || Mix(state, mix) != AudioStatus::Ok)
        {
            return 62;
        }

        if (HasNonZero(out, mix.writtenSamples))
        {
            return 63;
        }

        if (SetBusGain(state, AudioBus::Music, 1.0f) != AudioStatus::Ok ||
            SetMasterGain(state, 0.0f) != AudioStatus::Ok ||
            Mix(state, mix) != AudioStatus::Ok)
        {
            return 64;
        }

        if (HasNonZero(out, mix.writtenSamples))
        {
            return 65;
        }

        if (SetMasterGain(state, 1.0f) != AudioStatus::Ok || Mix(state, mix) != AudioStatus::Ok)
        {
            return 66;
        }

        if (!HasNonZero(out, mix.writtenSamples))
        {
            return 67;
        }

        if (Stop(state, voice) != AudioStatus::Ok || Mix(state, mix) != AudioStatus::Ok)
        {
            return 68;
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
        for (dng::u32 i = 0; i < 10; ++i)
        {
            if (Mix(state, flushMix) != AudioStatus::Ok)
            {
                return false;
            }
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
        AudioClipId resampledClip{};
        if (LoadWavPcm16Clip(state, fileSystem, kResampledWavPath, resampledClip) != AudioStatus::Ok ||
            !IsValid(resampledClip))
        {
            return 31;
        }

        AudioPlayParams resampledPlay{};
        resampledPlay.clip = resampledClip;
        resampledPlay.gain = 1.0f;
        resampledPlay.pitch = 1.0f;
        resampledPlay.loop = false;

        AudioVoiceId resampledVoice{};
        if (Play(state, resampledPlay, resampledVoice) != AudioStatus::Ok)
        {
            return 32;
        }

        float resampledOut[512]{};
        AudioMixParams resampledMix{};
        resampledMix.outSamples = resampledOut;
        resampledMix.outputCapacitySamples = 512;
        resampledMix.sampleRate = config.platform.sampleRate;
        resampledMix.channelCount = config.platform.channelCount;
        resampledMix.requestedFrames = 64;
        if (Mix(state, resampledMix) != AudioStatus::Ok || resampledMix.writtenSamples != 128)
        {
            return 33;
        }

        if (!HasNonZero(resampledOut, 48) || !HasNonZero(&resampledOut[96], 32))
        {
            return 34;
        }

        bool reachedSilence = false;
        for (dng::u32 i = 0; i < 4; ++i)
        {
            float resampledTail[512]{};
            resampledMix.outSamples = resampledTail;
            if (Mix(state, resampledMix) != AudioStatus::Ok)
            {
                return 35;
            }

            if (!HasNonZero(resampledTail, resampledMix.writtenSamples))
            {
                reachedSilence = true;
                break;
            }
        }

        if (!reachedSilence)
        {
            return 36;
        }

        if (UnloadClip(state, resampledClip) != AudioStatus::Ok)
        {
            return 37;
        }

        if (GetLoadedClipCount(state) != 1)
        {
            return 38;
        }
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
    if (LoadWavPcm16Clip(state, fileSystem, kTestWavPath, clipReloaded) != AudioStatus::Ok ||
        !IsValid(clipReloaded))
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

    {
        if (HasBoundStreamFileSystem(state))
        {
            return 69;
        }

        AudioClipId unboundStreamClip{};
        if (LoadWavPcm16StreamClip(state, fileSystem, kStreamedWavPath, unboundStreamClip) != AudioStatus::NotSupported)
        {
            return 70;
        }

        if (BindStreamFileSystem(state, fileSystem) != AudioStatus::Ok || !HasBoundStreamFileSystem(state))
        {
            return 71;
        }

        if (BindStreamFileSystem(state, fileSystem) != AudioStatus::Ok)
        {
            return 72;
        }

        LocalFileSystemForSmoke otherFileSystem{};
        dng::fs::FileSystemInterface otherInterface = dng::fs::MakeFileSystemInterface(otherFileSystem);
        AudioClipId mismatchedStreamClip{};
        if (LoadWavPcm16StreamClip(state, otherInterface, kStreamedWavPath, mismatchedStreamClip) != AudioStatus::NotSupported)
        {
            return 73;
        }

        localFileSystem.readRangeSupported = false;
        AudioClipId unsupportedRangeClip{};
        if (LoadWavPcm16StreamClip(state, fileSystem, kStreamedWavPath, unsupportedRangeClip) != AudioStatus::NotSupported)
        {
            return 74;
        }
        localFileSystem.readRangeSupported = true;
        localFileSystem.failAfterReadRangeCalls = ~dng::u32{0};

        AudioClipId oversizedClip{};
        if (LoadWavPcm16Clip(state, fileSystem, kStreamedWavPath, oversizedClip) != AudioStatus::NotSupported)
        {
            return 46;
        }

        constexpr dng::u32 kMaxSmokeStreamClips = 16;
        AudioClipId streamClips[kMaxSmokeStreamClips]{};
        dng::u32 streamClipCount = 0;
        const dng::u32 maxStreamClipCount = GetMaxStreamClipCount(state);
        if (maxStreamClipCount == 0u || maxStreamClipCount > kMaxSmokeStreamClips)
        {
            return 75;
        }

        for (; streamClipCount < maxStreamClipCount; ++streamClipCount)
        {
            AudioClipId streamClip{};
            if (LoadWavPcm16StreamClip(state, fileSystem, kStreamedWavPath, streamClip) != AudioStatus::Ok ||
                !IsValid(streamClip))
            {
                return 76;
            }
            streamClips[streamClipCount] = streamClip;
        }

        if (GetLoadedStreamClipCount(state) != maxStreamClipCount)
        {
            return 77;
        }

        AudioClipId overflowStreamClip{};
        if (LoadWavPcm16StreamClip(state, fileSystem, kStreamedWavPath, overflowStreamClip) != AudioStatus::NotSupported)
        {
            return 78;
        }

        if (UnbindStreamFileSystem(state) != AudioStatus::NotSupported)
        {
            return 79;
        }

        for (dng::u32 i = 0; i < streamClipCount; ++i)
        {
            if (UnloadClip(state, streamClips[i]) != AudioStatus::Ok)
            {
                return 80;
            }
        }

        if (GetLoadedStreamClipCount(state) != 0u)
        {
            return 81;
        }

        if (UnbindStreamFileSystem(state) != AudioStatus::Ok || HasBoundStreamFileSystem(state))
        {
            return 82;
        }

        if (BindStreamFileSystem(state, fileSystem) != AudioStatus::Ok)
        {
            return 83;
        }

        AudioClipId streamClip{};
        if (LoadWavPcm16StreamClip(state, fileSystem, kStreamedWavPath, streamClip) != AudioStatus::Ok ||
            !IsValid(streamClip))
        {
            return 47;
        }

        if (GetLoadedStreamClipCount(state) != 1u || GetClipPoolUsageSamples(state) != 0u)
        {
            return 48;
        }

        AudioPlayParams streamPlay{};
        streamPlay.clip = streamClip;
        streamPlay.gain = 1.0f;
        streamPlay.pitch = 1.0f;
        streamPlay.loop = false;

        AudioVoiceId streamVoice{};
        if (Play(state, streamPlay, streamVoice) != AudioStatus::Ok)
        {
            return 49;
        }

        float streamOut[512]{};
        AudioMixParams streamMix{};
        streamMix.outSamples = streamOut;
        streamMix.outputCapacitySamples = 512;
        streamMix.sampleRate = config.platform.sampleRate;
        streamMix.channelCount = config.platform.channelCount;
        streamMix.requestedFrames = 64;

        if (Mix(state, streamMix) != AudioStatus::Ok || streamMix.writtenSamples != 128)
        {
            return 50;
        }

        if (!HasNonZero(streamOut, streamMix.writtenSamples))
        {
            return 51;
        }

        localFileSystem.failAfterReadRangeCalls = localFileSystem.readRangeCallCount;
        if (Seek(state, streamVoice, 3000u) != AudioStatus::Ok || Mix(state, streamMix) != AudioStatus::Ok)
        {
            return 84;
        }

        if (HasNonZero(streamOut, streamMix.writtenSamples))
        {
            return 85;
        }

        localFileSystem.failAfterReadRangeCalls = ~dng::u32{0};
        const AudioStatus stopStatus = Stop(state, streamVoice);
        if (stopStatus != AudioStatus::Ok && stopStatus != AudioStatus::InvalidArg)
        {
            return 86;
        }

        if (Mix(state, streamMix) != AudioStatus::Ok)
        {
            return 52;
        }

        if (UnloadClip(state, streamClip) != AudioStatus::Ok ||
            GetLoadedStreamClipCount(state) != 0u ||
            GetLoadedClipCount(state) != 0u ||
            GetClipPoolUsageSamples(state) != 0u)
        {
            return 53;
        }

        if (UnbindStreamFileSystem(state) != AudioStatus::Ok || HasBoundStreamFileSystem(state))
        {
            return 87;
        }
    }

    {
        constexpr dng::u32 kMaxSmokeClips = 128;
        AudioClipId loadedClips[kMaxSmokeClips]{};
        dng::u32 loadedCount = 0;

        const dng::u32 maxClipCount = GetMaxClipCount(state);
        if (maxClipCount == 0 || maxClipCount > kMaxSmokeClips)
        {
            return 40;
        }

        for (; loadedCount < maxClipCount; ++loadedCount)
        {
            AudioClipId loaded{};
            if (LoadWavPcm16Clip(state, fileSystem, kTestWavPath, loaded) != AudioStatus::Ok || !IsValid(loaded))
            {
                return 41;
            }
            loadedClips[loadedCount] = loaded;
        }

        if (GetLoadedClipCount(state) != maxClipCount)
        {
            return 42;
        }

        AudioClipId overflowClip{};
        if (LoadWavPcm16Clip(state, fileSystem, kTestWavPath, overflowClip) != AudioStatus::NotSupported)
        {
            return 43;
        }

        for (dng::u32 i = 0; i < loadedCount; ++i)
        {
            if (UnloadClip(state, loadedClips[i]) != AudioStatus::Ok)
            {
                return 44;
            }
        }

        if (GetLoadedClipCount(state) != 0 || GetClipPoolUsageSamples(state) != 0)
        {
            return 45;
        }
    }

    return 0;
}
