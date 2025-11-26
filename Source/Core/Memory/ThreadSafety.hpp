#pragma once
// ============================================================================
// D-Engine - Core/Memory/ThreadSafety.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide thread-safe allocator wrappers and counters that compose
//           with existing D-Engine allocators.
// Contract: Header-only, self-contained, and noexcept where possible. No
//           exceptions or RTTI. Wrappers forward allocation requests to the
//           underlying allocator while adding synchronization and optional
//           statistics.
// Notes   : Intended for cases where explicit locking is acceptable. For
//           lock-free or sharded designs, prefer dedicated allocators.
// ============================================================================

#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/MemoryConfig.hpp"

#include <atomic>
#include <mutex>
#include <type_traits>
#include <utility>
#include <cstddef>
#include <cassert>

// Forward declarations for convenience aliases

namespace dng::core {
    class DefaultAllocator;
    class ArenaAllocator;
    class StackAllocator;
    class PoolAllocator;


    // --------------------------------------------------------------
    // Small helper: concept to detect Reset() on the underlying type
    // --------------------------------------------------------------
    template <class T>
    concept HasReset = requires(T t) { t.Reset(); };

    // --------------------------------------------------------------
    // Counter wrapper: uniform API for single-threaded & atomic cases
    // --------------------------------------------------------------
    template <typename T, bool Atomic>
    class CounterImpl;

    template <typename T>
    class CounterImpl<T, false> {
        static_assert(std::is_integral_v<T>, "CounterImpl expects integral type");
        T value_{ 0 };
    public:
        CounterImpl() = default;
        explicit CounterImpl(T v) : value_(v) {}

        T load(std::memory_order = std::memory_order_relaxed) const noexcept { return value_; }
        void store(T v, std::memory_order = std::memory_order_relaxed) noexcept { value_ = v; }

        T fetch_add(T v, std::memory_order = std::memory_order_relaxed) noexcept { T old = value_; value_ += v; return old; }
        T fetch_sub(T v, std::memory_order = std::memory_order_relaxed) noexcept { T old = value_; value_ -= v; return old; }

        CounterImpl& operator+=(T v) noexcept { value_ += v; return *this; }
        CounterImpl& operator-=(T v) noexcept { value_ -= v; return *this; }

        operator T() const noexcept { return value_; }
    };

    template <typename T>
    class CounterImpl<T, true> {
        static_assert(std::is_integral_v<T>, "CounterImpl expects integral type");
        std::atomic<T> value_{ 0 };
    public:
        CounterImpl() = default;
        explicit CounterImpl(T v) : value_(v) {}

        T load(std::memory_order order = std::memory_order_relaxed) const noexcept { return value_.load(order); }
        void store(T v, std::memory_order order = std::memory_order_relaxed) noexcept { value_.store(v, order); }

        T fetch_add(T v, std::memory_order order = std::memory_order_relaxed) noexcept { return value_.fetch_add(v, order); }
        T fetch_sub(T v, std::memory_order order = std::memory_order_relaxed) noexcept { return value_.fetch_sub(v, order); }

        CounterImpl& operator+=(T v) noexcept { value_.fetch_add(v, std::memory_order_relaxed); return *this; }
        CounterImpl& operator-=(T v) noexcept { value_.fetch_sub(v, std::memory_order_relaxed); return *this; }

        operator T() const noexcept { return value_.load(std::memory_order_relaxed); }
    };

    // --------------------------------------------------------------
    // Policies
    // --------------------------------------------------------------
    // ---
    // Purpose : Provide a zero-cost policy that assumes single-threaded access.
    // Contract: No locking is performed; counters remain plain integrals; caller must enforce exclusivity.
    // Notes   : Useful for debug builds where determinism matters more than concurrency.
    // ---
    struct SingleThreadedPolicy {
        struct Mutex { /* empty */ };
        struct Lock { explicit Lock(Mutex&) noexcept {} };

        template <typename T>
        using Counter = CounterImpl<T, false>; // plain integral

        static constexpr bool kIsThreadSafe = false;
    };

    // ---
    // Purpose : Wrap allocator operations with a std::mutex-based critical section.
    // Contract: Suitable for coarse-grained synchronization; incurs std::mutex overhead on every call.
    // Notes   : Counter implementation switches to atomics to maintain thread-safe stats.
    // ---
    struct MutexPolicy {
        using Mutex = std::mutex;
        using Lock = std::lock_guard<Mutex>;

        template <typename T>
        using Counter = CounterImpl<T, true>; // atomic integral

        static constexpr bool kIsThreadSafe = true;
    };

    // --------------------------------------------------------------
    // DefaultThreadPolicy selection from config macros
    // DNG_MEM_THREAD_SAFE (0/1)
    // DNG_MEM_THREAD_POLICY: 0 = SingleThreaded, 1 = Mutex (extendable)
    // --------------------------------------------------------------
#if defined(DNG_MEM_THREAD_SAFE) && (DNG_MEM_THREAD_SAFE)
#if defined(DNG_MEM_THREAD_POLICY) && (DNG_MEM_THREAD_POLICY == 1)
    using DefaultThreadPolicy = MutexPolicy;
#else
    using DefaultThreadPolicy = SingleThreadedPolicy;
#endif
#else
    using DefaultThreadPolicy = SingleThreadedPolicy;
#endif

    // ---
    // Purpose : Wrap an existing allocator with thread-safe access and optional statistics.
    // Contract: Forwards all calls to `Underlying` while serialising via `Policy`; ownership semantics match the embedded allocator instance.
    // Notes   : Compile-time policy toggle keeps the wrapper usable in both single-threaded and multithreaded deployments without code changes.
    // ---
    template <typename Underlying, typename Policy = DefaultThreadPolicy>
    class ThreadSafeAllocator final : public IAllocator {
    public:
        using ThisType = ThreadSafeAllocator<Underlying, Policy>;
        using Mutex = typename Policy::Mutex;
        using Lock = typename Policy::Lock;

        // ---
        // Purpose : Surface whether the chosen policy actually performs locking.
        // Contract: constexpr and noexcept; callers may branch on the result for diagnostics.
        // Notes   : Mirrors Policy::kIsThreadSafe for convenience.
        // ---
        static constexpr bool IsThreadSafe() noexcept { return Policy::kIsThreadSafe; }

        // ---
        // Purpose : Construct the wrapper by forwarding arguments to the underlying allocator.
        // Contract: Perfect-forwards `Args...` to `Underlying`; caller controls ownership semantics.
        // Notes   : No locking occurs during construction; statistics are zero-initialised.
        // ---
        template <typename... Args>
        explicit ThreadSafeAllocator(Args&&... args)
            : m_underlying(std::forward<Args>(args)...) {
        }

        // ---
        // Purpose : Serialize allocation requests and record high-level statistics.
        // Contract: Normalizes alignment before delegating; locks for the duration of the call; stats updated only when allocation succeeds.
        // Notes   : Does not change allocator behaviour beyond locking and counters.
        // ---
        void* Allocate(usize size, usize alignment = alignof(std::max_align_t)) noexcept override {
            alignment = NormalizeAlignment(alignment);
            Lock guard(m_mutex);
            void* p = m_underlying.Allocate(size, alignment);
            if (p) {
                stats_on_alloc(size);
            }
            return p;
        }

        // ---
        // Purpose : Serialize deallocation requests and keep live-allocation counters in sync.
        // Contract: Ignores null pointers; normalizes alignment; lock spans the entire call into the underlying allocator.
        // Notes   : Counters assume callers pass the original `size` tuple as mandated by the allocator contract.
        // ---
        void  Deallocate(void* ptr, usize size, usize alignment = alignof(std::max_align_t)) noexcept override {
            if (!ptr) return;
            alignment = NormalizeAlignment(alignment);
            Lock guard(m_mutex);
            m_underlying.Deallocate(ptr, size, alignment);
            stats_on_free(size);
        }

        // ---
        // Purpose : Provide a thread-safe bridge for reallocation while tracking size deltas.
        // Contract: Locks around the entire operation; forwards directly to the underlying allocator; updates stats based on the outcome.
        // Notes   : Does not attempt to optimise for the in-place case beyond relaying `wasInPlace`.
        // ---
        void* Reallocate(void* ptr,
            usize oldSize,
            usize newSize,
            usize alignment = alignof(std::max_align_t),
            bool* wasInPlace = nullptr) noexcept override
        {
            alignment = NormalizeAlignment(alignment);
            Lock guard(m_mutex);

            // Update stats conservatively around the underlying call.
            // We forward to underlying::Reallocate so that specialized in-place logic can happen.
            void* newPtr = m_underlying.Reallocate(ptr, oldSize, newSize, alignment, wasInPlace);

            if (!ptr && newPtr) {
                // pure allocation
                stats_on_alloc(newSize);
            }
            else if (ptr && !newPtr) {
                // failed reallocation -> no stat change (original remains valid)
            }
            else if (ptr && newPtr) {
                // realloc success: adjust current bytes by delta
                if (newSize > oldSize) {
                    stats_on_alloc(newSize - oldSize);
                }
                else if (oldSize > newSize) {
                    stats_on_free(oldSize - newSize);
                }
            }
            return newPtr;
        }

        // ---
        // Purpose : Expose mutable access to the wrapped allocator when callers need advanced configuration.
        // Contract: Not thread-safe; callers must synchronize externally before invoking methods on the returned reference.
        // Notes   : Primarily for diagnostics or out-of-band setup.
        // ---
        Underlying& GetUnderlying()       noexcept { return m_underlying; }
        // ---
        // Purpose : Provide read-only access to the wrapped allocator for inspection.
        // Contract: Mirrors the non-const overload; no synchronization is performed.
        // Notes   : Use sparingly in multithreaded contexts.
        // ---
        const Underlying& GetUnderlying() const noexcept { return m_underlying; }

        // ---
        // Purpose : Reset the underlying allocator when it supports that API while clearing live-stat counters.
        // Contract: Available only when `Underlying` models `HasReset`; acquires the policy lock before invoking `Reset`.
        // Notes   : Peak/total counters remain for post-mortem analysis; only current metrics are reset.
        // ---
        template <typename U = Underlying>
            requires HasReset<U>
        void Reset() {
            Lock guard(m_mutex);
            m_underlying.Reset();
            // Reset stats that represent "live memory"
            m_currentAllocations.store(0);
            m_currentBytes.store(0);
            // Keep total/peak for post-mortem stats
        }

        // ---
        // Purpose : Report aggregate statistics gathered since construction.
        // Contract: Lock-free loads; values are approximate when other threads update concurrently but never undefined.
        // Notes   : Useful for diagnostics dashboards or leak detectors.
        // ---
        usize GetTotalAllocations()  const noexcept { return m_totalAllocations.load(); }
        usize GetCurrentAllocations()const noexcept { return m_currentAllocations.load(); }
        usize GetTotalBytes()        const noexcept { return m_totalBytes.load(); }
        usize GetCurrentBytes()      const noexcept { return m_currentBytes.load(); }
        usize GetPeakBytes()         const noexcept { return m_peakBytes.load(); }

    private:
        // --- Stats helpers (called under lock) ---
        void stats_on_alloc(usize size) noexcept {
            m_totalAllocations += 1;
            m_currentAllocations += 1;

            m_totalBytes += static_cast<usize>(size);
            const usize cur = (m_currentBytes += static_cast<usize>(size)).load();
            // peak = max(peak, cur)
            usize peak = m_peakBytes.load();
            if (cur > peak) {
                m_peakBytes.store(cur);
            }
        }

        void stats_on_free(usize size) noexcept {
            // Current allocations/bytes should never underflow if client code passes correct sizes.
            m_currentAllocations -= 1;
            (void)m_currentBytes.fetch_sub(static_cast<usize>(size));
        }

    private:
        Underlying m_underlying;

        // Coarse-grain lock guarding the underlying allocator and stat updates
        mutable Mutex m_mutex;

        // Stats
        typename Policy::template Counter<usize> m_totalAllocations{ 0 };
        typename Policy::template Counter<usize> m_currentAllocations{ 0 };
        typename Policy::template Counter<usize> m_totalBytes{ 0 };
        typename Policy::template Counter<usize> m_currentBytes{ 0 };
        typename Policy::template Counter<usize> m_peakBytes{ 0 };
    };

    // --------------------------------------------------------------
    // Convenience aliases
    // --------------------------------------------------------------
    using ThreadSafeDefaultAllocator = ThreadSafeAllocator<DefaultAllocator>;
    using ThreadSafeArenaAllocator = ThreadSafeAllocator<ArenaAllocator>;
    using ThreadSafeStackAllocator = ThreadSafeAllocator<StackAllocator>;
    using ThreadSafePoolAllocator = ThreadSafeAllocator<PoolAllocator>;

} // namespace dng::core
