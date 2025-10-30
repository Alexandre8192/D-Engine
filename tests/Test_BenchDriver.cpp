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
#include <algorithm>    // std::sort
#include <cmath>        // std::sqrt, std::abs
#include <atomic>       // std::atomic for LocalBarrier
#include <thread>       // std::thread::hardware_concurrency
#include <cerrno>       // errno

#if defined(_WIN32)
#   ifndef NOMINMAX
#       define NOMINMAX 1
#   endif
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN 1
#   endif
#   include <windows.h>
#else
#   include <sched.h>
#   include <sys/resource.h>
#   include <unistd.h>
#endif

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

    struct Stats
    {
        double Min   = 0.0;
        double Max   = 0.0;
        double Median = 0.0;
        double Mean   = 0.0;
        double StdDev = 0.0;
        double RsdPct = 0.0;
    };

    [[nodiscard]] inline Stats ComputeStats(const double* samples, std::size_t count) noexcept
    {
        Stats stats{};
        if (!samples || count == 0u)
            return stats;

        double minValue = samples[0];
        double maxValue = samples[0];
        double sum = 0.0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const double sample = samples[i];
            if (sample < minValue) minValue = sample;
            if (sample > maxValue) maxValue = sample;
            sum += sample;
        }

        stats.Min = minValue;
        stats.Max = maxValue;
        stats.Mean = sum / static_cast<double>(count);

        std::vector<double> sorted(samples, samples + count);
        std::sort(sorted.begin(), sorted.end());
        const std::size_t mid = sorted.size() / 2;
        if ((sorted.size() % 2u) == 1u)
        {
            stats.Median = sorted[mid];
        }
        else
        {
            stats.Median = (sorted[mid - 1] + sorted[mid]) * 0.5;
        }

        double variance = 0.0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const double sample = samples[i];
            const double delta = sample - stats.Mean;
            variance += delta * delta;
        }
        variance /= static_cast<double>(count);
        stats.StdDev = std::sqrt(variance);
        stats.RsdPct = (stats.Mean != 0.0) ? ((stats.StdDev / stats.Mean) * 100.0) : 0.0;
        return stats;
    }

    inline void StabilizeCpu() noexcept
    {
#if defined(_WIN32)
        // Elevate process and thread priority, and pin to the first available logical CPU.
        const HANDLE process = ::GetCurrentProcess();
        const HANDLE thread  = ::GetCurrentThread();

        // Set high priority class for the process.
        (void)::SetPriorityClass(process, HIGH_PRIORITY_CLASS);
        (void)::SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);

        // Pin to the lowest-index logical CPU in the system mask to reduce scheduling variance.
        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask  = 0;
        if (::GetProcessAffinityMask(process, &processMask, &systemMask) != 0)
        {
            if (systemMask == 0)
            {
                systemMask = 1; // Fallback to CPU0 if unexpected.
            }
            // Find first set bit in systemMask
            DWORD_PTR single = 0;
            for (DWORD i = 0; i < sizeof(DWORD_PTR) * 8; ++i)
            {
                const DWORD_PTR bit = (static_cast<DWORD_PTR>(1) << i);
                if (systemMask & bit) { single = bit; break; }
            }
            if (single == 0) single = 1; // fallback to CPU0
            (void)::SetProcessAffinityMask(process, single);
        }
#else
        // Best-effort: try to elevate priority and pin to CPU0 if possible.
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(0, &set);
        (void)::sched_setaffinity(0, sizeof(set), &set);
        (void)::setpriority(PRIO_PROCESS, 0, -5);
#endif
    }

    inline void PrintCpuDiagnostics() noexcept
    {
        const unsigned rawLogical = std::thread::hardware_concurrency();
        const unsigned logical = (rawLogical == 0u) ? 1u : rawLogical;
        char buffer[256]{};

#if defined(_WIN32)
        const HANDLE process = ::GetCurrentProcess();
        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask = 0;
        const BOOL affinityOk = ::GetProcessAffinityMask(process, &processMask, &systemMask);
        const DWORD priorityClass = ::GetPriorityClass(process);

        const char* priorityLabel = "UNKNOWN";
        switch (priorityClass)
        {
            case IDLE_PRIORITY_CLASS:        priorityLabel = "IDLE"; break;
            case BELOW_NORMAL_PRIORITY_CLASS: priorityLabel = "BELOW_NORMAL"; break;
            case NORMAL_PRIORITY_CLASS:      priorityLabel = "NORMAL"; break;
            case ABOVE_NORMAL_PRIORITY_CLASS: priorityLabel = "ABOVE_NORMAL"; break;
            case HIGH_PRIORITY_CLASS:        priorityLabel = "HIGH"; break;
            case REALTIME_PRIORITY_CLASS:    priorityLabel = "REALTIME"; break;
            default: break;
        }

        if (affinityOk != 0)
        {
            std::snprintf(buffer, sizeof(buffer),
                "[CPU] logical=%u affinity=0x%llX priority=%s",
                logical,
                static_cast<unsigned long long>(processMask),
                priorityLabel);
        }
        else
        {
            const DWORD err = ::GetLastError();
            std::snprintf(buffer, sizeof(buffer),
                "[CPU] logical=%u affinity=<error %lu> priority=%s",
                logical,
                static_cast<unsigned long>(err),
                priorityLabel);
        }
#else
        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        const int affinityResult = ::sched_getaffinity(0, sizeof(cpuSet), &cpuSet);
        const int affinityErr = (affinityResult == 0) ? 0 : errno;

        unsigned long long mask = 0ull;
        if (affinityResult == 0)
        {
            const int maxBits = static_cast<int>(sizeof(mask) * 8);
            const int cpuLimit = std::min(maxBits, CPU_SETSIZE);
            for (int cpu = 0; cpu < cpuLimit; ++cpu)
            {
                if (CPU_ISSET(cpu, &cpuSet))
                    mask |= (1ull << cpu);
            }
        }

        errno = 0;
        const int niceValue = ::getpriority(PRIO_PROCESS, 0);
        const int niceErr = errno;

        if (affinityResult == 0)
        {
            if (niceErr == 0)
            {
                std::snprintf(buffer, sizeof(buffer),
                    "[CPU] logical=%u affinity=0x%llX nice=%d",
                    logical,
                    mask,
                    niceValue);
            }
            else
            {
                std::snprintf(buffer, sizeof(buffer),
                    "[CPU] logical=%u affinity=0x%llX nice=<error %d>",
                    logical,
                    mask,
                    niceErr);
            }
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer),
                "[CPU] logical=%u affinity=<error %d>",
                logical,
                affinityErr);
        }
#endif

        PrintLine(std::string_view{buffer});
    }

    // --- Small-object benchmark helpers ------------------------------------

    constexpr std::size_t kSmallObjectSizes[] = { 8u, 16u, 32u, 64u, 128u, 256u, 512u };
    constexpr std::size_t kSmallObjectSizeCount = sizeof(kSmallObjectSizes) / sizeof(kSmallObjectSizes[0]);
    constexpr std::size_t kSmallObjectBurst = 64u;
    constexpr std::size_t kMaxSmallObjectThreads = 8u;
    constexpr std::size_t kSmallObjectSlotCount = kSmallObjectSizeCount * kSmallObjectBurst;

#if defined(DNG_SMALLOBJ_TLS_BINS)
    constexpr bool kSmallObjectTLSCompiled = (DNG_SMALLOBJ_TLS_BINS != 0);
#else
    constexpr bool kSmallObjectTLSCompiled = false;
#endif

#if defined(DNG_GLOBAL_NEW_SMALL_THRESHOLD)
    constexpr std::size_t kGlobalNewSmallThreshold = static_cast<std::size_t>(DNG_GLOBAL_NEW_SMALL_THRESHOLD);
#else
    constexpr std::size_t kGlobalNewSmallThreshold = 0u;
#endif

#if defined(DNG_SOALLOC_BATCH)
    constexpr std::size_t kSmallObjectBatchCompiled = static_cast<std::size_t>(DNG_SOALLOC_BATCH);
#else
    constexpr std::size_t kSmallObjectBatchCompiled = 0u;
#endif

    struct SmallObjectThreadContext
    {
        void* slots[kSmallObjectSlotCount]{};

        void Reset() noexcept
        {
            for (std::size_t i = 0; i < kSmallObjectSlotCount; ++i)
            {
                slots[i] = nullptr;
            }
        }
    };

    inline void BestEffortPinWorker(unsigned logicalIndex) noexcept
    {
#if defined(_WIN32)
        const HANDLE thread = ::GetCurrentThread();
        const unsigned bitCount = static_cast<unsigned>(sizeof(DWORD_PTR) * 8u);
        const unsigned shift = (bitCount == 0u) ? 0u : (logicalIndex % bitCount);
        DWORD_PTR mask = (bitCount == 0u) ? 1u : (static_cast<DWORD_PTR>(1) << shift);
        if (mask == 0)
        {
            mask = 1;
        }
        (void)::SetThreadAffinityMask(thread, mask);
#else
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(static_cast<int>(logicalIndex % CPU_SETSIZE), &set);
        (void)::sched_setaffinity(0, sizeof(set), &set);
#endif
    }

    class LocalBarrier
    {
    public:
        explicit LocalBarrier(std::size_t count) noexcept
            : mThreshold(count == 0u ? 1u : static_cast<unsigned>(count)),
              mCount(mThreshold),
              mGeneration(0u)
        {
        }

        void ArriveAndWait() noexcept
        {
            const unsigned generation = mGeneration.load(std::memory_order_acquire);
            if (mCount.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
            {
                mCount.store(mThreshold, std::memory_order_release);
                mGeneration.fetch_add(1u, std::memory_order_acq_rel);
            }
            else
            {
                while (mGeneration.load(std::memory_order_acquire) == generation)
                {
                    std::this_thread::yield();
                }
            }
        }

    private:
        const unsigned mThreshold;
        std::atomic<unsigned> mCount;
        std::atomic<unsigned> mGeneration;
    };

    inline void PrintSmallObjectBanner(const char* caseLabel,
        bool runtimeRequested,
        bool effective,
        std::size_t threadCount) noexcept
    {
        char banner[256]{};
        const int written = std::snprintf(banner, sizeof(banner),
            "[SmallObject] %s CT=%c RT=%c EFFECTIVE=%c threshold=%llu batch=%llu threads=%llu",
            caseLabel ? caseLabel : "<unnamed>",
            kSmallObjectTLSCompiled ? '1' : '0',
            runtimeRequested ? '1' : '0',
            effective ? '1' : '0',
            static_cast<unsigned long long>(kGlobalNewSmallThreshold),
            static_cast<unsigned long long>(kSmallObjectBatchCompiled),
            static_cast<unsigned long long>(threadCount));

        if (written > 0)
        {
            const std::size_t length = (written >= static_cast<int>(sizeof(banner)))
                ? (sizeof(banner) - 1u)
                : static_cast<std::size_t>(written);
            PrintLine(std::string_view{ banner, length });
        }
    }

    inline void RunSmallObjectMultithreadIteration(::dng::core::SmallObjectAllocator& allocator,
        std::size_t threadCount,
        bool crossThreadFree,
        SmallObjectThreadContext* contexts,
        std::size_t contextCapacity) noexcept
    {
        const std::size_t requestedThreads = (threadCount == 0u) ? 1u : threadCount;
        const std::size_t useThreads = (requestedThreads > contextCapacity) ? contextCapacity : requestedThreads;

        for (std::size_t t = 0; t < useThreads; ++t)
        {
            contexts[t].Reset();
        }

        LocalBarrier barrier(useThreads);
        const std::size_t alignment = alignof(std::max_align_t);

        auto worker = [&](std::size_t threadIndex) noexcept
        {
            BestEffortPinWorker(static_cast<unsigned>(threadIndex));
            SmallObjectThreadContext& ctx = contexts[threadIndex];
            for (std::size_t sizeIdx = 0; sizeIdx < kSmallObjectSizeCount; ++sizeIdx)
            {
                const std::size_t size = kSmallObjectSizes[sizeIdx];
                void** const mySlot = ctx.slots + (sizeIdx * kSmallObjectBurst);

                for (std::size_t i = 0; i < kSmallObjectBurst; ++i)
                {
                    void* ptr = allocator.Allocate(size, alignment);
                    DNG_CHECK(ptr != nullptr);
                    mySlot[i] = ptr;
                }

                barrier.ArriveAndWait();

                if (crossThreadFree && useThreads > 1u)
                {
                    const std::size_t recipientIndex = (threadIndex + 1u) % useThreads;
                    void** const recipientSlot = contexts[recipientIndex].slots + (sizeIdx * kSmallObjectBurst);
                    const std::size_t crossCount = kSmallObjectBurst / 2u;
                    for (std::size_t i = 0; i < crossCount; ++i)
                    {
                        void* target = recipientSlot[i];
                        DNG_CHECK(target != nullptr);
                        allocator.Deallocate(target, size, alignment);
                        recipientSlot[i] = nullptr;
                    }
                }

                barrier.ArriveAndWait();

                for (std::size_t i = 0; i < kSmallObjectBurst; ++i)
                {
                    void* target = mySlot[i];
                    if (target != nullptr)
                    {
                        allocator.Deallocate(target, size, alignment);
                        mySlot[i] = nullptr;
                    }
                }

                barrier.ArriveAndWait();
            }
        };

        std::thread threadPool[kMaxSmallObjectThreads];
        for (std::size_t t = 1; t < useThreads; ++t)
        {
            threadPool[t - 1] = std::thread(worker, t);
        }

        worker(0u);

        for (std::size_t t = 1; t < useThreads; ++t)
        {
            if (threadPool[t - 1].joinable())
            {
                threadPool[t - 1].join();
            }
        }
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

    int warmupCount = 0;
    int repeatMin = 3;
    int repeatMax = repeatMin;
    bool repeatMaxExplicit = false;
    double targetRsd = 0.0;
    bool cpuInfoEnabled = true;

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
            repeatMin = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
            if (repeatMin < 1) repeatMin = 1;
            if (!repeatMaxExplicit)
                repeatMax = repeatMin;
        }
        else if (arg == "--target-rsd" && (i + 1) < argc)
        {
            targetRsd = std::strtod(argv[++i], nullptr);
            if (targetRsd < 0.0)
                targetRsd = 0.0;
        }
        else if (arg == "--max-repeat" && (i + 1) < argc)
        {
            repeatMax = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
            if (repeatMax < 1) repeatMax = 1;
            repeatMaxExplicit = true;
        }
        else if (arg == "--cpu-info")
        {
            cpuInfoEnabled = true;
            if ((i + 1) < argc && argv[i + 1] && argv[i + 1][0] != '-')
            {
                const std::string value = argv[++i];
                if (value == "0" || value == "false" || value == "off")
                    cpuInfoEnabled = false;
            }
        }
        else if (arg == "--no-cpu-info")
        {
            cpuInfoEnabled = false;
        }
    }

    if (targetRsd > 0.0 && repeatMin < 3)
        repeatMin = 3;
    if (repeatMin < 1)
        repeatMin = 1;
    if (repeatMax < repeatMin)
        repeatMax = repeatMin;

    if (cpuInfoEnabled)
    {
        StabilizeCpu();
        PrintCpuDiagnostics();
    }

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

    struct Agg
    {
        std::vector<double> ns;
        double bytesPerOp = -1.0;
        double allocsPerOp = -1.0;
        bool bytesWarned = false;
        bool allocsWarned = false;
    };

    std::vector<std::pair<std::string, Agg>> aggregates; // name -> aggregate; deterministic insertion order

    auto find_or_add = [&](const char* name) -> Agg& {
        for (auto& p : aggregates)
        {
            if (p.first == name)
                return p.second;
        }
        aggregates.emplace_back(name ? std::string{name} : std::string{"<unnamed>"}, Agg{});
        return aggregates.back().second;
    };

    const int minRepeatCount = repeatMin;
    const int maxRepeatCount = repeatMax;
    const double targetRsdPct = targetRsd;
    const bool targetRsdActive = targetRsdPct > 0.0;

    auto executeScenario = [&](const std::string& metricName, auto&& runOnce) noexcept
    {
        for (int i = 0; i < warmupCount; ++i)
        {
            (void)runOnce();
        }

        auto& agg = find_or_add(metricName.c_str());
        agg.ns.clear();
        agg.ns.reserve(static_cast<std::size_t>(maxRepeatCount));
        agg.bytesPerOp = -1.0;
        agg.allocsPerOp = -1.0;
        agg.bytesWarned = false;
        agg.allocsWarned = false;

        int repeats = 0;
        bool hitMax = false;

        while (true)
        {
            auto result = runOnce();
            PrintBenchLine(result);
            ++repeats;

            agg.ns.push_back(result.NsPerOp);

            if (result.BytesPerOp >= 0.0)
            {
                if (agg.bytesPerOp < 0.0)
                {
                    agg.bytesPerOp = result.BytesPerOp;
                }
                else if (!agg.bytesWarned && std::abs(agg.bytesPerOp - result.BytesPerOp) > 1e-9)
                {
                    char warn[256]{};
                    std::snprintf(warn, sizeof(warn),
                        "[WARN] bytesPerOp mismatch across repeats for %s",
                        metricName.c_str());
                    PrintNote(warn);
                    agg.bytesWarned = true;
                }
            }

            if (result.AllocsPerOp >= 0.0)
            {
                if (agg.allocsPerOp < 0.0)
                {
                    agg.allocsPerOp = result.AllocsPerOp;
                }
                else if (!agg.allocsWarned && std::abs(agg.allocsPerOp - result.AllocsPerOp) > 1e-9)
                {
                    char warn[256]{};
                    std::snprintf(warn, sizeof(warn),
                        "[WARN] allocsPerOp mismatch across repeats for %s",
                        metricName.c_str());
                    PrintNote(warn);
                    agg.allocsWarned = true;
                }
            }

            const Stats stats = ComputeStats(agg.ns.data(), agg.ns.size());
            const bool reachedMin = repeats >= minRepeatCount;
            const bool reachedMax = repeats >= maxRepeatCount;
            const bool meetsRsd = targetRsdActive && repeats >= 3 && stats.RsdPct <= targetRsdPct;

            if (reachedMax)
            {
                hitMax = true;
                break;
            }

            if (targetRsdActive)
            {
                if (reachedMin && meetsRsd)
                    break;
            }
            else if (reachedMin)
            {
                break;
            }
        }

        const Stats finalStats = ComputeStats(agg.ns.data(), agg.ns.size());
        char summary[256]{};
        std::snprintf(summary, sizeof(summary),
            "n=%d median=%.3f mean=%.3f RSD=%.3f%% min=%.3f max=%.3f",
            repeats,
            finalStats.Median,
            finalStats.Mean,
            finalStats.RsdPct,
            finalStats.Min,
            finalStats.Max);
        PrintLine(std::string_view{summary});

        if (targetRsdActive && finalStats.RsdPct > targetRsdPct && hitMax)
        {
            char warn[256]{};
            std::snprintf(warn, sizeof(warn),
                "[WARN] target RSD exceeded for %s: %.3f%% > %.3f%% (max-repeat reached)",
                metricName.c_str(),
                finalStats.RsdPct,
                targetRsdPct);
            PrintNote(warn);
        }
    };

    // ---- Baseline: NoOp (harness overhead) ---------------------------------
    {
        const std::string metricName = std::string{"NoOp"};
        auto runOnce = [&]() noexcept -> bench::BenchResult {
            return DNG_BENCH(metricName.c_str(), kIterations, []() noexcept {});
        };
        executeScenario(metricName, runOnce);
    }

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
        executeScenario(metricName, runOnce);
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
        executeScenario(metricName, runOnce);
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
        executeScenario(metricName, runOnce);
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
        executeScenario(metricName, runOnce);
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
        executeScenario(metricName, runOnce);
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
        executeScenario(metricName, runOnce);
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
            executeScenario(metricName, runOnce);
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
            executeScenario(metricName, runOnce);
        }
    }
    else
    {
        PrintNote("Default allocator unavailable; skipping SmallObject scenario.");
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
        executeScenario(metricName, runOnce);
    }
    else
    {
        PrintNote("Tracking allocator unavailable; skipping tracking_vector scenarios.");
    }

    // ---- Scenario 8: SmallObjectAllocator multi-thread stress --------------
    if (defaultAlloc)
    {
        struct SmallScenario
        {
            const char* Label;
            bool RuntimeTLS;
            bool CrossThreadFree;
            std::size_t Threads;
        };

        const SmallScenario scenarios[] = {
            { "ST-Local", false, false, 1u },
            { "ST-TLS",   true,  false, 1u },
            { "MT2-Local", false, false, 2u },
            { "MT2-TLS",   true,  false, 2u },
            { "MT2-TLS-XFree", true, true, 2u },
            { "MT4-TLS",   true,  false, 4u },
        };

        SmallObjectThreadContext contexts[kMaxSmallObjectThreads]{};

        for (const SmallScenario& scenario : scenarios)
        {
            ::dng::core::SmallObjectConfig config{};
            config.EnableTLSBins = scenario.RuntimeTLS;
            ::dng::core::SmallObjectAllocator soalloc{ defaultAlloc, config };

            const bool effectiveTLS = scenario.RuntimeTLS && kSmallObjectTLSCompiled;
            PrintSmallObjectBanner(scenario.Label, scenario.RuntimeTLS, effectiveTLS, scenario.Threads);

            std::string metricName = std::string{"SmallObject/"} + scenario.Label;
            metricName += scenario.CrossThreadFree ? " CrossFree" : " LocalFree";
            metricName += " T";
            metricName += std::to_string(static_cast<unsigned long long>(scenario.Threads));

            auto runOnce = [&]() noexcept -> bench::BenchResult {
                return DNG_BENCH(metricName.c_str(), kIterations, [&]() noexcept {
                    RunSmallObjectMultithreadIteration(soalloc,
                        scenario.Threads,
                        scenario.CrossThreadFree,
                        contexts,
                        kMaxSmallObjectThreads);
                });
            };

            executeScenario(metricName, runOnce);
        }
    }
    else
    {
        PrintNote("Default allocator unavailable; skipping SmallObject TLS scenarios.");
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
                for (std::size_t i = 0; i < aggregates.size(); ++i)
                {
                    const auto& name = aggregates[i].first;
                    const auto& agg  = aggregates[i].second;
                    const Stats stats = ComputeStats(agg.ns.data(), agg.ns.size());
                    std::fprintf(f, "    { \"name\": \"%s\", \"unit\": \"ns/op\", \"value\": %.3f",
                        name.c_str(), stats.Median);
                    if (agg.ns.size() > 1)
                    {
                        std::fprintf(f, ", \"min\": %.3f, \"max\": %.3f, \"mean\": %.3f, \"stddev\": %.3f",
                            stats.Min, stats.Max, stats.Mean, stats.StdDev);
                    }
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
