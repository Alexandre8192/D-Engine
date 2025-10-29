#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
/*
===============================================================================
D-Engine - tests/Test_BenchDriver.cpp  (BenchDriver v2)
Purpose :
    Exercise Core/Diagnostics/Bench.hpp with representative allocator-centric
    scenarios to validate timing, warm-up, auto-scaling, and monotonic churn
    reporting.
Contract :
    - Single-threaded console driver; each benchmark body is a `noexcept`
      lambda.
    - MemorySystem::Init() and Shutdown() are invoked exactly once.
    - Release|x64 provides stable timings; Debug builds are for smoke only.
Notes :
    - When TrackingAllocator is unavailable, BenchResult prints "<tracking-off>".
    - Iteration hints remain adjustable, but the harness now scales toward
      ~250 ms total per scenario automatically.
===============================================================================
*/

#include "Core/CoreMinimal.hpp"            // Platform glue, logging, types
#include "Core/Diagnostics/Bench.hpp"      // dng::bench::{DNG_BENCH, BenchResult, ToString}
#include "Core/Containers/StdAliases.hpp"  // dng::vector alias (AllocatorAdapter)
#include "Core/Memory/MemorySystem.hpp"    // MemorySystem lifecycle façade
#include "Core/Memory/ArenaAllocator.hpp"  // dng::core::ArenaAllocator contract
#include "Core/Memory/SmallObjectAllocator.hpp" // dng::core::SmallObjectAllocator (for batch sweep)

#include <cstddef>      // std::max_align_t, std::size_t
#include <cstdint>      // std::uint32_t, std::uint64_t
#include <type_traits>  // std::true_type, std::false_type
#include <vector>       // std::vector for tracking_vector alias
#include <string_view>  // std::string_view
#include <string>       // std::string
#include <ctime>        // std::time_t, std::tm, std::gmtime
#include <cstdio>       // std::FILE, std::fopen, std::fprintf, std::fclose
#include <cstdlib>      // std::getenv, std::strtol
#include <functional>   // std::function
#include <algorithm>    // std::min_element, std::max_element, std::sort
#include <numeric>      // std::accumulate
#include <cmath>        // std::sqrt, std::abs

#ifdef __cpp_lib_print
    #include <print>
#else
    #include <iostream>
#endif

// --- Build banner to confirm the right binary is running ---------------------
#ifndef NDEBUG
    #define DNG_BENCH_BUILD_MODE "Debug"
#else
    #define DNG_BENCH_BUILD_MODE "Release"
#endif

namespace
{
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

    inline void PrintBenchLine(const ::dng::bench::BenchResult& result) noexcept
    {
    #ifdef __cpp_lib_print
        std::print("{}\n", ::dng::bench::ToString(result));
    #else
        std::cout << ::dng::bench::ToString(result) << '\n';
    #endif
    }

    // Build environment-driven suffixes for metric names to support param sweeps.
    // Example: [sampling=8, shards=8] or [batch=128]
    inline std::string TrackingSuffixFromEnv() noexcept
    {
    #if defined(_MSC_VER)
    #   pragma warning(push)
    #   pragma warning(disable:4996)
    #endif
        const char* s = std::getenv("DNG_MEM_TRACKING_SAMPLING_RATE");
        const char* h = std::getenv("DNG_MEM_TRACKING_SHARDS");
    #if defined(_MSC_VER)
    #   pragma warning(pop)
    #endif
        std::string tag;
        if ((s && *s) || (h && *h))
        {
            tag += " [";
            bool first = true;
            if (s && *s) { tag += "sampling="; tag += s; first = false; }
            if (h && *h) { if (!first) tag += ", "; tag += "shards="; tag += h; }
            tag += "]";
        }
        return tag;
    }

    inline std::string SoaBatchSuffixFromEnv() noexcept
    {
    #if defined(_MSC_VER)
    #   pragma warning(push)
    #   pragma warning(disable:4996)
    #endif
        const char* b = std::getenv("DNG_SOALLOC_BATCH");
    #if defined(_MSC_VER)
    #   pragma warning(pop)
    #endif
        std::string tag;
        if (b && *b)
        {
            tag += " [batch=";
            tag += b;
            tag += "]";
        }
        return tag;
    }

    // --- Allocation adapters -------------------------------------------------

    // Adapts Allocate/Deallocate naming differences across allocators.
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

    // std::vector adapter that forwards storage to the global TrackingAllocator.
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

} // namespace

// -----------------------------------------------------------------------------

int main(int argc, char** argv)
{
    using namespace ::dng;

#ifdef __cpp_lib_print
    std::print("[BenchDriver v2] build={} {} {}\n", DNG_BENCH_BUILD_MODE, __DATE__, __TIME__);
#else
    std::cout << "[BenchDriver v2] build=" << DNG_BENCH_BUILD_MODE
              << " " << __DATE__ << " " << __TIME__ << '\n';
#endif

    memory::MemorySystem::Init();

    const auto defaultRef  = memory::MemorySystem::GetDefaultAllocator();
    const auto trackingRef = memory::MemorySystem::GetTrackingAllocator();

    auto* defaultAlloc = defaultRef.Get();
    auto* tracking     = trackingRef.Get();

    if (!tracking)
    {
        PrintNote("TrackingAllocator unavailable: Bench results will show <tracking-off> for churn metrics.");
    }

    // You can bump iterations to target ~200ms per scenario in Release builds.
    constexpr std::uint64_t kIterations = 1'000'000; // Initial hint; Run() rescales toward ~250 ms.

    // CLI flags for stable runs
    int warmupCount = 0;
    int repeatCount = 1;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--warmup" && (i + 1) < argc)
        {
            warmupCount = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
            if (warmupCount < 0) warmupCount = 0;
        }
        else if (arg == "--repeat" && (i + 1) < argc)
        {
            repeatCount = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
            if (repeatCount < 1) repeatCount = 1;
        }
    }

    struct Agg
    {
        std::vector<double> ns;
        double bytesPerOp = -1.0;
        double allocsPerOp = -1.0;
        bool bytesWarned = false;
        bool allocsWarned = false;
    };

    std::vector<std::string> order; // preserve insertion order
    // name -> aggregate
    std::vector<std::pair<std::string, Agg>> aggregates; // use vector for deterministic order

    auto find_or_add = [&](const char* name) -> Agg& {
        for (auto& p : aggregates)
        {
            if (p.first == name)
                return p.second;
        }
        order.emplace_back(name ? name : std::string{"<unnamed>"});
        aggregates.emplace_back(order.back(), Agg{});
        return aggregates.back().second;
    };

    // ---- Scenario 1: dng::vector push/pop without reserve -------------------
    {
        const std::string metricName = std::string{"Vector PushPop (no reserve)"};
        auto runOnce = [&]() noexcept -> bench::BenchResult {
            vector<int> values;
            std::uint32_t counter = 0;
            return DNG_BENCH(metricName.c_str(), kIterations, [&]() noexcept {
                values.push_back(static_cast<int>(counter++));
                values.pop_back();
            });
        };
        for (int i = 0; i < warmupCount; ++i) { (void)runOnce(); }
        for (int i = 0; i < repeatCount; ++i)
        {
            auto r = runOnce();
            PrintBenchLine(r);
            auto& agg = find_or_add(r.Name);
            agg.ns.push_back(r.NsPerOp);
            if (r.BytesPerOp >= 0.0)
            {
                if (agg.bytesPerOp < 0.0) agg.bytesPerOp = r.BytesPerOp;
                else if (!agg.bytesWarned && std::abs(agg.bytesPerOp - r.BytesPerOp) > 1e-9)
                {
                    PrintNote("[WARN] bytesPerOp mismatch across repeats for Vector PushPop (no reserve)");
                    agg.bytesWarned = true;
                }
            }
            if (r.AllocsPerOp >= 0.0)
            {
                if (agg.allocsPerOp < 0.0) agg.allocsPerOp = r.AllocsPerOp;
                else if (!agg.allocsWarned && std::abs(agg.allocsPerOp - r.AllocsPerOp) > 1e-9)
                {
                    PrintNote("[WARN] allocsPerOp mismatch across repeats for Vector PushPop (no reserve)");
                    agg.allocsWarned = true;
                }
            }
        }
        // No analytical churn printed: growth strategy is implementation dependent.
    }

    // ---- Scenario 2: dng::vector push/pop with reserve ----------------------
    {
        const std::string metricName = std::string{"Vector PushPop (reserved)"};
        auto runOnce = [&]() noexcept -> bench::BenchResult {
            vector<int> values;
            values.reserve(static_cast<std::size_t>(kIterations) + 1);
            std::uint32_t counter = 0;
            return DNG_BENCH(metricName.c_str(), kIterations, [&]() noexcept {
                values.push_back(static_cast<int>(counter++));
                values.pop_back();
            });
        };
        for (int i = 0; i < warmupCount; ++i) { (void)runOnce(); }
        for (int i = 0; i < repeatCount; ++i)
        {
            auto r = runOnce();
            PrintBenchLine(r);
            auto& agg = find_or_add(r.Name);
            agg.ns.push_back(r.NsPerOp);
            if (r.BytesPerOp >= 0.0)
            {
                if (agg.bytesPerOp < 0.0) agg.bytesPerOp = r.BytesPerOp;
                else if (!agg.bytesWarned && std::abs(agg.bytesPerOp - r.BytesPerOp) > 1e-9)
                {
                    PrintNote("[WARN] bytesPerOp mismatch across repeats for Vector PushPop (reserved)");
                    agg.bytesWarned = true;
                }
            }
            if (r.AllocsPerOp >= 0.0)
            {
                if (agg.allocsPerOp < 0.0) agg.allocsPerOp = r.AllocsPerOp;
                else if (!agg.allocsWarned && std::abs(agg.allocsPerOp - r.AllocsPerOp) > 1e-9)
                {
                    PrintNote("[WARN] allocsPerOp mismatch across repeats for Vector PushPop (reserved)");
                    agg.allocsWarned = true;
                }
            }
        }
    }

    // ---- Scenario 3: Arena allocate/rewind (64 bytes) -----------------------
    if (defaultAlloc)
    {
        const std::string metricName = std::string{"Arena Allocate/Rewind (64B)"};
        auto runOnce = [&]() noexcept -> bench::BenchResult {
            ::dng::core::ArenaAllocator arena{ defaultAlloc, 8u * 1024u * 1024u };
            return DNG_BENCH(metricName.c_str(), kIterations, [&]() noexcept {
                const auto marker = arena.GetMarker();
                void* ptr = arena.Allocate(64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
                arena.Rewind(marker);
            });
        };
        for (int i = 0; i < warmupCount; ++i) { (void)runOnce(); }
        for (int i = 0; i < repeatCount; ++i)
        {
            auto r = runOnce();
            PrintBenchLine(r);
            auto& agg = find_or_add(r.Name);
            agg.ns.push_back(r.NsPerOp);
            if (r.BytesPerOp >= 0.0)
            {
                if (agg.bytesPerOp < 0.0) agg.bytesPerOp = r.BytesPerOp;
                else if (!agg.bytesWarned && std::abs(agg.bytesPerOp - r.BytesPerOp) > 1e-9)
                {
                    PrintNote("[WARN] bytesPerOp mismatch across repeats for Arena Allocate/Rewind (64B)");
                    agg.bytesWarned = true;
                }
            }
            if (r.AllocsPerOp >= 0.0)
            {
                if (agg.allocsPerOp < 0.0) agg.allocsPerOp = r.AllocsPerOp;
                else if (!agg.allocsWarned && std::abs(agg.allocsPerOp - r.AllocsPerOp) > 1e-9)
                {
                    PrintNote("[WARN] allocsPerOp mismatch across repeats for Arena Allocate/Rewind (64B)");
                    agg.allocsWarned = true;
                }
            }
        }
    }
    else
    {
        PrintNote("Default allocator unavailable; skipping arena scenario.");
    }

    // ---- Scenario 3b: Arena bulk allocate + rewind -------------------------
    if (defaultAlloc)
    {
        const std::string metricName = std::string{"Arena Bulk Allocate/Rewind (8x64B)"};
        auto runOnce = [&]() noexcept -> bench::BenchResult {
            ::dng::core::ArenaAllocator arena{ defaultAlloc, 8u * 1024u * 1024u };
            return DNG_BENCH(metricName.c_str(), kIterations, [&]() noexcept {
                const auto marker = arena.GetMarker();
                for (int i = 0; i < 8; ++i)
                {
                    void* ptr = arena.Allocate(64u, alignof(std::max_align_t));
                    DNG_CHECK(ptr != nullptr);
                }
                arena.Rewind(marker);
            });
        };
        for (int i = 0; i < warmupCount; ++i) { (void)runOnce(); }
        for (int i = 0; i < repeatCount; ++i)
        {
            auto r = runOnce();
            PrintBenchLine(r);
            auto& agg = find_or_add(r.Name);
            agg.ns.push_back(r.NsPerOp);
            if (r.BytesPerOp >= 0.0)
            {
                if (agg.bytesPerOp < 0.0) agg.bytesPerOp = r.BytesPerOp;
                else if (!agg.bytesWarned && std::abs(agg.bytesPerOp - r.BytesPerOp) > 1e-9)
                {
                    PrintNote("[WARN] bytesPerOp mismatch across repeats for Arena Bulk Allocate/Rewind (8x64B)");
                    agg.bytesWarned = true;
                }
            }
            if (r.AllocsPerOp >= 0.0)
            {
                if (agg.allocsPerOp < 0.0) agg.allocsPerOp = r.AllocsPerOp;
                else if (!agg.allocsWarned && std::abs(agg.allocsPerOp - r.AllocsPerOp) > 1e-9)
                {
                    PrintNote("[WARN] allocsPerOp mismatch across repeats for Arena Bulk Allocate/Rewind (8x64B)");
                    agg.allocsWarned = true;
                }
            }
        }
    }

    // ---- Scenario 4: DefaultAllocator direct alloc/free (64 bytes) ----------
    if (defaultAlloc)
    {
        const std::string metricName = std::string{"DefaultAllocator Alloc/Free 64B"};
        auto runOnce = [&]() noexcept -> bench::BenchResult {
            return DNG_BENCH(metricName.c_str(), kIterations, [&]() noexcept {
                void* ptr = AllocateCompat(defaultAlloc, 64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
                DeallocateCompat(defaultAlloc, ptr, 64u, alignof(std::max_align_t));
            });
        };
        for (int i = 0; i < warmupCount; ++i) { (void)runOnce(); }
        for (int i = 0; i < repeatCount; ++i)
        {
            auto r = runOnce();
            PrintBenchLine(r);
            auto& agg = find_or_add(r.Name);
            agg.ns.push_back(r.NsPerOp);
            if (r.BytesPerOp >= 0.0)
            {
                if (agg.bytesPerOp < 0.0) agg.bytesPerOp = r.BytesPerOp;
                else if (!agg.bytesWarned && std::abs(agg.bytesPerOp - r.BytesPerOp) > 1e-9)
                {
                    PrintNote("[WARN] bytesPerOp mismatch across repeats for DefaultAllocator Alloc/Free 64B)");
                    agg.bytesWarned = true;
                }
            }
            if (r.AllocsPerOp >= 0.0)
            {
                if (agg.allocsPerOp < 0.0) agg.allocsPerOp = r.AllocsPerOp;
                else if (!agg.allocsWarned && std::abs(agg.allocsPerOp - r.AllocsPerOp) > 1e-9)
                {
                    PrintNote("[WARN] allocsPerOp mismatch across repeats for DefaultAllocator Alloc/Free 64B)");
                    agg.allocsWarned = true;
                }
            }
        }
    }
    else
    {
        PrintNote("Default allocator unavailable; skipping default alloc/free scenario.");
    }

    // ---- Scenario 5: TrackingAllocator direct alloc/free (64 bytes) ---------
    if (tracking)
    {
        const std::string metricName = std::string{"TrackingAllocator Alloc/Free 64B"} + TrackingSuffixFromEnv();
        auto runOnce = [&]() noexcept -> bench::BenchResult {
            return DNG_BENCH(metricName.c_str(), kIterations, [&]() noexcept {
                void* ptr = AllocateCompat(tracking, 64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
                DeallocateCompat(tracking, ptr, 64u, alignof(std::max_align_t));
            });
        };
        for (int i = 0; i < warmupCount; ++i) { (void)runOnce(); }
        for (int i = 0; i < repeatCount; ++i)
        {
            auto r = runOnce();
            PrintBenchLine(r);
            auto& agg = find_or_add(r.Name);
            agg.ns.push_back(r.NsPerOp);
            if (r.BytesPerOp >= 0.0)
            {
                if (agg.bytesPerOp < 0.0) agg.bytesPerOp = r.BytesPerOp;
                else if (!agg.bytesWarned && std::abs(agg.bytesPerOp - r.BytesPerOp) > 1e-9)
                {
                    PrintNote("[WARN] bytesPerOp mismatch across repeats for TrackingAllocator Alloc/Free 64B)");
                    agg.bytesWarned = true;
                }
            }
            if (r.AllocsPerOp >= 0.0)
            {
                if (agg.allocsPerOp < 0.0) agg.allocsPerOp = r.AllocsPerOp;
                else if (!agg.allocsWarned && std::abs(agg.allocsPerOp - r.AllocsPerOp) > 1e-9)
                {
                    PrintNote("[WARN] allocsPerOp mismatch across repeats for TrackingAllocator Alloc/Free 64B)");
                    agg.allocsWarned = true;
                }
            }
        }
    }
    else
    {
        PrintNote("Tracking allocator unavailable; skipping direct allocation scenario.");
    }

    // ---- Scenario 6: tracking_vector push/pop (no reserve / reserved) -------
    if (tracking)
    {
        // 6a) No reserve: growth-dependent → no analytical expected printed.
        {
            const std::string metricName = std::string{"tracking_vector PushPop (no reserve)"} + TrackingSuffixFromEnv();
            auto runOnce = [&]() noexcept -> bench::BenchResult {
                tracking_vector<int> trackedValues;
                std::uint32_t counter = 0;
                return DNG_BENCH(metricName.c_str(), kIterations, [&]() noexcept {
                    trackedValues.push_back(static_cast<int>(counter++));
                    trackedValues.pop_back();
                });
            };
            for (int i = 0; i < warmupCount; ++i) { (void)runOnce(); }
            for (int i = 0; i < repeatCount; ++i)
            {
                auto r = runOnce();
                PrintBenchLine(r);
                auto& agg = find_or_add(r.Name);
                agg.ns.push_back(r.NsPerOp);
                if (r.BytesPerOp >= 0.0)
                {
                    if (agg.bytesPerOp < 0.0) agg.bytesPerOp = r.BytesPerOp;
                    else if (!agg.bytesWarned && std::abs(agg.bytesPerOp - r.BytesPerOp) > 1e-9)
                    {
                        PrintNote("[WARN] bytesPerOp mismatch across repeats for tracking_vector PushPop (no reserve)");
                        agg.bytesWarned = true;
                    }
                }
                if (r.AllocsPerOp >= 0.0)
                {
                    if (agg.allocsPerOp < 0.0) agg.allocsPerOp = r.AllocsPerOp;
                    else if (!agg.allocsWarned && std::abs(agg.allocsPerOp - r.AllocsPerOp) > 1e-9)
                    {
                        PrintNote("[WARN] allocsPerOp mismatch across repeats for tracking_vector PushPop (no reserve)");
                        agg.allocsWarned = true;
                    }
                }
            }
        }

        // 6b) Reserved: steady-state → 0 churn/op.
        {
            const std::string metricName = std::string{"tracking_vector PushPop (reserved)"} + TrackingSuffixFromEnv();
            auto runOnce = [&]() noexcept -> bench::BenchResult {
                tracking_vector<int> trackedValues;
                trackedValues.reserve(static_cast<std::size_t>(kIterations) + 1);
                std::uint32_t counter = 0;
                return DNG_BENCH(metricName.c_str(), kIterations, [&]() noexcept {
                    trackedValues.push_back(static_cast<int>(counter++));
                    trackedValues.pop_back();
                });
            };
            for (int i = 0; i < warmupCount; ++i) { (void)runOnce(); }
            for (int i = 0; i < repeatCount; ++i)
            {
                auto r = runOnce();
                PrintBenchLine(r);
                auto& agg = find_or_add(r.Name);
                agg.ns.push_back(r.NsPerOp);
                if (r.BytesPerOp >= 0.0)
                {
                    if (agg.bytesPerOp < 0.0) agg.bytesPerOp = r.BytesPerOp;
                    else if (!agg.bytesWarned && std::abs(agg.bytesPerOp - r.BytesPerOp) > 1e-9)
                    {
                        PrintNote("[WARN] bytesPerOp mismatch across repeats for tracking_vector PushPop (reserved)");
                        agg.bytesWarned = true;
                    }
                }
                if (r.AllocsPerOp >= 0.0)
                {
                    if (agg.allocsPerOp < 0.0) agg.allocsPerOp = r.AllocsPerOp;
                    else if (!agg.allocsWarned && std::abs(agg.allocsPerOp - r.AllocsPerOp) > 1e-9)
                    {
                        PrintNote("[WARN] allocsPerOp mismatch across repeats for tracking_vector PushPop (reserved)");
                        agg.allocsWarned = true;
                    }
                }
            }
        }
    }

    // ---- Scenario 7: SmallObjectAllocator 64B (env-tagged batch) ----------
    if (defaultAlloc)
    {
        const std::string metricName = std::string{"SmallObject 64B"} + SoaBatchSuffixFromEnv();
        auto runOnce = [&]() noexcept -> bench::BenchResult {
            ::dng::core::SmallObjectAllocator soalloc{ defaultAlloc };
            return DNG_BENCH(metricName.c_str(), kIterations, [&]() noexcept {
                void* ptr = AllocateCompat(&soalloc, 64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
                DeallocateCompat(&soalloc, ptr, 64u, alignof(std::max_align_t));
            });
        };
        for (int i = 0; i < warmupCount; ++i) { (void)runOnce(); }
        for (int i = 0; i < repeatCount; ++i)
        {
            auto r = runOnce();
            PrintBenchLine(r);
            auto& agg = find_or_add(r.Name);
            agg.ns.push_back(r.NsPerOp);
            if (r.BytesPerOp >= 0.0)
            {
                if (agg.bytesPerOp < 0.0) agg.bytesPerOp = r.BytesPerOp;
                else if (!agg.bytesWarned && std::abs(agg.bytesPerOp - r.BytesPerOp) > 1e-9)
                {
                    PrintNote("[WARN] bytesPerOp mismatch across repeats for SmallObject 64B");
                    agg.bytesWarned = true;
                }
            }
            if (r.AllocsPerOp >= 0.0)
            {
                if (agg.allocsPerOp < 0.0) agg.allocsPerOp = r.AllocsPerOp;
                else if (!agg.allocsWarned && std::abs(agg.allocsPerOp - r.AllocsPerOp) > 1e-9)
                {
                    PrintNote("[WARN] allocsPerOp mismatch across repeats for SmallObject 64B");
                    agg.allocsWarned = true;
                }
            }
        }
    }
    else
    {
        PrintNote("Tracking allocator unavailable; skipping tracking_vector scenarios.");
    }

    // --- Export JSON to artifacts/bench -------------------------------------
    {
        using namespace ::dng::bench;
        // Ensure output directory exists
        if (!EnsureBenchOutputDirExists())
        {
            PrintNote("Failed to create bench output directory; skipping JSON export.");
        }
        else
        {
            const char* outDir = BenchOutputDir();
#if defined(_WIN32)
            const char kSep = '\\';
#else
            const char kSep = '/';
#endif
            // Build ISO-like UTC timestamp
            char timeIso[32]{};
            std::time_t now = std::time(nullptr);
            std::tm tmUtc{};
#if defined(_WIN32)
            gmtime_s(&tmUtc, &now);
#else
            gmtime_r(&now, &tmUtc);
#endif
            // Use filename-safe UTC stamp (no ':' for Windows): YYYYMMDDTHHMMSSZ
            std::snprintf(timeIso, sizeof(timeIso), "%04d%02d%02dT%02d%02d%02dZ",
                tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday,
                tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec);

            // Choose gitSha from environment if available (e.g., CI)
            const char* sha = std::getenv("GITHUB_SHA");
            if (!sha || sha[0] == '\0') sha = std::getenv("GIT_COMMIT");
            if (!sha || sha[0] == '\0') sha = "unknown";

            // Compose filename: bench-runner-{gitsha}-{dateUtc}-windows-x64-msvc.bench.json
            char filePath[1024]{};
            std::snprintf(filePath, sizeof(filePath), "%s%cbench-runner-%s-%s-windows-x64-msvc.bench.json",
                outDir, kSep, sha, timeIso);

            PrintNote(filePath);
            std::FILE* f = std::fopen(filePath, "wb");
            if (!f)
            {
                PrintNote("Failed to open bench JSON file for writing.");
            }
            else
            {
                std::fprintf(f, "{\n");
                std::fprintf(f, "  \"suite\": \"bench-runner\",\n");
                std::fprintf(f, "  \"gitSha\": \"%s\",\n", sha);
                std::fprintf(f, "  \"dateUtc\": \"%s\",\n", timeIso);
                std::fprintf(f, "  \"platform\": { \"os\": \"Windows\", \"cpu\": \"x64\", \"compiler\": \"MSVC\" },\n");
                std::fprintf(f, "  \"metrics\": [\n");
                // Compute and emit aggregated metrics
                auto compute_stats = [](const std::vector<double>& v, double& outMin, double& outMax,
                                        double& outMedian, double& outMean, double& outStdDev) noexcept {
                    if (v.empty()) { outMin = outMax = outMedian = outMean = outStdDev = 0.0; return; }
                    outMin = *std::min_element(v.begin(), v.end());
                    outMax = *std::max_element(v.begin(), v.end());
                    outMean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
                    std::vector<double> tmp = v;
                    std::sort(tmp.begin(), tmp.end());
                    if (tmp.size() % 2 == 1)
                        outMedian = tmp[tmp.size() / 2];
                    else
                        outMedian = (tmp[tmp.size() / 2 - 1] + tmp[tmp.size() / 2]) * 0.5;
                    double var = 0.0;
                    for (double x : v) { double d = x - outMean; var += d * d; }
                    var /= static_cast<double>(v.size());
                    outStdDev = std::sqrt(var);
                };

                for (std::size_t i = 0; i < aggregates.size(); ++i)
                {
                    const auto& name = aggregates[i].first;
                    const auto& agg  = aggregates[i].second;
                    double mn, mx, med, mean, sd;
                    compute_stats(agg.ns, mn, mx, med, mean, sd);
                    std::fprintf(f, "    { \"name\": \"%s\", \"unit\": \"ns/op\", \"value\": %.3f, \"min\": %.3f, \"max\": %.3f, \"mean\": %.3f, \"stddev\": %.3f",
                        name.c_str(), med, mn, mx, mean, sd);
                    if (agg.bytesPerOp >= 0.0)
                        std::fprintf(f, ", \"bytesPerOp\": %.3f", agg.bytesPerOp);
                    if (agg.allocsPerOp >= 0.0)
                        std::fprintf(f, ", \"allocsPerOp\": %.3f", agg.allocsPerOp);
                    std::fprintf(f, " }%s\n", (i + 1 < aggregates.size()) ? "," : "");
                }
                std::fprintf(f, "  ]\n");
                std::fprintf(f, "}\n");
                std::fclose(f);

                PrintNote("Bench JSON exported to artifacts/bench/");
            }
        }
    }

    memory::MemorySystem::Shutdown();
    return 0;
}
