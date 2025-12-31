// ============================================================================
// D-Engine - tests/BenchRunner/BenchRunner_main.cpp
// ----------------------------------------------------------------------------
// Purpose : Minimal BenchRunner harness (Bench M0) to emit basic performance metrics.
// Notes   : No exceptions/RTTI. Small registry of microbenches with steady_clock timing.
// ============================================================================

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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
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

    const std::array<Benchmark, 3> benches = {{
        {"baseline_loop", baseIter, 0.0, 0.0, &Bench_BaselineLoop},
        {"vec3_dot", vecIter, 12.0, 0.0, &Bench_Vec3Dot},
        {"memcpy_64", memcpyIter, 512.0, 0.0, &Bench_Memcpy64},
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
