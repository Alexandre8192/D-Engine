/*
===============================================================================
D-Engine - tests/Test_BenchDriver.cpp  (BenchDriver v2)
-------------------------------------------------------------------------------
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

#include <cstddef>      // std::max_align_t, std::size_t
#include <cstdint>      // std::uint32_t, std::uint64_t
#include <type_traits>  // std::true_type, std::false_type
#include <vector>       // std::vector for tracking_vector alias
#include <string_view>  // std::string_view

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
        if (!message) return;
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
        if (!alloc || !ptr) return;

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

int main()
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

    // ---- Scenario 1: dng::vector push/pop without reserve -------------------
    {
        vector<int> values;
        std::uint32_t counter = 0;

        auto result = DNG_BENCH("Vector PushPop (no reserve)", kIterations, [&]() noexcept {
            values.push_back(static_cast<int>(counter++));
            values.pop_back();
        });

        PrintBenchLine(result);
        // No analytical churn printed: growth strategy is implementation dependent.
    }

    // ---- Scenario 2: dng::vector push/pop with reserve ----------------------
    {
        vector<int> values;
        values.reserve(static_cast<std::size_t>(kIterations) + 1);
        std::uint32_t counter = 0;

        auto result = DNG_BENCH("Vector PushPop (reserved)", kIterations, [&]() noexcept {
            values.push_back(static_cast<int>(counter++));
            values.pop_back();
        });

    PrintBenchLine(result);
    }

    // ---- Scenario 3: Arena allocate/rewind (64 bytes) -----------------------
    if (defaultAlloc)
    {
        ::dng::core::ArenaAllocator arena{ defaultAlloc, 8u * 1024u * 1024u };

        auto result = DNG_BENCH("Arena Allocate/Rewind (64B)", kIterations, [&]() noexcept {
            const auto marker = arena.GetMarker();
            void* ptr = arena.Allocate(64u, alignof(std::max_align_t));
            DNG_CHECK(ptr != nullptr);
            arena.Rewind(marker);
        });

    PrintBenchLine(result);
    }
    else
    {
        PrintNote("Default allocator unavailable; skipping arena scenario.");
    }

    // ---- Scenario 3b: Arena bulk allocate + rewind -------------------------
    if (defaultAlloc)
    {
        ::dng::core::ArenaAllocator arena{ defaultAlloc, 8u * 1024u * 1024u };

        auto result = DNG_BENCH("Arena Bulk Allocate/Rewind (8x64B)", kIterations, [&]() noexcept {
            const auto marker = arena.GetMarker();
            for (int i = 0; i < 8; ++i)
            {
                void* ptr = arena.Allocate(64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
            }
            arena.Rewind(marker);
        });

        PrintBenchLine(result);
    }

    // ---- Scenario 4: DefaultAllocator direct alloc/free (64 bytes) ----------
    if (defaultAlloc)
    {
        auto result = DNG_BENCH("DefaultAllocator Alloc/Free 64B", kIterations, [&]() noexcept {
            void* ptr = AllocateCompat(defaultAlloc, 64u, alignof(std::max_align_t));
            DNG_CHECK(ptr != nullptr);
            DeallocateCompat(defaultAlloc, ptr, 64u, alignof(std::max_align_t));
        });

    PrintBenchLine(result);
    }
    else
    {
        PrintNote("Default allocator unavailable; skipping default alloc/free scenario.");
    }

    // ---- Scenario 5: TrackingAllocator direct alloc/free (64 bytes) ---------
    if (tracking)
    {
        auto result = DNG_BENCH("TrackingAllocator Alloc/Free 64B", kIterations, [&]() noexcept {
            void* ptr = AllocateCompat(tracking, 64u, alignof(std::max_align_t));
            DNG_CHECK(ptr != nullptr);
            DeallocateCompat(tracking, ptr, 64u, alignof(std::max_align_t));
        });

    PrintBenchLine(result);
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
            tracking_vector<int> trackedValues;
            std::uint32_t counter = 0;

            auto resultNoReserve = DNG_BENCH("tracking_vector PushPop (no reserve)", kIterations, [&]() noexcept {
                trackedValues.push_back(static_cast<int>(counter++));
                trackedValues.pop_back();
            });
            PrintBenchLine(resultNoReserve);
        }

        // 6b) Reserved: steady-state → 0 churn/op.
        {
            tracking_vector<int> trackedValues;
            trackedValues.reserve(static_cast<std::size_t>(kIterations) + 1);
            std::uint32_t counter = 0;

            auto resultReserved = DNG_BENCH("tracking_vector PushPop (reserved)", kIterations, [&]() noexcept {
                trackedValues.push_back(static_cast<int>(counter++));
                trackedValues.pop_back();
            });
            PrintBenchLine(resultReserved);
        }
    }
    else
    {
        PrintNote("Tracking allocator unavailable; skipping tracking_vector scenarios.");
    }

    memory::MemorySystem::Shutdown();
    return 0;
}
