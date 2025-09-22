#pragma once

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
    struct SingleThreadedPolicy {
        struct Mutex { /* empty */ };
        struct Lock { explicit Lock(Mutex&) noexcept {} };

        template <typename T>
        using Counter = CounterImpl<T, false>; // plain integral

        static constexpr bool kIsThreadSafe = false;
    };

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

    // --------------------------------------------------------------
    // ThreadSafeAllocator wrapper
    // - Wraps any Underlying allocator type (must model IAllocator)
    // - Adds coarse-grained locking on public Allocate/Deallocate/Reallocate
    // - Provides thread-safe stats
    // - Exposes Reset() iff underlying has Reset()
    // --------------------------------------------------------------
    template <typename Underlying, typename Policy = DefaultThreadPolicy>
    class ThreadSafeAllocator final : public IAllocator {
    public:
        using ThisType = ThreadSafeAllocator<Underlying, Policy>;
        using Mutex = typename Policy::Mutex;
        using Lock = typename Policy::Lock;

        static constexpr bool IsThreadSafe() noexcept { return Policy::kIsThreadSafe; }

        // Convenience: forward-construct underlying allocator
        template <typename... Args>
        explicit ThreadSafeAllocator(Args&&... args)
            : m_underlying(std::forward<Args>(args)...) {
        }

        // IAllocator API
        void* Allocate(usize size, usize alignment = alignof(std::max_align_t)) noexcept override {
            alignment = NormalizeAlignment(alignment);
            Lock guard(m_mutex);
            void* p = m_underlying.Allocate(size, alignment);
            if (p) {
                stats_on_alloc(size);
            }
            return p;
        }

        void  Deallocate(void* ptr, usize size, usize alignment = alignof(std::max_align_t)) noexcept override {
            if (!ptr) return;
            alignment = NormalizeAlignment(alignment);
            Lock guard(m_mutex);
            m_underlying.Deallocate(ptr, size, alignment);
            stats_on_free(size);
        }

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

        // Access to the underlying allocator (NOT thread-safe).
        // Use with care - typically only for diagnostics or one-off configuration.
        Underlying& GetUnderlying()       noexcept { return m_underlying; }
        const Underlying& GetUnderlying() const noexcept { return m_underlying; }

        // Thread-safe Reset() only if Underlying provides Reset()
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

        // Statistics (thread-safe to read)
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


