// ============================================================================
// D-Engine - tests/BenchRunner/BenchRunner_main.cpp
// ----------------------------------------------------------------------------
// Purpose : BenchRunner harness with repeat-based stability checks and JSON
//           output used by local gates and CI perf comparisons.
// Contract: No exceptions/RTTI. Benchmarks report explicit status
//           (ok/skipped/unstable/error) with reason strings.
// Notes   : Uses dng::bench::Run for measured ns/op + bytes/op + allocs/op.
// ============================================================================

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Contracts/FileSystem.hpp"
#include "Core/Diagnostics/Bench.hpp"
#include "Core/Memory/MemorySystem.hpp"

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

#if defined(_WIN32) || defined(_WIN64)
    #define NOMINMAX
    #include <windows.h>
#endif

namespace
{
    constexpr const char* kBenchAudioMemPath = "artifacts/bench/Bench_audio_mem.wav";
    constexpr const char* kBenchAudioStreamPath = "artifacts/bench/Bench_audio_stream.wav";
    constexpr dng::u32 kBenchSchemaVersion = 2u;
    constexpr int kDefaultWarmup = 1;
    constexpr double kDefaultTargetRsd = 3.0;
    constexpr int kDefaultMaxRepeats = 15;
    constexpr int kDefaultIterations = 20000000;
    constexpr int kBatchRuns = 3;

    enum class BenchStatus : dng::u8
    {
        Ok = 0,
        Skipped,
        Unstable,
        Error
    };

    [[nodiscard]] const char* ToString(BenchStatus status) noexcept
    {
        switch (status)
        {
            case BenchStatus::Ok:       return "ok";
            case BenchStatus::Skipped:  return "skipped";
            case BenchStatus::Unstable: return "unstable";
            case BenchStatus::Error:    return "error";
            default:                    return "error";
        }
    }

    struct BenchArgs
    {
        int warmupCount = kDefaultWarmup;
        double targetRsdPct = kDefaultTargetRsd;
        int maxRepeats = kDefaultMaxRepeats;
        int iterations = kDefaultIterations;
        bool cpuInfo = false;
        bool strictStability = false;
        bool showHelp = false;
    };

    struct BenchSample
    {
        std::string name;
        double nsPerOp = 0.0;
        double rsdPct = 0.0;
        double bytesPerOp = -1.0;
        double allocsPerOp = -1.0;
        BenchStatus status = BenchStatus::Error;
        std::string reason;
        int repeatsUsed = 0;
        double targetRsdPct = 0.0;
    };

    struct BenchSummary
    {
        int okCount = 0;
        int skippedCount = 0;
        int unstableCount = 0;
        int errorCount = 0;
    };

    using BenchFn = bool (*)(int iterations, volatile std::uint64_t& sink, const char*& outReason) noexcept;

    struct Benchmark
    {
        std::string_view name;
        int iterations = 1;
        BenchFn fn = nullptr;
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

    [[nodiscard]] bool ParseIntValue(const char* text, int minInclusive, int maxInclusive, int& outValue) noexcept
    {
        if (text == nullptr || *text == '\0')
        {
            return false;
        }

        char* end = nullptr;
        const long parsed = std::strtol(text, &end, 10);
        if (end == text || *end != '\0')
        {
            return false;
        }

        if (parsed < static_cast<long>(minInclusive) || parsed > static_cast<long>(maxInclusive))
        {
            return false;
        }

        outValue = static_cast<int>(parsed);
        return true;
    }

    [[nodiscard]] bool ParseDoubleValue(const char* text, double minInclusive, double maxInclusive, double& outValue) noexcept
    {
        if (text == nullptr || *text == '\0')
        {
            return false;
        }

        char* end = nullptr;
        const double parsed = std::strtod(text, &end);
        if (end == text || *end != '\0')
        {
            return false;
        }

        if (!std::isfinite(parsed))
        {
            return false;
        }

        if (parsed < minInclusive || parsed > maxInclusive)
        {
            return false;
        }

        outValue = parsed;
        return true;
    }

    void PrintUsage()
    {
        std::printf("D-Engine BenchRunner\n");
        std::printf("Usage:\n");
        std::printf("  D-Engine-BenchRunner.exe [options]\n");
        std::printf("\n");
        std::printf("Options:\n");
        std::printf("  --warmup N            Warmup runs before measurement (default: %d)\n", kDefaultWarmup);
        std::printf("  --target-rsd P        RSD target percentage (default: %.1f)\n", kDefaultTargetRsd);
        std::printf("  --max-repeat M        Maximum measured repeats (default: %d)\n", kDefaultMaxRepeats);
        std::printf("  --repeat M            Alias for --max-repeat\n");
        std::printf("  --iterations K        Base iteration budget (default: %d)\n", kDefaultIterations);
        std::printf("  --cpu-info            Print runtime CPU/affinity/priority info\n");
        std::printf("  --strict-stability    Return non-zero when any benchmark is unstable\n");
        std::printf("  --help                Show this help message\n");
    }
    bool ParseArgs(int argc, char** argv, BenchArgs& args, std::string& outError)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view a{argv[i]};
            if (a == "--help")
            {
                args.showHelp = true;
            }
            else if (a == "--cpu-info")
            {
                args.cpuInfo = true;
            }
            else if (a == "--strict-stability")
            {
                args.strictStability = true;
            }
            else if (a == "--warmup")
            {
                if (i + 1 >= argc || !ParseIntValue(argv[i + 1], 0, std::numeric_limits<int>::max(), args.warmupCount))
                {
                    outError = "Invalid value for --warmup";
                    return false;
                }
                ++i;
            }
            else if (a == "--target-rsd")
            {
                if (i + 1 >= argc || !ParseDoubleValue(argv[i + 1], 0.0, 1000.0, args.targetRsdPct))
                {
                    outError = "Invalid value for --target-rsd";
                    return false;
                }
                ++i;
            }
            else if (a == "--max-repeat" || a == "--repeat")
            {
                if (i + 1 >= argc || !ParseIntValue(argv[i + 1], 1, std::numeric_limits<int>::max(), args.maxRepeats))
                {
                    outError = (a == "--repeat")
                        ? "Invalid value for --repeat"
                        : "Invalid value for --max-repeat";
                    return false;
                }
                ++i;
            }
            else if (a == "--iterations")
            {
                if (i + 1 >= argc || !ParseIntValue(argv[i + 1], 1, std::numeric_limits<int>::max(), args.iterations))
                {
                    outError = "Invalid value for --iterations";
                    return false;
                }
                ++i;
            }
            else
            {
                outError = "Unknown argument: ";
                outError += std::string{a};
                return false;
            }
        }

        return true;
    }

    void PrintCpuInfo() noexcept
    {
#if defined(_WIN32) || defined(_WIN64)
        const HANDLE process = ::GetCurrentProcess();
        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask = 0;
        const BOOL maskOk = ::GetProcessAffinityMask(process, &processMask, &systemMask);

        const DWORD logicalCpuCount = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        const DWORD priorityClass = ::GetPriorityClass(process);

        const char* priorityText = "UNKNOWN";
        switch (priorityClass)
        {
            case IDLE_PRIORITY_CLASS:         priorityText = "IDLE"; break;
            case BELOW_NORMAL_PRIORITY_CLASS: priorityText = "BELOW_NORMAL"; break;
            case NORMAL_PRIORITY_CLASS:       priorityText = "NORMAL"; break;
            case ABOVE_NORMAL_PRIORITY_CLASS: priorityText = "ABOVE_NORMAL"; break;
            case HIGH_PRIORITY_CLASS:         priorityText = "HIGH"; break;
            case REALTIME_PRIORITY_CLASS:     priorityText = "REALTIME"; break;
            default:                          break;
        }

        const unsigned long long affinityValue = maskOk
            ? static_cast<unsigned long long>(processMask)
            : 0ull;

        std::printf("[CPU] logical=%lu affinity=0x%llx priority=%s\n",
                    static_cast<unsigned long>(logicalCpuCount),
                    affinityValue,
                    priorityText);
#else
        std::printf("[CPU] cpu-info not supported on this platform\n");
#endif
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

    bool Bench_BaselineLoop(int iterations, volatile std::uint64_t& sink, const char*& outReason) noexcept
    {
        outReason = nullptr;
        if (iterations < 0)
        {
            outReason = "iterations must be non-negative";
            return false;
        }

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
        return true;
    }

    bool Bench_Vec3Dot(int iterations, volatile std::uint64_t& sink, const char*& outReason) noexcept
    {
        outReason = nullptr;
        if (iterations < 0)
        {
            outReason = "iterations must be non-negative";
            return false;
        }

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
        return true;
    }

    bool Bench_Memcpy64(int iterations, volatile std::uint64_t& sink, const char*& outReason) noexcept
    {
        outReason = nullptr;
        if (iterations < 0)
        {
            outReason = "iterations must be non-negative";
            return false;
        }

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
        return true;
    }

    bool Bench_AudioMixNull1024fStereo(int iterations, volatile std::uint64_t& sink, const char*& outReason) noexcept
    {
        outReason = nullptr;
        if (iterations < 0)
        {
            outReason = "iterations must be non-negative";
            return false;
        }

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
            outReason = "Null audio backend init failed";
            return false;
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
        return true;
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
        bool initAttempted = false;
        const char* initFailureReason = nullptr;
    };

    bool EnsureAudioPlatformBench(AudioPlatformBenchContext& context,
                                  const char* wavPath,
                                  dng::u32 frameCount,
                                  bool stream,
                                  const char*& outReason) noexcept
    {
        outReason = nullptr;

        if (context.initialized)
        {
            return true;
        }

        if (context.initAttempted)
        {
            outReason = (context.initFailureReason != nullptr)
                ? context.initFailureReason
                : "platform audio backend unavailable";
            return false;
        }

        context.initAttempted = true;
        context.initFailureReason = "platform audio backend unavailable";

        if (wavPath == nullptr)
        {
            context.initFailureReason = "invalid WAV path";
            outReason = context.initFailureReason;
            return false;
        }

        const std::filesystem::path wavFilePath = std::filesystem::path(wavPath);
        if (wavFilePath.has_parent_path())
        {
            std::error_code ec;
            (void)std::filesystem::create_directories(wavFilePath.parent_path(), ec);
            if (ec)
            {
                context.initFailureReason = "failed to create bench WAV directory";
                outReason = context.initFailureReason;
                return false;
            }
        }

        if (!WritePcm16Wav(wavPath, 48000u, frameCount))
        {
            context.initFailureReason = "failed to create bench WAV file";
            outReason = context.initFailureReason;
            return false;
        }

        context.fileSystem = dng::fs::MakeFileSystemInterface(context.localFileSystem);

        dng::audio::AudioSystemConfig config{};
        config.backend = dng::audio::AudioSystemBackend::Platform;
        config.fallbackToNullOnInitFailure = false;
        if (!dng::audio::InitAudioSystem(context.state, config) ||
            context.state.backend != dng::audio::AudioSystemBackend::Platform)
        {
            context.initFailureReason = "platform audio init failed";
            outReason = context.initFailureReason;
            return false;
        }

        if (stream)
        {
            if (dng::audio::BindStreamFileSystem(context.state, context.fileSystem) != dng::audio::AudioStatus::Ok)
            {
                context.initFailureReason = "stream FS bind failed";
                outReason = context.initFailureReason;
                return false;
            }
            if (dng::audio::LoadWavPcm16StreamClip(context.state, context.fileSystem, wavPath, context.clip) != dng::audio::AudioStatus::Ok)
            {
                context.initFailureReason = "stream clip load failed";
                outReason = context.initFailureReason;
                return false;
            }
        }
        else
        {
            if (dng::audio::LoadWavPcm16Clip(context.state, context.fileSystem, wavPath, context.clip) != dng::audio::AudioStatus::Ok)
            {
                context.initFailureReason = "memory clip load failed";
                outReason = context.initFailureReason;
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
            context.initFailureReason = "play voice failed";
            outReason = context.initFailureReason;
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
            context.initFailureReason = "platform warmup mix failed";
            outReason = context.initFailureReason;
            return false;
        }

        context.initialized = true;
        context.initFailureReason = nullptr;
        return true;
    }
    bool Bench_AudioMixMemoryClipPlatform1024fStereo(int iterations,
                                                      volatile std::uint64_t& sink,
                                                      const char*& outReason) noexcept
    {
        outReason = nullptr;
        if (iterations < 0)
        {
            outReason = "iterations must be non-negative";
            return false;
        }

        static AudioPlatformBenchContext context{};
        if (!EnsureAudioPlatformBench(context, kBenchAudioMemPath, 4096u, false, outReason))
        {
            return false;
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
        return true;
    }

    bool Bench_AudioMixStreamClipPlatform1024fStereo(int iterations,
                                                      volatile std::uint64_t& sink,
                                                      const char*& outReason) noexcept
    {
        outReason = nullptr;
        if (iterations < 0)
        {
            outReason = "iterations must be non-negative";
            return false;
        }

        static AudioPlatformBenchContext context{};
        if (!EnsureAudioPlatformBench(context, kBenchAudioStreamPath, 48000u, true, outReason))
        {
            return false;
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
        return true;
    }

    BenchSample RunBenchmark(const Benchmark& bench, const BenchArgs& args)
    {
        BenchSample result{};
        result.name = std::string{bench.name};
        result.targetRsdPct = args.targetRsdPct;
        result.status = BenchStatus::Error;
        result.reason = "benchmark did not execute";
        result.bytesPerOp = -1.0;
        result.allocsPerOp = -1.0;

        if (bench.fn == nullptr)
        {
            result.reason = "benchmark function is null";
            return result;
        }

        if (bench.iterations <= 0)
        {
            result.reason = "benchmark iterations must be > 0";
            return result;
        }

        volatile std::uint64_t sink = 0;
        const char* probeReason = nullptr;
        if (!bench.fn(0, sink, probeReason))
        {
            result.status = BenchStatus::Skipped;
            result.reason = (probeReason != nullptr) ? probeReason : "benchmark probe failed";
            result.repeatsUsed = 0;
            result.nsPerOp = 0.0;
            result.rsdPct = 0.0;
            return result;
        }

        for (int i = 0; i < args.warmupCount; ++i)
        {
            const char* warmupReason = nullptr;
            if (!bench.fn(bench.iterations, sink, warmupReason))
            {
                result.status = BenchStatus::Skipped;
                result.reason = (warmupReason != nullptr) ? warmupReason : "benchmark warmup failed";
                result.repeatsUsed = 0;
                result.nsPerOp = 0.0;
                result.rsdPct = 0.0;
                return result;
            }
        }

        std::vector<double> nsSamples;
        nsSamples.reserve(static_cast<std::size_t>(args.maxRepeats));

        std::vector<double> bytesSamples;
        bytesSamples.reserve(static_cast<std::size_t>(args.maxRepeats));

        std::vector<double> allocSamples;
        allocSamples.reserve(static_cast<std::size_t>(args.maxRepeats));

        const double iterDenom = static_cast<double>(bench.iterations);

        for (int rep = 0; rep < args.maxRepeats; ++rep)
        {
            double repNsTotal = 0.0;
            double repBytesTotal = 0.0;
            double repAllocsTotal = 0.0;
            int repMemorySamples = 0;

            for (int batch = 0; batch < kBatchRuns; ++batch)
            {
                bool runOk = true;
                const char* runReason = nullptr;
                const auto benchResult = dng::bench::Run(bench.name.data(), 1u, [&]() noexcept {
                    runOk = bench.fn(bench.iterations, sink, runReason);
                });

                if (!runOk)
                {
                    result.status = BenchStatus::Skipped;
                    result.reason = (runReason != nullptr) ? runReason : "benchmark run failed";
                    result.repeatsUsed = rep;
                    result.nsPerOp = 0.0;
                    result.rsdPct = 0.0;
                    result.bytesPerOp = -1.0;
                    result.allocsPerOp = -1.0;
                    return result;
                }

                repNsTotal += (benchResult.NsPerOp / iterDenom);

                if (benchResult.HasMemoryStats())
                {
                    repBytesTotal += (benchResult.BytesPerOp / iterDenom);
                    repAllocsTotal += (benchResult.AllocsPerOp / iterDenom);
                    ++repMemorySamples;
                }
            }

            const double repNs = repNsTotal / static_cast<double>(kBatchRuns);
            nsSamples.push_back(repNs);

            if (repMemorySamples == kBatchRuns)
            {
                bytesSamples.push_back(repBytesTotal / static_cast<double>(kBatchRuns));
                allocSamples.push_back(repAllocsTotal / static_cast<double>(kBatchRuns));
            }

            const double meanNs = ComputeMean(nsSamples);
            const double rsdNs = ComputeRsdPct(nsSamples, meanNs);
            if (nsSamples.size() >= 2 && rsdNs <= args.targetRsdPct)
            {
                result.status = BenchStatus::Ok;
                result.reason.clear();
                result.repeatsUsed = rep + 1;
                result.nsPerOp = meanNs;
                result.rsdPct = rsdNs;
                result.bytesPerOp = bytesSamples.empty() ? -1.0 : ComputeMean(bytesSamples);
                result.allocsPerOp = allocSamples.empty() ? -1.0 : ComputeMean(allocSamples);
                return result;
            }
        }

        const double finalMeanNs = ComputeMean(nsSamples);
        const double finalRsdNs = ComputeRsdPct(nsSamples, finalMeanNs);
        result.nsPerOp = finalMeanNs;
        result.rsdPct = finalRsdNs;
        result.repeatsUsed = args.maxRepeats;
        result.bytesPerOp = bytesSamples.empty() ? -1.0 : ComputeMean(bytesSamples);
        result.allocsPerOp = allocSamples.empty() ? -1.0 : ComputeMean(allocSamples);

        if (nsSamples.size() >= 2 && finalRsdNs > args.targetRsdPct)
        {
            result.status = BenchStatus::Unstable;
            result.reason = "target RSD not reached";
            return result;
        }

        result.status = BenchStatus::Ok;
        result.reason.clear();
        return result;
    }

    std::string EscapeJson(const std::string& input)
    {
        std::string out;
        out.reserve(input.size() + 8);
        for (char c : input)
        {
            switch (c)
            {
                case '\\': out += "\\\\"; break;
                case '"':  out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:   out += c; break;
            }
        }
        return out;
    }

    BenchSummary BuildSummary(const std::vector<BenchSample>& samples)
    {
        BenchSummary summary{};
        for (const BenchSample& sample : samples)
        {
            switch (sample.status)
            {
                case BenchStatus::Ok:       ++summary.okCount; break;
                case BenchStatus::Skipped:  ++summary.skippedCount; break;
                case BenchStatus::Unstable: ++summary.unstableCount; break;
                case BenchStatus::Error:    ++summary.errorCount; break;
                default:                    ++summary.errorCount; break;
            }
        }
        return summary;
    }

    void WriteJson(const std::filesystem::path& outPath,
                   const std::vector<BenchSample>& samples,
                   const BenchArgs& args,
                   const BenchSummary& summary)
    {
        std::ofstream os(outPath.string(), std::ios::binary);
        if (!os)
        {
            std::fprintf(stderr, "Failed to open bench output file: %s\n", outPath.string().c_str());
            return;
        }

        os << "{\n";
        os << "  \"schemaVersion\": " << kBenchSchemaVersion << ",\n";
        os << "  \"benchmarks\": [\n";
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const auto& s = samples[i];
            os << "    {\n";
            os << "      \"name\": \"" << EscapeJson(s.name) << "\",\n";
            os << "      \"value\": " << std::fixed << std::setprecision(6) << s.nsPerOp << ",\n";
            os << "      \"rsdPct\": " << std::fixed << std::setprecision(6) << s.rsdPct << ",\n";
            os << "      \"bytesPerOp\": " << std::fixed << std::setprecision(6) << s.bytesPerOp << ",\n";
            os << "      \"allocsPerOp\": " << std::fixed << std::setprecision(6) << s.allocsPerOp << ",\n";
            os << "      \"status\": \"" << ToString(s.status) << "\",\n";
            os << "      \"reason\": \"" << EscapeJson(s.reason) << "\",\n";
            os << "      \"repeatsUsed\": " << s.repeatsUsed << ",\n";
            os << "      \"targetRsdPct\": " << std::fixed << std::setprecision(3) << s.targetRsdPct << "\n";
            os << "    }";
            if (i + 1 != samples.size()) { os << ","; }
            os << "\n";
        }
        os << "  ],\n";
        os << "  \"summary\": {\n";
        os << "    \"okCount\": " << summary.okCount << ",\n";
        os << "    \"skippedCount\": " << summary.skippedCount << ",\n";
        os << "    \"unstableCount\": " << summary.unstableCount << ",\n";
        os << "    \"errorCount\": " << summary.errorCount << "\n";
        os << "  },\n";
        os << "  \"metadata\": {\n";
        os << "    \"note\": \"BenchRunner v2\",\n";
        os << "    \"unit\": \"ns/op\",\n";
        os << "    \"warmup\": " << args.warmupCount << ",\n";
        os << "    \"targetRsdPct\": " << std::fixed << std::setprecision(3) << args.targetRsdPct << ",\n";
        os << "    \"maxRepeat\": " << args.maxRepeats << ",\n";
        os << "    \"iterations\": " << args.iterations << ",\n";
        os << "    \"strictStability\": " << (args.strictStability ? "true" : "false") << ",\n";
        const std::string sha = GetEnv("GITHUB_SHA");
        if (!sha.empty()) { os << "    \"gitSha\": \"" << EscapeJson(sha) << "\",\n"; }
        os << "    \"schemaVersion\": " << kBenchSchemaVersion << "\n";
        os << "  }\n";
        os << "}\n";
    }
}

int main(int argc, char** argv)
{
    BenchArgs args{};
    std::string parseError;
    if (!ParseArgs(argc, argv, args, parseError))
    {
        std::fprintf(stderr, "%s\n\n", parseError.c_str());
        PrintUsage();
        return 2;
    }

    if (args.showHelp)
    {
        PrintUsage();
        return 0;
    }

    if (args.cpuInfo)
    {
        PrintCpuInfo();
    }

    const std::string outPath = BuildOutputPath();
    const std::filesystem::path outFs{outPath};
    if (!EnsureParentDirectory(outFs))
    {
        std::fprintf(stderr, "Failed to create bench output directory: %s\n", outFs.parent_path().string().c_str());
        return 1;
    }

    const bool ownsMemorySystem = !dng::memory::MemorySystem::IsInitialized();
    if (ownsMemorySystem)
    {
        dng::memory::MemoryConfig memoryConfig{};
        memoryConfig.enable_tracking = true;
        dng::memory::MemorySystem::Init(memoryConfig);
    }

    const int baseIter = args.iterations;
    const int vecIter = args.iterations > 1 ? args.iterations / 2 : 1;
    const int memcpyIter = args.iterations > 1 ? args.iterations / 2 : 1;
    const int audioMixIter = args.iterations > 1000 ? args.iterations / 1000 : 1000;
    const int audioPlatformIter = args.iterations > 2000 ? args.iterations / 2000 : 500;

    const std::array<Benchmark, 6> benches = {{
        {"baseline_loop", baseIter, &Bench_BaselineLoop},
        {"vec3_dot", vecIter, &Bench_Vec3Dot},
        {"memcpy_64", memcpyIter, &Bench_Memcpy64},
        {"audio_mix_null_1024f_stereo", audioMixIter, &Bench_AudioMixNull1024fStereo},
        {"audio_mix_mem_clip_platform_1024f_stereo", audioPlatformIter, &Bench_AudioMixMemoryClipPlatform1024fStereo},
        {"audio_mix_stream_clip_platform_1024f_stereo", audioPlatformIter, &Bench_AudioMixStreamClipPlatform1024fStereo},
    }};

    std::vector<BenchSample> results;
    results.reserve(benches.size());

    for (const auto& bench : benches)
    {
        BenchSample sample = RunBenchmark(bench, args);
        std::printf("%s: %s value=%.6f rsd=%.3f repeats=%d",
                    sample.name.c_str(),
                    ToString(sample.status),
                    sample.nsPerOp,
                    sample.rsdPct,
                    sample.repeatsUsed);
        if (!sample.reason.empty())
        {
            std::printf(" reason=%s", sample.reason.c_str());
        }
        std::printf("\n");
        results.push_back(std::move(sample));
    }

    const BenchSummary summary = BuildSummary(results);
    WriteJson(outFs, results, args, summary);
    std::printf("BenchRunner wrote %s\n", outPath.c_str());

    if (ownsMemorySystem)
    {
        dng::memory::MemorySystem::Shutdown();
    }

    if (summary.errorCount > 0)
    {
        return 3;
    }

    if (args.strictStability && summary.unstableCount > 0)
    {
        return 2;
    }

    return 0;
}
