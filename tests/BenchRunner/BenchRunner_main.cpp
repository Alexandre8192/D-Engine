// ============================================================================
// D-Engine - tests/BenchRunner/BenchRunner_main.cpp
// ----------------------------------------------------------------------------
// Purpose : Minimal BenchRunner harness (Bench M0) to emit basic performance metrics.
// Notes   : No exceptions/RTTI. Small registry of microbenches with steady_clock timing.
// ============================================================================

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Contracts/FileSystem.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* kBenchAudioMemPath = "artifacts/bench/Bench_audio_mem.wav";
    constexpr const char* kBenchAudioStreamPath = "artifacts/bench/Bench_audio_stream.wav";

    using Clock = std::chrono::steady_clock;

    struct BenchArgs
    {
        int warmupCount = 1;
        double targetRsdPct = 3.0;
        int maxRepeats = 15;
        int iterations = 20000000;
    };

    struct BenchSample
    {
        std::string name;
        double nsPerOp = 0.0;
        double rsdPct = 0.0;
        double bytesPerOp = 0.0;
        double allocsPerOp = 0.0;
    };

    struct Benchmark
    {
        std::string_view name;
        int iterations;
        double bytesPerOp;
        double allocsPerOp;
        void (*fn)(int iterations, volatile std::uint64_t& sink);
    };

    std::string GetEnv(const char* name)
    {
        char* buffer = nullptr;
        std::size_t len = 0;
        if (_dupenv_s(&buffer, &len, name) != 0 || buffer == nullptr)
        {
            return {};
        }

        std::string value{buffer};
        std::free(buffer);
        return value;
    }

    std::string BuildOutputPath()
    {
        std::string base;
        const std::string envPath = GetEnv("DNG_BENCH_OUT");
        base = envPath.empty() ? std::string{"artifacts/bench"} : envPath;

        auto now = std::chrono::system_clock::now();
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        std::string path = base;
        path.append("/bench-");
        path.append(std::to_string(secs));
        path.append(".bench.json");
        return path;
    }

    bool EnsureParentDirectory(const std::filesystem::path& p)
    {
        std::error_code ec{};
        const auto parent = p.parent_path();
        if (parent.empty()) { return true; }
        if (std::filesystem::exists(parent, ec)) { return true; }
        return std::filesystem::create_directories(parent, ec);
    }

    void ParseArgs(int argc, char** argv, BenchArgs& args)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view a{argv[i]};
            if (a == "--warmup" && i + 1 < argc) { args.warmupCount = std::atoi(argv[++i]); }
            else if (a == "--target-rsd" && i + 1 < argc) { args.targetRsdPct = std::atof(argv[++i]); }
            else if (a == "--max-repeat" && i + 1 < argc) { args.maxRepeats = std::atoi(argv[++i]); }
            else if (a == "--iterations" && i + 1 < argc) { args.iterations = std::atoi(argv[++i]); }
        }
        if (args.warmupCount < 0) { args.warmupCount = 0; }
        if (args.maxRepeats < 1) { args.maxRepeats = 1; }
        if (args.iterations < 1) { args.iterations = 1; }
    }

    template<typename Fn>
    double TimeOnce(Fn&& fn)
    {
        const auto t0 = Clock::now();
        fn();
        const auto t1 = Clock::now();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        return static_cast<double>(ns);
    }

    double ComputeMean(const std::vector<double>& values)
    {
        if (values.empty()) { return 0.0; }
        double sum = 0.0;
        for (double v : values) { sum += v; }
        return sum / static_cast<double>(values.size());
    }

    double ComputeRsdPct(const std::vector<double>& values, double mean)
    {
        if (values.size() < 2 || mean == 0.0) { return 0.0; }
        double var = 0.0;
        for (double v : values)
        {
            const double d = v - mean;
            var += d * d;
        }
        var /= static_cast<double>(values.size() - 1);
        const double stddev = std::sqrt(var);
        return (stddev / mean) * 100.0;
    }

    double RunBenchmark(const Benchmark& bench, const BenchArgs& args, double& rsdOut)
    {
        volatile std::uint64_t sink = 0;
        const auto runner = [&]() { bench.fn(bench.iterations, sink); };

        for (int i = 0; i < args.warmupCount; ++i) { runner(); }

        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(args.maxRepeats));

        constexpr int batchRuns = 3;

        for (int rep = 0; rep < args.maxRepeats; ++rep)
        {
            double totalNs = 0.0;
            for (int b = 0; b < batchRuns; ++b) { totalNs += TimeOnce(runner); }
            const double nsPerOp = totalNs / (static_cast<double>(bench.iterations) * static_cast<double>(batchRuns));
            samples.push_back(nsPerOp);

            const double mean = ComputeMean(samples);
            const double rsd = ComputeRsdPct(samples, mean);
            if (samples.size() >= 2 && rsd <= args.targetRsdPct)
            {
                rsdOut = rsd;
                return mean;
            }
        }

        const double mean = ComputeMean(samples);
        rsdOut = ComputeRsdPct(samples, mean);
        return mean;
    }

    void Bench_BaselineLoop(int iterations, volatile std::uint64_t& sink)
    {
        std::uint64_t local = 0;
        for (int i = 0; i < iterations; ++i)
        {
            const std::uint64_t v = static_cast<std::uint64_t>(i & 0xFFu);
            local += v;
            local ^= (v << 8);
            local += (v << 16);
            local ^= (v << 24);
        }
        sink ^= local;
    }

    void Bench_Vec3Dot(int iterations, volatile std::uint64_t& sink)
    {
        struct Vec3 { float x, y, z; };
        Vec3 a{1.0f, 2.0f, 3.0f};
        Vec3 b{4.0f, 5.0f, 6.0f};
        float acc = 0.0f;
        for (int i = 0; i < iterations; ++i)
        {
            acc += a.x * b.x + a.y * b.y + a.z * b.z;
            a.x += 0.0001f;
        }
        const auto bits = static_cast<std::uint64_t>(acc * 1000.0f);
        sink ^= bits;
    }

    void Bench_Memcpy64(int iterations, volatile std::uint64_t& sink)
    {
        alignas(64) std::array<std::uint8_t, 64> src{};
        alignas(64) std::array<std::uint8_t, 64> dst{};
        for (std::size_t i = 0; i < src.size(); ++i) { src[i] = static_cast<std::uint8_t>(i); }
        std::uint64_t acc = 0;
        constexpr int copiesPerIter = 8;
        for (int i = 0; i < iterations; ++i)
        {
            for (int c = 0; c < copiesPerIter; ++c)
            {
                std::memcpy(dst.data(), src.data(), src.size());
                acc += dst[0] + dst[63];
            }
        }
        sink ^= acc;
    }

    void Bench_AudioMixNull1024fStereo(int iterations, volatile std::uint64_t& sink)
    {
        static dng::audio::AudioSystemState state{};
        static bool initialized = false;
        if (!initialized)
        {
            dng::audio::AudioSystemConfig config{};
            config.backend = dng::audio::AudioSystemBackend::Null;
            initialized = dng::audio::InitAudioSystem(state, config);
        }
        if (!initialized)
        {
            return;
        }

        float buffer[2048]{};
        dng::audio::AudioMixParams mix{};
        mix.outSamples = buffer;
        mix.outputCapacitySamples = 2048;
        mix.sampleRate = 48000;
        mix.channelCount = 2;
        mix.requestedFrames = 1024;
        mix.deltaTimeSec = 1.0f / 60.0f;

        std::uint64_t acc = 0;
        for (int i = 0; i < iterations; ++i)
        {
            mix.frameIndex = static_cast<dng::u64>(i);
            if (dng::audio::Mix(state, mix) == dng::audio::AudioStatus::Ok)
            {
                acc += mix.writtenSamples;
                acc += (buffer[0] == 0.0f) ? 1ull : 2ull;
            }
        }
        sink ^= acc;
    }

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

    [[nodiscard]] bool WritePcm16Wav(const char* path,
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
        const dng::u32 dataBytes = frameCount * static_cast<dng::u32>(kChannels) * sizeof(dng::i16);
        constexpr dng::u32 fmtChunkBytes = 16;
        const dng::u32 riffSize = 4 + 8 + fmtChunkBytes + 8 + dataBytes;

        bool ok = true;
        ok = ok && (std::fwrite("RIFF", 1, 4, file) == 4);
        ok = ok && WriteLe32(file, riffSize);
        ok = ok && (std::fwrite("WAVE", 1, 4, file) == 4);

        ok = ok && (std::fwrite("fmt ", 1, 4, file) == 4);
        ok = ok && WriteLe32(file, fmtChunkBytes);
        ok = ok && WriteLe16(file, 1u);
        ok = ok && WriteLe16(file, kChannels);
        ok = ok && WriteLe32(file, sampleRate);
        ok = ok && WriteLe32(file, sampleRate * static_cast<dng::u32>(kChannels) * sizeof(dng::i16));
        ok = ok && WriteLe16(file, static_cast<dng::u16>(kChannels * sizeof(dng::i16)));
        ok = ok && WriteLe16(file, kBitsPerSample);

        ok = ok && (std::fwrite("data", 1, 4, file) == 4);
        ok = ok && WriteLe32(file, dataBytes);

        for (dng::u32 frame = 0; ok && frame < frameCount; ++frame)
        {
            const dng::i16 left = (frame < (frameCount / 2u)) ? 11000 : -11000;
            const dng::i16 right = static_cast<dng::i16>(-left);
            ok = ok && (std::fwrite(&left, sizeof(left), 1, file) == 1);
            ok = ok && (std::fwrite(&right, sizeof(right), 1, file) == 1);
        }

        std::fclose(file);
        return ok;
    }

    struct LocalBenchFileSystem
    {
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

    struct AudioPlatformBenchContext
    {
        dng::audio::AudioSystemState state{};
        LocalBenchFileSystem localFileSystem{};
        dng::fs::FileSystemInterface fileSystem{};
        dng::audio::AudioClipId clip{};
        bool initialized = false;
    };

    bool EnsureAudioPlatformBench(AudioPlatformBenchContext& context,
                                  const char* wavPath,
                                  dng::u32 frameCount,
                                  bool stream) noexcept
    {
        if (context.initialized)
        {
            return true;
        }

        if (wavPath == nullptr)
        {
            return false;
        }

        const std::filesystem::path wavFilePath = std::filesystem::path(wavPath);
        if (wavFilePath.has_parent_path())
        {
            std::error_code ec;
            (void)std::filesystem::create_directories(wavFilePath.parent_path(), ec);
            if (ec)
            {
                return false;
            }
        }

        if (!WritePcm16Wav(wavPath, 48000u, frameCount))
        {
            return false;
        }

        context.fileSystem = dng::fs::MakeFileSystemInterface(context.localFileSystem);

        dng::audio::AudioSystemConfig config{};
        config.backend = dng::audio::AudioSystemBackend::Platform;
        config.fallbackToNullOnInitFailure = false;
        if (!dng::audio::InitAudioSystem(context.state, config) ||
            context.state.backend != dng::audio::AudioSystemBackend::Platform)
        {
            return false;
        }

        if (stream)
        {
            if (dng::audio::BindStreamFileSystem(context.state, context.fileSystem) != dng::audio::AudioStatus::Ok)
            {
                return false;
            }
            if (dng::audio::LoadWavPcm16StreamClip(context.state, context.fileSystem, wavPath, context.clip) != dng::audio::AudioStatus::Ok)
            {
                return false;
            }
        }
        else
        {
            if (dng::audio::LoadWavPcm16Clip(context.state, context.fileSystem, wavPath, context.clip) != dng::audio::AudioStatus::Ok)
            {
                return false;
            }
        }

        dng::audio::AudioPlayParams play{};
        play.clip = context.clip;
        play.gain = 1.0f;
        play.pitch = 1.0f;
        play.bus = dng::audio::AudioBus::Sfx;
        play.loop = true;
        dng::audio::AudioVoiceId voice{};
        if (dng::audio::Play(context.state, play, voice) != dng::audio::AudioStatus::Ok)
        {
            return false;
        }

        float warmupBuffer[2048]{};
        dng::audio::AudioMixParams warmupMix{};
        warmupMix.outSamples = warmupBuffer;
        warmupMix.outputCapacitySamples = 2048;
        warmupMix.sampleRate = 48000;
        warmupMix.channelCount = 2;
        warmupMix.requestedFrames = 1024;
        if (dng::audio::Mix(context.state, warmupMix) != dng::audio::AudioStatus::Ok)
        {
            return false;
        }

        context.initialized = true;
        return true;
    }

    void Bench_AudioMixMemoryClipPlatform1024fStereo(int iterations, volatile std::uint64_t& sink)
    {
        static AudioPlatformBenchContext context{};
        if (!EnsureAudioPlatformBench(context, kBenchAudioMemPath, 4096u, false))
        {
            return;
        }

        float buffer[2048]{};
        dng::audio::AudioMixParams mix{};
        mix.outSamples = buffer;
        mix.outputCapacitySamples = 2048;
        mix.sampleRate = 48000;
        mix.channelCount = 2;
        mix.requestedFrames = 1024;

        std::uint64_t acc = 0;
        for (int i = 0; i < iterations; ++i)
        {
            mix.frameIndex = static_cast<dng::u64>(i);
            if (dng::audio::Mix(context.state, mix) == dng::audio::AudioStatus::Ok)
            {
                acc += mix.writtenSamples;
                acc += (buffer[0] == 0.0f) ? 1ull : 2ull;
            }
        }
        sink ^= acc;
    }

    void Bench_AudioMixStreamClipPlatform1024fStereo(int iterations, volatile std::uint64_t& sink)
    {
        static AudioPlatformBenchContext context{};
        if (!EnsureAudioPlatformBench(context, kBenchAudioStreamPath, 48000u, true))
        {
            return;
        }

        float buffer[2048]{};
        dng::audio::AudioMixParams mix{};
        mix.outSamples = buffer;
        mix.outputCapacitySamples = 2048;
        mix.sampleRate = 48000;
        mix.channelCount = 2;
        mix.requestedFrames = 1024;

        std::uint64_t acc = 0;
        for (int i = 0; i < iterations; ++i)
        {
            mix.frameIndex = static_cast<dng::u64>(i);
            if (dng::audio::Mix(context.state, mix) == dng::audio::AudioStatus::Ok)
            {
                acc += mix.writtenSamples;
                acc += (buffer[0] == 0.0f) ? 1ull : 2ull;
            }
        }
        sink ^= acc;
    }

    void WriteJson(const std::filesystem::path& outPath, const std::vector<BenchSample>& samples)
    {
        std::ofstream os(outPath.string(), std::ios::binary);
        if (!os) { std::fprintf(stderr, "Failed to open bench output file: %s\n", outPath.string().c_str()); return; }

        os << "{\n";
        os << "  \"benchmarks\": [\n";
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const auto& s = samples[i];
            os << "    {\n";
            os << "      \"name\": \"" << s.name << "\",\n";
            os << "      \"value\": " << std::fixed << std::setprecision(6) << s.nsPerOp << ",\n";
            os << "      \"rsdPct\": " << std::fixed << std::setprecision(6) << s.rsdPct << ",\n";
            os << "      \"bytesPerOp\": " << std::fixed << std::setprecision(6) << s.bytesPerOp << ",\n";
            os << "      \"allocsPerOp\": " << std::fixed << std::setprecision(6) << s.allocsPerOp << "\n";
            os << "    }";
            if (i + 1 != samples.size()) { os << ","; }
            os << "\n";
        }
        os << "  ],\n";
        os << "  \"metadata\": {\n";
        os << "    \"note\": \"BenchRunner M0\",\n";
        const std::string sha = GetEnv("GITHUB_SHA");
        if (!sha.empty()) { os << "    \"gitSha\": \"" << sha << "\",\n"; }
        os << "    \"unit\": \"ns/op\"\n";
        os << "  }\n";
        os << "}\n";
    }
}

int main(int argc, char** argv)
{
    BenchArgs args{};
    ParseArgs(argc, argv, args);

    const std::string outPath = BuildOutputPath();
    const std::filesystem::path outFs{outPath};
    if (!EnsureParentDirectory(outFs))
    {
        std::fprintf(stderr, "Failed to create bench output directory: %s\n", outFs.parent_path().string().c_str());
        return 1;
    }

    const int baseIter = args.iterations;
    const int vecIter = args.iterations > 1 ? args.iterations / 2 : 1;
    const int memcpyIter = args.iterations > 1 ? args.iterations / 2 : 1;
    const int audioMixIter = args.iterations > 1000 ? args.iterations / 1000 : 1000;
    const int audioPlatformIter = args.iterations > 2000 ? args.iterations / 2000 : 500;

    const std::array<Benchmark, 6> benches = {{
        {"baseline_loop", baseIter, 0.0, 0.0, &Bench_BaselineLoop},
        {"vec3_dot", vecIter, 12.0, 0.0, &Bench_Vec3Dot},
        {"memcpy_64", memcpyIter, 512.0, 0.0, &Bench_Memcpy64},
        {"audio_mix_null_1024f_stereo", audioMixIter, 8192.0, 0.0, &Bench_AudioMixNull1024fStereo},
        {"audio_mix_mem_clip_platform_1024f_stereo", audioPlatformIter, 8192.0, 0.0, &Bench_AudioMixMemoryClipPlatform1024fStereo},
        {"audio_mix_stream_clip_platform_1024f_stereo", audioPlatformIter, 8192.0, 0.0, &Bench_AudioMixStreamClipPlatform1024fStereo},
    }};

    std::vector<BenchSample> results;
    results.reserve(benches.size());

    for (const auto& b : benches)
    {
        double rsd = 0.0;
        const double meanNs = RunBenchmark(b, args, rsd);
        results.push_back(BenchSample{
            std::string{b.name},
            meanNs,
            rsd,
            b.bytesPerOp,
            b.allocsPerOp
        });
    }

    WriteJson(outFs, results);
    std::printf("BenchRunner wrote %s\n", outPath.c_str());
    return 0;
}
