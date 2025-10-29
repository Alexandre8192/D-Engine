#pragma once
// ============================================================================
// D-Engine - Core/Diagnostics/Bench.hpp
// ----------------------------------------------------------------------------
// Purpose : Header-only micro-benchmark harness that reports per-op timing and
//           churn. Integrates with TrackingAllocator monotonic counters when
//           available to expose bytes/op and allocs/op without extra pressure.
// Contract: Self-contained, no global state. Accepts a noexcept `void()`
//           callable. Performs a warm-up phase (configurable, default 20'000
//           invocations), then measures a timed pass. Optional auto-scaling
//           targets ~250 ms total runtime in Release builds behind a constexpr
//           switch. Memory metrics require DNG_MEM_TRACKING and a live
//           TrackingAllocator exposing CaptureMonotonic(); otherwise they are
//           reported as `<tracking-off>`.
// Notes   :
//   - Uses std::chrono::steady_clock (monotonic) to avoid wall-clock jumps.
//   - Extremely small bodies may still need explicit iteration overrides.
//   - Memory deltas can accumulate noise if other threads allocate during the
//     timed window; serialize callers for stable numbers.
//   - The helper emits no logs; `ToString` offers quick, dependency-free text.
// ============================================================================

#include <chrono>      // std::chrono::steady_clock
#include <cmath>       // std::ceil
#include <cstdint>     // std::uint64_t
#include <string>      // std::string
#include <utility>     // std::forward, std::declval
#include <type_traits> // std::is_same
#include <limits>      // std::numeric_limits
#include <cstdio>      // std::snprintf
#include <cstdlib>     // std::getenv
#include <errno.h>     // errno, EEXIST
#if defined(_WIN32)
    #include <direct.h>    // _mkdir
#else
    #include <sys/types.h>
    #include <sys/stat.h>  // mkdir
#endif

// --- Optional memory tracking integration ----------------------------------
// Define DNG_MEM_TRACKING to enable per-op memory/alloc counts.
// If your TrackingAllocator lives in a different namespace, override:
//   #define DNG_TRACKING_ALLOCATOR_NS ::dng::core
#if !defined(DNG_TRACKING_ALLOCATOR_NS)
#define DNG_TRACKING_ALLOCATOR_NS ::dng::core
#endif

#if defined(DNG_MEM_TRACKING) && DNG_MEM_TRACKING
    #include "Core/Memory/MemorySystem.hpp"        // MemorySystem::GetTrackingAllocator()
    #include "Core/Memory/TrackingAllocator.hpp"   // TrackingAllocator, TrackingMonotonicCounters
#endif

// --- Minimal diagnostics guard (kept include-safe) --------------------------
#ifndef DNG_CHECK
#define DNG_CHECK(expr) do { (void)sizeof(expr); } while (0)
#endif

namespace dng {
namespace bench {
// ---
// Purpose : Resolve the output directory for benchmark artifacts.
// Contract: Reads environment variable DNG_BENCH_OUT; when unset, returns a
//           stable default ("artifacts/bench"). No allocations; caller may
//           create the directory if needed. Thread-safe; returns pointer to a
//           static string literal or the process environment memory.
// Notes   : Kept header-only per engine guidance; path uses platform separators.
// ---
[[nodiscard]] inline const char* BenchOutputDir() noexcept
{
#if defined(_WIN32)
    static constexpr const char* kDefault = "artifacts\\bench";
#else
    static constexpr const char* kDefault = "artifacts/bench";
#endif
    // MSVC marks getenv as deprecated; use a targeted pragma to silence 4996 here.
#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable:4996)
#endif
    const char* env = std::getenv("DNG_BENCH_OUT");
#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
    if (env && env[0] != '\0')
        return env;
    return kDefault;
}

// ---
// Purpose : Best-effort creation of the benchmark output directory returned by
//           BenchOutputDir(), creating intermediate components if necessary.
// Contract:
//   - Never throws; returns true if the directory exists (already or after
//     creation), false on error (including overly long paths).
//   - Does not allocate on the heap; uses a fixed-size stack buffer (1 KiB);
//     paths longer than this are not supported and will fail gracefully.
//   - Thread-safety: benign races if called concurrently; multiple callers may
//     attempt to create the same component; EEXIST is tolerated.
// Notes   : Avoids <filesystem>; uses _mkdir (Windows) or mkdir (POSIX).
// ---
[[nodiscard]] inline bool EnsureBenchOutputDirExists() noexcept
{
    const char* path = BenchOutputDir();
    if (!path || path[0] == '\0')
        return false;

    constexpr std::size_t kMax = 1024; // hard ceiling to avoid dynamic allocs
    char buf[kMax + 1]{};
    std::size_t len = 0;

#if defined(_WIN32)
    const char kSep = '\\';
#else
    const char kSep = '/';
#endif

    // Build the path and create each prefix directory when a separator is seen.
    for (const char* p = path; ; ++p)
    {
        const char c = *p;
        const bool atEnd = (c == '\0');
        const bool isSep = (!atEnd) && (c == '/' || c == '\\');

        if (!atEnd)
        {
            const char outc = (c == '/' || c == '\\') ? kSep : c;
            if (len >= kMax) return false;
            buf[len++] = outc;
        }

        if (isSep || atEnd)
        {
            // Determine prefix length without trailing separators
            std::size_t t = len;
            while (t > 0 && buf[t - 1] == kSep) { --t; }
            if (t > 0)
            {
                const char saved = buf[t];
                buf[t] = '\0';
#if defined(_WIN32)
                if (_mkdir(buf) != 0)
                {
                    if (errno != EEXIST)
                        return false;
                }
#else
                if (mkdir(buf, 0777) != 0)
                {
                    if (errno != EEXIST)
                        return false;
                }
#endif
                buf[t] = saved;
            }
            if (atEnd) break;
        }
    }
    return true;
}


// === BenchResult ============================================================
// Purpose : Immutable container describing a single benchmark outcome.
// Contract: Produced by bench::Run. `Name` must outlive the result;
//           `Iterations >= 1`; time and memory metrics are per operation.
// Notes   : Memory metrics equal -1.0 when the build lacks tracking support
//           or the TrackingAllocator is not available/initialized. Negative
//           values are possible if the body frees more than it allocates.
// ----------------------------------------------------------------------------
struct BenchResult
{
    const char*   Name{nullptr};      // String literal or stable c-string.
    std::uint64_t Iterations{0};      // Count of iterations measured (>= 1).
    double        NsPerOp{0.0};       // Nanoseconds per operation.
    double        BytesPerOp{-1.0};   // Bytes/op or -1.0 if tracking is off.
    double        AllocsPerOp{-1.0};  // Allocs/op or -1.0 if tracking is off.

    [[nodiscard]] constexpr bool HasMemoryStats() const noexcept
    {
        return BytesPerOp >= 0.0 && AllocsPerOp >= 0.0;
    }
};

namespace detail {

// --- Policy knobs -----------------------------------------------------------
// Purpose : Keep internal tunables local to implementation.
// Notes   : Warmup is configurable via DNG_BENCH_WARMUP_ITERS.
// ----------------------------------------------------------------------------
#if defined(DNG_BENCH_WARMUP_ITERS)
constexpr std::uint64_t kWarmupIterations = static_cast<std::uint64_t>(DNG_BENCH_WARMUP_ITERS);
#else
constexpr std::uint64_t kWarmupIterations = 20'000;
#endif

#if defined(DNG_BENCH_ENABLE_AUTOSCALE)
constexpr bool kEnableAutoScale = (DNG_BENCH_ENABLE_AUTOSCALE != 0);
#else
constexpr bool kEnableAutoScale = true;
#endif

#if defined(DNG_BENCH_TARGET_NS)
constexpr double kTargetDurationNs = static_cast<double>(DNG_BENCH_TARGET_NS);
#else
constexpr double kTargetDurationNs = 250'000'000.0; // ~250 ms target
#endif

constexpr double kMinDurationNs = 150'000'000.0;     // Lower bound before we rescale
constexpr double kMaxAutoScaleMultiplier = 512.0;    // Clamp to avoid runaway iteration counts

// Sentinel used when memory tracking is off or the allocator snapshot
// cannot be captured. Public API mirrors this sentinel via BenchResult.
constexpr double kTrackingUnavailable = -1.0;

// Use a monotonic clock to avoid wall-clock adjustments.
using Clock = std::chrono::steady_clock;

// Trait that validates the callable matches `void()` and is noexcept.
template<class F>
struct IsNoexceptVoidCallable
{
    enum : bool
    {
        value = std::is_same<decltype(std::declval<F&>()()), void>::value && noexcept(std::declval<F&>()())
    };
};

#if defined(DNG_MEM_TRACKING) && DNG_MEM_TRACKING
template<class...>
struct VoidHelper { using type = void; };

template<class... Ts>
using VoidT = typename VoidHelper<Ts...>::type;

template<class TA, class = void>
struct HasCaptureMonotonic : std::false_type {};

template<class TA>
struct HasCaptureMonotonic<TA, VoidT<decltype(std::declval<const TA&>().CaptureMonotonic())>>
    : std::true_type {};
#endif

template<class F>
[[nodiscard]] inline double Measure(F& callable, std::uint64_t iterations) noexcept
{
    const auto start = Clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i)
        callable();
    const auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count();
}

#if defined(DNG_MEM_TRACKING) && DNG_MEM_TRACKING
template<class TA>
inline void CaptureBefore(::dng::core::TrackingMonotonicCounters& out, TA* allocator, std::true_type) noexcept
{
    if (allocator)
    {
        out = allocator->CaptureMonotonic();
    }
}

template<class TA>
inline void CaptureBefore(::dng::core::TrackingMonotonicCounters&, TA*, std::false_type) noexcept
{
}

template<class TA>
[[nodiscard]] inline ::dng::core::TrackingMonotonicCounters CaptureAfter(
    TA* allocator,
    const ::dng::core::TrackingMonotonicCounters& before,
    std::uint64_t iterations,
    double& bytesPerOp,
    double& allocsPerOp,
    std::true_type) noexcept
{
    if (!allocator)
        return before;

    const auto after = allocator->CaptureMonotonic();
    const double denom = static_cast<double>(iterations);
    const double dAllocCalls = static_cast<double>(after.TotalAllocCalls - before.TotalAllocCalls);
    const double dAllocBytes = static_cast<double>(after.TotalBytesAllocated - before.TotalBytesAllocated);

    bytesPerOp  = dAllocBytes / denom;
    allocsPerOp = dAllocCalls / denom;
    return after;
}

template<class TA>
[[nodiscard]] inline ::dng::core::TrackingMonotonicCounters CaptureAfter(
    TA*,
    const ::dng::core::TrackingMonotonicCounters& before,
    std::uint64_t,
    double&,
    double&,
    std::false_type) noexcept
{
    return before;
}
#endif

// Helper that appends a floating-point value formatted to three decimals.
// Using snprintf keeps dependencies predictable and cross-platform.
inline void AppendFormatted(std::string& out, double value, const char* suffix) noexcept
{
    char buffer[64]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    if (written > 0)
    {
        out.append(buffer, static_cast<std::size_t>(written));
    }
    if (suffix) { out += suffix; }
}

} // namespace detail

// === Run ====================================================================
// Purpose : Execute a micro-benchmark, reporting timing and optional memory
//           metrics per operation.
// Contract:
//   - name: non-null, stable c-string for the duration of the call.
//   - iterations: if 0 is passed, promoted to 1 (documented) to keep a valid
//                 divisor.
//   - fn: must be a `noexcept` callable with signature `void()`.
//   - Thread-safety: `Run` is not thread-safe nor re-entrant; callers must
//     serialize invocations if they rely on global tracking deltas.
// Notes :
//   - Warmup runs first and is excluded from timed results.
//   - For extremely small bodies, increase `iterations` to exceed clock granularity.
//   - Memory deltas simply diff allocator snapshots and may include noise if
//     other threads allocate concurrently. On single-thread benches, deltas are
//     typically stable.
// ----------------------------------------------------------------------------
template <class F>
[[nodiscard]] inline BenchResult Run(const char* name, std::uint64_t iterations, F&& fn) noexcept
{
    DNG_CHECK(name != nullptr);

    static_assert(detail::IsNoexceptVoidCallable<F>::value,
        "dng::bench::Run expects a noexcept callable with signature void()");

    std::uint64_t effectiveIterations = (iterations == 0) ? 1 : iterations;

    auto&& callable = std::forward<F>(fn);

    // Warmup: prime caches/branch predictors without polluting timings.
    const std::uint64_t warmupBudget = (detail::kWarmupIterations == 0)
        ? 0
        : ((effectiveIterations < detail::kWarmupIterations) ? effectiveIterations : detail::kWarmupIterations);
    for (std::uint64_t i = 0; i < warmupBudget; ++i)
        callable();

    double bytesPerOp  = detail::kTrackingUnavailable;
    double allocsPerOp = detail::kTrackingUnavailable;

#if defined(DNG_MEM_TRACKING) && DNG_MEM_TRACKING
    using TA = DNG_TRACKING_ALLOCATOR_NS::TrackingAllocator;

    TA* trackingAllocator = nullptr;
    ::dng::core::TrackingMonotonicCounters beforeMono{};

    if (const auto trackingRef = ::dng::memory::MemorySystem::GetTrackingAllocator(); trackingRef.Get())
    {
        trackingAllocator = static_cast<TA*>(trackingRef.Get());
    }

    detail::CaptureBefore(beforeMono, trackingAllocator, detail::HasCaptureMonotonic<TA>{});
#endif

    double totalNanoseconds = detail::Measure(callable, effectiveIterations);

#if defined(DNG_MEM_TRACKING) && DNG_MEM_TRACKING
    beforeMono = detail::CaptureAfter(
        trackingAllocator,
        beforeMono,
        effectiveIterations,
        bytesPerOp,
        allocsPerOp,
        detail::HasCaptureMonotonic<TA>{});
#endif

    if (detail::kEnableAutoScale)
    {
        if (totalNanoseconds > 0.0 && totalNanoseconds < detail::kMinDurationNs)
        {
            const double desiredScale = detail::kTargetDurationNs / totalNanoseconds;
            double clampedScale = desiredScale;
            if (clampedScale < 1.0)
                clampedScale = 1.0;
            if (clampedScale > detail::kMaxAutoScaleMultiplier)
                clampedScale = detail::kMaxAutoScaleMultiplier;

            const double scaledProduct = clampedScale * static_cast<double>(effectiveIterations);
            const double maxIterations = static_cast<double>((std::numeric_limits<std::uint64_t>::max)());
            const double sanitized = (scaledProduct > maxIterations) ? maxIterations : scaledProduct;
            const std::uint64_t scaledIterations = static_cast<std::uint64_t>(std::ceil(sanitized));

            if (scaledIterations > effectiveIterations)
            {
                effectiveIterations = scaledIterations;
#if defined(DNG_MEM_TRACKING) && DNG_MEM_TRACKING
                detail::CaptureBefore(beforeMono, trackingAllocator, detail::HasCaptureMonotonic<TA>{});
#endif
                totalNanoseconds = detail::Measure(callable, effectiveIterations);
#if defined(DNG_MEM_TRACKING) && DNG_MEM_TRACKING
                beforeMono = detail::CaptureAfter(
                    trackingAllocator,
                    beforeMono,
                    effectiveIterations,
                    bytesPerOp,
                    allocsPerOp,
                    detail::HasCaptureMonotonic<TA>{});
#endif
            }
        }
    }

    const double nsPerOp = totalNanoseconds / static_cast<double>(effectiveIterations);
    return BenchResult{ name, effectiveIterations, nsPerOp, bytesPerOp, allocsPerOp };
}

// === ToString ===============================================================
// Purpose : Produce a one-line textual summary without depending on the engine
//           logger subsystem.
// Contract: Accepts a valid BenchResult. Returns a std::string; allocation may
//           fail and throw (rare). Caller owns the result.
// Notes   : Prints "<tracking-off>" for memory fields when sentinels are set.
// ----------------------------------------------------------------------------
[[nodiscard]] inline std::string ToString(const BenchResult& result)
{
    std::string text;
    text.reserve(128);

    text += (result.Name ? result.Name : "<unnamed-bench>");
    text += " : ";
    detail::AppendFormatted(text, result.NsPerOp, " ns/op");

    text += " (N=";
    text += std::to_string(static_cast<unsigned long long>(result.Iterations));
    text += ")";

    text += ", bytes/op=";
    if (result.BytesPerOp >= 0.0)
        detail::AppendFormatted(text, result.BytesPerOp, nullptr);
    else
        text += "<tracking-off>";

    text += ", allocs/op=";
    if (result.AllocsPerOp >= 0.0)
        detail::AppendFormatted(text, result.AllocsPerOp, nullptr);
    else
        text += "<tracking-off>";

    return text;
}

} // namespace bench
} // namespace dng

// === DNG_BENCH Macro ========================================================
// Purpose : Convenience wrapper that forwards to dng::bench::Run while
//           preserving argument evaluation order.
// Contract: NameLiteral must be a stable c-string. Iterations forwarded as-is;
//           0 promotes to 1 per Run's documented policy. Body must be a
//           noexcept callable/lambda. Macro introduces no additional scope.
// Notes   : Only global symbol provided by this header per engine guidance.
// ----------------------------------------------------------------------------
#define DNG_BENCH(NameLiteral, Iterations, BodyLambdaLike) \
    (::dng::bench::Run((NameLiteral), (Iterations), (BodyLambdaLike)))

#if defined(DNG_COMPILE_TESTS)
static_assert(sizeof(::dng::bench::BenchResult) > 0, "BenchResult must be complete");
static_assert(noexcept(::dng::bench::Run("noop", 1, []() noexcept {})), "Run must be noexcept");
#endif

// ---------------------------------------------------------------------------
// Example Usage (commented)
// ---------------------------------------------------------------------------
// auto vectorBench = DNG_BENCH("StdVector PushPop", 1'000'000, [&]() noexcept {
//     vec.push_back(42);
//     vec.pop_back();
// });
//
// auto arenaBench = DNG_BENCH("Arena Allocate/Rewind", 500'000, [&]() noexcept {
//     auto marker = arena.GetMarker();
//     void* block = arena.AllocateBytes(64, alignof(std::max_align_t));
//     DNG_CHECK(block != nullptr);
//     arena.Rewind(marker);
// });
