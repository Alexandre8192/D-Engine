/*
===============================================================================
D-Engine - tests/Test_BenchDriver.cpp  (BenchDriver v3)
-------------------------------------------------------------------------------
Purpose :
    Extend the benchmark harness with CSV export, Release baselines, associative
    container crossover sweeps, TLS multi-thread probes, and allocator probes.
Contract :
    - Single entry point guarded by DNG_BENCH_APP; emits CSV or human-readable
      output based on environment flags.
    - MemorySystem::Init/Shutdown wrap the run exactly once.
    - All benchmark bodies remain noexcept and leverage the DNG_BENCH macro.
Notes :
    - CSV mode omits banners so output can be redirected directly.
    - Probe scenarios are opt-in via DNG_ENABLE_BENCH_PROBE to limit overhead.
===============================================================================
*/

#include "Core/CoreMinimal.hpp"
#include "Core/Diagnostics/Bench.hpp"
#include "Core/Containers/StdAliases.hpp"
#include "Core/Containers/FlatMap.hpp"
#include "Core/Memory/MemorySystem.hpp"
#include "Core/Memory/ArenaAllocator.hpp"
#include "Core/Memory/SmallObjectAllocator.hpp"
#include "tests/BenchProbeAllocator.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iomanip>
#include <memory>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef __cpp_lib_print
    #include <print>
#else
    #include <iostream>
#endif

#if !defined(DNG_BENCH_APP)
#define DNG_BENCH_APP 0
#endif

#ifndef NDEBUG
    #define DNG_BENCH_BUILD_MODE "Debug"
#else
    #define DNG_BENCH_BUILD_MODE "Release"
#endif

namespace
{
    constexpr std::uint64_t kDefaultIterations = 1'000'000ULL;

    struct BenchFlags
    {
        bool Csv{ false };
        bool Summary{ false };
        bool Probe{ false };
    };

    using BenchVector = std::vector<::dng::bench::BenchResult>;

    inline void PrintLine(std::string_view s) noexcept
    {
    #ifdef __cpp_lib_print
        std::print("{}\n", s);
    #else
        std::cout << s << '\n';
    #endif
    }

    inline void PrintNote(const char* message) noexcept
    {
        if (!message)
            return;
    #ifdef __cpp_lib_print
        std::print("[Note] {}\n", message);
    #else
        std::cout << "[Note] " << message << '\n';
    #endif
    }

    inline void EmitCSV(const std::string& csv) noexcept
    {
    #ifdef __cpp_lib_print
        std::print("{}", csv);
    #else
        std::cout << csv;
    #endif
    }

    inline void PrintBenchLine(const ::dng::bench::BenchResult& result) noexcept
    {
        PrintLine(::dng::bench::ToString(result));
    }

    [[nodiscard]] bool EnvEnabled(const char* name) noexcept
    {
        if (!name)
            return false;

        const char* value = nullptr;

#if defined(_WIN32)
        char* buffer = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr)
            return false;
        std::unique_ptr<char, decltype(&std::free)> guard(buffer, &std::free);
        value = guard.get();
#else
        value = std::getenv(name);
#endif

        if (value)
        {
            if (*value == '\0')
                return true;
            if (value[0] == '0' && value[1] == '\0')
                return false;
            return true;
        }
        return false;
    }

    [[nodiscard]] BenchFlags DetectFlags() noexcept
    {
        BenchFlags flags{};
        flags.Csv = EnvEnabled("DNG_BENCH_CSV");
        flags.Summary = EnvEnabled("DNG_BENCH_PRINT_SUMMARY");
        flags.Probe = EnvEnabled("DNG_ENABLE_BENCH_PROBE");
        return flags;
    }

    std::vector<std::string>& LabelStore()
    {
        static std::vector<std::string> labels;
        return labels;
    }

    const char* MakeLabel(std::string label)
    {
        auto& storage = LabelStore();
        storage.push_back(std::move(label));
        return storage.back().c_str();
    }

    void EmitResult(const ::dng::bench::BenchResult& result, BenchVector& recorded, bool csvMode)
    {
        recorded.push_back(result);
        if (csvMode)
        {
            EmitCSV(::dng::bench::ToCSV(recorded.back()));
        }
        else
        {
            PrintBenchLine(recorded.back());
        }
    }

    [[nodiscard]] const ::dng::bench::BenchResult* FindResult(const BenchVector& recorded, const char* name) noexcept
    {
        for (const auto& result : recorded)
        {
            if (result.Name && std::string_view(result.Name) == name)
                return &result;
        }
        return nullptr;
    }

    void PrintSummary(const BenchVector& recorded, bool csvMode)
    {
        if (csvMode)
            return;

        static constexpr std::array<const char*, 6> kSummaryNames = {
            "Vector PushPop (reserved)",
            "Arena ScopedMarker (64B)",
            "Arena ScopedMarker (8x64B)",
            "SmallObject TLS Alloc/Free 64B",
            "DefaultAllocator Alloc/Free 64B",
            "TrackingAllocator Alloc/Free 64B"
        };

        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss << std::setprecision(3);
        oss << "[Summary] ";

        bool first = true;
        for (const char* name : kSummaryNames)
        {
            if (const auto* result = FindResult(recorded, name))
            {
                if (!first)
                    oss << " | ";
                first = false;
                oss << name << '=' << result->NsPerOp << " ns";
            }
        }

        if (!first)
            PrintLine(oss.str());
    }

    template<class Alloc>
    [[nodiscard]] void* AllocateCompat(Alloc* alloc, std::size_t size, std::size_t alignment) noexcept
    {
        DNG_CHECK(alloc != nullptr);

        if constexpr (requires(Alloc* a) { a->AllocateBytes(size, alignment); })
        {
            return alloc->AllocateBytes(size, alignment);
        }
        else
        {
            return alloc->Allocate(size, alignment);
        }
    }

    template<class Alloc>
    inline void DeallocateCompat(Alloc* alloc, void* ptr, std::size_t size, std::size_t alignment) noexcept
    {
        if (!alloc || !ptr)
            return;

        if constexpr (requires(Alloc* a) { a->DeallocateBytes(ptr, size, alignment); })
        {
            alloc->DeallocateBytes(ptr, size, alignment);
        }
        else
        {
            alloc->Deallocate(ptr, size, alignment);
        }
    }

    template<class T>
    struct TrackingAdapter
    {
        using value_type = T;

        TrackingAdapter() noexcept = default;
        template<class U> TrackingAdapter(const TrackingAdapter<U>&) noexcept {}

        [[nodiscard]] T* allocate(std::size_t count)
        {
            auto ref = ::dng::memory::MemorySystem::GetTrackingAllocator();
            auto* tracker = ref.Get();
            DNG_CHECK(tracker != nullptr);

            void* raw = AllocateCompat(tracker, count * sizeof(T), alignof(T));
            return static_cast<T*>(raw);
        }

        void deallocate(T* ptr, std::size_t count) noexcept
        {
            auto ref = ::dng::memory::MemorySystem::GetTrackingAllocator();
            auto* tracker = ref.Get();
            DeallocateCompat(tracker, ptr, count * sizeof(T), alignof(T));
        }

        template<class U> struct rebind { using other = TrackingAdapter<U>; };
        using propagate_on_container_swap = std::true_type;
        using is_always_equal = std::true_type;

        [[nodiscard]] bool operator==(const TrackingAdapter&) const noexcept { return true; }
        [[nodiscard]] bool operator!=(const TrackingAdapter&) const noexcept { return false; }
    };

    template<class T>
    using tracking_vector = std::vector<T, TrackingAdapter<T>>;

    template<class FlatMapLike, class MapLike>
    void BenchAssocCrossover(std::initializer_list<int> sizes,
                             BenchVector& recorded,
                             bool csvMode)
    {
        std::mt19937 rng(1337u);

        for (int n : sizes)
        {
            std::vector<int> keys(static_cast<std::size_t>(n));
            std::uniform_int_distribution<int> dist(0, n * 4);
            std::generate(keys.begin(), keys.end(), [&]() { return dist(rng); });

            const char* flatInsertLabel = MakeLabel("FlatMap insert_or_assign (N=" + std::to_string(n) + ")");
            auto flatInsert = DNG_BENCH(flatInsertLabel, 1, [&]() noexcept {
                FlatMapLike flat;
                for (int key : keys)
                    flat.insert_or_assign(key, key * 3);
            });
            EmitResult(flatInsert, recorded, csvMode);

            const char* mapInsertLabel = MakeLabel("std::map  operator[]       (N=" + std::to_string(n) + ")");
            auto mapInsert = DNG_BENCH(mapInsertLabel, 1, [&]() noexcept {
                MapLike tree;
                for (int key : keys)
                    tree[key] = key * 3;
            });
            EmitResult(mapInsert, recorded, csvMode);

            FlatMapLike flatBaseline;
            MapLike mapBaseline;
            for (int key : keys)
            {
                flatBaseline.insert_or_assign(key, key * 2);
                mapBaseline[key] = key * 2;
            }

            const char* flatFindLabel = MakeLabel("FlatMap find (N=" + std::to_string(n) + ")");
            auto flatFind = DNG_BENCH(flatFindLabel, 1, [&]() noexcept {
                for (int key : keys)
                {
                    auto it = flatBaseline.find(key);
                    DNG_CHECK(it != flatBaseline.end());
                    ::dng::bench::detail::Blackhole(it != flatBaseline.end());
                }
            });
            EmitResult(flatFind, recorded, csvMode);

            const char* mapFindLabel = MakeLabel("std::map  find             (N=" + std::to_string(n) + ")");
            auto mapFind = DNG_BENCH(mapFindLabel, 1, [&]() noexcept {
                for (int key : keys)
                {
                    auto it = mapBaseline.find(key);
                    DNG_CHECK(it != mapBaseline.end());
                    ::dng::bench::detail::Blackhole(it != mapBaseline.end());
                }
            });
            EmitResult(mapFind, recorded, csvMode);
        }
    }

    struct MTOutcome
    {
        double NsPerOp{};
        double OpsPerSecond{};
        std::uint64_t TotalOps{};
    };

    MTOutcome RunSmallObjectTLSMultiThread(::dng::core::SmallObjectAllocator& allocator,
                                           std::uint32_t threadCount,
                                           std::uint64_t iterationsPerThread)
    {
        using Clock = std::chrono::steady_clock;

        std::atomic<int> stage{0};
        Clock::time_point start{};
        Clock::time_point end{};

        std::barrier sync(static_cast<std::ptrdiff_t>(threadCount) + 1,
            [&]() noexcept
            {
                if (stage.fetch_add(1, std::memory_order_relaxed) == 0)
                    start = Clock::now();
                else
                    end = Clock::now();
            });

        auto worker = [&]() noexcept
        {
            sync.arrive_and_wait();
            for (std::uint64_t i = 0; i < iterationsPerThread; ++i)
            {
                void* ptr = allocator.Allocate(64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
                allocator.Deallocate(ptr, 64u, alignof(std::max_align_t));
            }
            sync.arrive_and_wait();
        };

        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        for (std::uint32_t t = 0; t < threadCount; ++t)
            threads.emplace_back(worker);

        sync.arrive_and_wait();
        sync.arrive_and_wait();

        for (auto& thread : threads)
            thread.join();

        const double totalNs = std::chrono::duration<double, std::nano>(end - start).count();
        const double totalOps = static_cast<double>(threadCount) * static_cast<double>(iterationsPerThread);
        const double nsPerOp = totalNs / totalOps;
        const double opsPerSec = (totalOps * 1'000'000'000.0) / totalNs;

        MTOutcome outcome{};
        outcome.NsPerOp = nsPerOp;
        outcome.OpsPerSecond = opsPerSec;
        outcome.TotalOps = static_cast<std::uint64_t>(totalOps);
        return outcome;
    }

    std::string FormatIterationsTag(std::uint64_t iterations) noexcept
    {
        if (iterations == 1'000'000ULL)
            return "1e6";
        if (iterations == 10'000'000ULL)
            return "1e7";
        return std::to_string(iterations);
    }

    void EmitMTResult(std::uint32_t threads,
                      std::uint64_t iterationsPerThread,
                      const MTOutcome& outcome,
                      BenchVector& recorded,
                      bool csvMode)
    {
        std::string label = "[MT] SmallObject TLS 64B (T=" + std::to_string(threads) + ", K=" +
            FormatIterationsTag(iterationsPerThread) + ")";
        const char* storedLabel = MakeLabel(label);

        ::dng::bench::BenchResult result{};
        result.Name = storedLabel;
        result.Iterations = outcome.TotalOps;
        result.NsPerOp = outcome.NsPerOp;
        result.BytesPerOp = -1.0;
        result.AllocsPerOp = -1.0;
        recorded.push_back(result);

        if (csvMode)
        {
            EmitCSV(::dng::bench::ToCSV(recorded.back()));
        }
        else
        {
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss << std::setprecision(3);
            oss << label << ":  ns/op=" << outcome.NsPerOp
                << "  ops/s=" << outcome.OpsPerSecond;
            PrintLine(oss.str());
        }
    }

} // namespace

#if DNG_BENCH_APP

int main()
{
    using namespace ::dng;

    const BenchFlags flags = DetectFlags();

    if (!flags.Csv)
    {
    #ifdef __cpp_lib_print
        std::print("[BenchDriver v3] build={} {} {}\n", DNG_BENCH_BUILD_MODE, __DATE__, __TIME__);
    #else
        std::cout << "[BenchDriver v3] build=" << DNG_BENCH_BUILD_MODE
                  << " " << __DATE__ << " " << __TIME__ << '\n';
    #endif

        std::string flagLine = "[Flags]";
        if (flags.Summary) flagLine += " DNG_BENCH_PRINT_SUMMARY=1";
        if (flags.Probe)   flagLine += " DNG_ENABLE_BENCH_PROBE=1";
        if (flagLine == "[Flags]")
            flagLine += " (none)";
        PrintLine(flagLine);
    }

    memory::MemorySystem::Init();

    const auto defaultRef  = memory::MemorySystem::GetDefaultAllocator();
    const auto trackingRef = memory::MemorySystem::GetTrackingAllocator();

    auto* defaultAlloc = defaultRef.Get();
    auto* tracking     = trackingRef.Get();

    if (!tracking && !flags.Csv)
        PrintNote("TrackingAllocator unavailable: Bench results will show <tracking-off> for churn metrics.");

    BenchVector recorded;
    recorded.reserve(128);

    {
        vector<int> values;
        std::uint32_t counter = 0;

        auto result = DNG_BENCH("Vector PushPop (no reserve)", kDefaultIterations, [&]() noexcept {
            values.push_back(static_cast<int>(counter++));
            values.pop_back();
        });

        EmitResult(result, recorded, flags.Csv);
    }

    {
        vector<int> values;
        values.reserve(static_cast<std::size_t>(kDefaultIterations) + 1);
        std::uint32_t counter = 0;

        auto result = DNG_BENCH("Vector PushPop (reserved)", kDefaultIterations, [&]() noexcept {
            values.push_back(static_cast<int>(counter++));
            values.pop_back();
        });

        EmitResult(result, recorded, flags.Csv);
    }

    if (defaultAlloc)
    {
        ::dng::core::SmallObjectConfig soCfg{};
        ::dng::core::SmallObjectAllocator smallAlloc{ defaultAlloc, soCfg };

        auto result = DNG_BENCH("SmallObject TLS Alloc/Free 64B", kDefaultIterations, [&]() noexcept {
            void* ptr = smallAlloc.Allocate(64u, alignof(std::max_align_t));
            DNG_CHECK(ptr != nullptr);
            smallAlloc.Deallocate(ptr, 64u, alignof(std::max_align_t));
        });

        EmitResult(result, recorded, flags.Csv);
    }
    else if (!flags.Csv)
    {
        PrintNote("Default allocator unavailable; skipping small-object scenario.");
    }

    if (defaultAlloc)
    {
        ::dng::core::ArenaAllocator arena{ defaultAlloc, 8u * 1024u * 1024u };

        auto result = DNG_BENCH("Arena ScopedMarker (64B)", kDefaultIterations, [&]() noexcept {
            ::dng::core::ArenaAllocator::ScopedMarker scope{ arena };
            void* ptr = arena.Allocate(64u, alignof(std::max_align_t));
            DNG_CHECK(ptr != nullptr);
        });

        EmitResult(result, recorded, flags.Csv);
    }
    else if (!flags.Csv)
    {
        PrintNote("Default allocator unavailable; skipping arena scenario.");
    }

    if (defaultAlloc)
    {
        ::dng::core::ArenaAllocator arena{ defaultAlloc, 8u * 1024u * 1024u };

        auto result = DNG_BENCH("Arena ScopedMarker (8x64B)", kDefaultIterations, [&]() noexcept {
            ::dng::core::ArenaAllocator::ScopedMarker scope{ arena };
            for (int i = 0; i < 8; ++i)
            {
                void* ptr = arena.Allocate(64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
            }
        });

        EmitResult(result, recorded, flags.Csv);
    }
    else if (!flags.Csv)
    {
        PrintNote("Default allocator unavailable; skipping arena bulk scenario.");
    }

    if (defaultAlloc)
    {
        auto result = DNG_BENCH("DefaultAllocator Alloc/Free 64B", kDefaultIterations, [&]() noexcept {
            void* ptr = AllocateCompat(defaultAlloc, 64u, alignof(std::max_align_t));
            DNG_CHECK(ptr != nullptr);
            DeallocateCompat(defaultAlloc, ptr, 64u, alignof(std::max_align_t));
        });

        EmitResult(result, recorded, flags.Csv);
    }
    else if (!flags.Csv)
    {
        PrintNote("Default allocator unavailable; skipping default alloc/free scenario.");
    }

    if (tracking)
    {
        auto result = DNG_BENCH("TrackingAllocator Alloc/Free 64B", kDefaultIterations, [&]() noexcept {
            void* ptr = AllocateCompat(tracking, 64u, alignof(std::max_align_t));
            DNG_CHECK(ptr != nullptr);
            DeallocateCompat(tracking, ptr, 64u, alignof(std::max_align_t));
        });

        EmitResult(result, recorded, flags.Csv);
    }
    else if (!flags.Csv)
    {
        PrintNote("Tracking allocator unavailable; skipping direct allocation scenario.");
    }

    if (tracking)
    {
        {
            tracking_vector<int> trackedValues;
            std::uint32_t counter = 0;

            auto result = DNG_BENCH("tracking_vector PushPop (no reserve)", kDefaultIterations, [&]() noexcept {
                trackedValues.push_back(static_cast<int>(counter++));
                trackedValues.pop_back();
            });
            EmitResult(result, recorded, flags.Csv);
        }

        {
            tracking_vector<int> trackedValues;
            trackedValues.reserve(static_cast<std::size_t>(kDefaultIterations) + 1);
            std::uint32_t counter = 0;

            auto result = DNG_BENCH("tracking_vector PushPop (reserved)", kDefaultIterations, [&]() noexcept {
                trackedValues.push_back(static_cast<int>(counter++));
                trackedValues.pop_back();
            });
            EmitResult(result, recorded, flags.Csv);
        }
    }
    else if (!flags.Csv)
    {
        PrintNote("Tracking allocator unavailable; skipping tracking_vector scenarios.");
    }

    BenchAssocCrossover<::dng::core::FlatMap<int, int>, std::map<int, int>>({8, 16, 32, 64, 128}, recorded, flags.Csv);

    if (defaultAlloc)
    {
        ::dng::core::SmallObjectConfig soCfg{};
        ::dng::core::SmallObjectAllocator tlsAllocator{ defaultAlloc, soCfg };

    #ifdef NDEBUG
        constexpr std::uint64_t kMTIterations = 10'000'000ULL;
    #else
        constexpr std::uint64_t kMTIterations = 1'000'000ULL;
    #endif

        for (std::uint32_t threads : {1u, 2u, 4u, 8u})
        {
            auto outcome = RunSmallObjectTLSMultiThread(tlsAllocator, threads, kMTIterations);
            EmitMTResult(threads, kMTIterations, outcome, recorded, flags.Csv);
        }
    }
    else if (!flags.Csv)
    {
        PrintNote("Default allocator unavailable; skipping TLS multi-thread probe.");
    }

    if (flags.Probe && defaultAlloc)
    {
        dng::tests::BenchProbeAllocator defaultProbe(defaultAlloc);
        auto result = DNG_BENCH("Probe DefaultAllocator Alloc/Free 64B", kDefaultIterations, [&]() noexcept {
            void* ptr = AllocateCompat(&defaultProbe, 64u, alignof(std::max_align_t));
            DNG_CHECK(ptr != nullptr);
            DeallocateCompat(&defaultProbe, ptr, 64u, alignof(std::max_align_t));
        });
        auto after = defaultProbe.CaptureMonotonic();
        const double denom = static_cast<double>(result.Iterations);
        result.BytesPerOp = static_cast<double>(after.TotalBytesAllocated) / denom;
        result.AllocsPerOp = static_cast<double>(after.TotalAllocCalls) / denom;
        EmitResult(result, recorded, flags.Csv);

        ::dng::core::ArenaAllocator arena{ defaultAlloc, 8u * 1024u * 1024u };
        dng::tests::BenchProbeAllocator arenaProbe(&arena);
        auto arenaResult = DNG_BENCH("Probe Arena ScopedMarker (8x64B)", kDefaultIterations, [&]() noexcept {
            ::dng::core::ArenaAllocator::ScopedMarker scope{ arena };
            for (int i = 0; i < 8; ++i)
            {
                void* ptr = arena.Allocate(64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
            }
        });
        auto arenaAfter = arenaProbe.CaptureMonotonic();
        const double denomArena = static_cast<double>(arenaResult.Iterations);
        arenaResult.BytesPerOp = static_cast<double>(arenaAfter.TotalBytesAllocated) / denomArena;
        arenaResult.AllocsPerOp = static_cast<double>(arenaAfter.TotalAllocCalls) / denomArena;
        EmitResult(arenaResult, recorded, flags.Csv);
    }
    else if (flags.Probe && !defaultAlloc && !flags.Csv)
    {
        PrintNote("Bench probe scenarios skipped: default allocator unavailable.");
    }

    PrintSummary(recorded, flags.Csv);

    memory::MemorySystem::Shutdown();
    return 0;
}

#endif // DNG_BENCH_APP
