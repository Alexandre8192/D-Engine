#pragma once
// ============================================================================
// D-Engine - Core/Memory/SmallObjectTLSBins.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a reusable thread-local magazine for small-object allocators
//           so hot paths can service allocations from per-thread caches while
//           keeping refill logic centralized in the owning allocator.
// Contract: Header-only, self-contained, no hidden OS calls. Magazines attach to
//           an owning allocator instance that must expose FlushThreadCache() and
//           IsAlive() members. Reset() normalises batch sizes supplied by the
//           owner. Destructing a thread cache flushes cached blocks back to the
//           owner to preserve symmetry. Thread fingerprints are stable per
//           thread and never return zero so they can double as sentinels.
// Notes   : Designed for SmallObjectAllocator but reusable for other slab-backed
//           allocators that honour the same `(size, alignment)` symmetry. No
//           logging or heap usage appears on the hot path; optional counters can
//           be layered on later behind feature flags without touching this file.
// ============================================================================

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>

#if defined(DNG_MEM_PROFILE_LIGHT) && DNG_MEM_PROFILE_LIGHT
#    include <atomic>
#endif

namespace dng
{
namespace core
{

#if defined(DNG_MEM_PROFILE_LIGHT) && DNG_MEM_PROFILE_LIGHT
// Purpose : Lightweight thread-local statistics container used when the light
//           profiling gate is enabled.
// Contract: Atomic counters are updated by owners; Reset() clears them for the
//           current thread safely.
// Notes   : Keep the struct tiny so it can live inside TLS caches without
//           bloating per-thread state.
struct SmallObjectTLSProfile
{
    std::atomic<std::uint64_t> tlsHits{ 0 };
    std::atomic<std::uint64_t> tlsMisses{ 0 };

    void Reset() noexcept
    {
        tlsHits.store(0, std::memory_order_relaxed);
        tlsMisses.store(0, std::memory_order_relaxed);
    }
};
#else
struct SmallObjectTLSProfile
{
    void Reset() noexcept {}
};
#endif

// Purpose : Thread-local magazine helper parametrised by owner and node type.
// Contract: Owner must provide `void FlushThreadCache(ThreadCache&)` and
//           `[[nodiscard]] bool IsAlive() const noexcept`. NodeT must model a
//           singly-linked free-list node (`NodeT* next`).
// Notes   : `NumClasses` is the compile-time number of size-classes provided by
//           the owner (e.g., 7 for powers-of-two up to 1 KiB).
template<typename OwnerT, typename NodeT, std::size_t NumClasses>
class SmallObjectTLSBins
{
public:
    using Owner = OwnerT;
    using Node = NodeT;
    static constexpr std::uint64_t kNoThreadOwner = 0ull;

    struct ProfileSnapshot
    {
        std::uint64_t tlsHits = 0;
        std::uint64_t tlsMisses = 0;
    };

    struct ThreadCache;

    static void FlushOnThreadExit(Owner& owner, ThreadCache& cache) noexcept;

    struct Magazine
    {
        Node* Head = nullptr;
        std::size_t Count = 0;
        std::size_t Batch = 0;
        std::chrono::time_point<std::chrono::steady_clock> LastRefillTime{};
        bool HasRefilled = false;

        // Purpose : Reset the magazine to an empty state with the provided
        //           baseline batch policy.
        // Contract: `baseBatch` must be >= 1 and provided by the owning
        //           allocator. Safe to call on already-reset magazines.
        // Notes   : Time bookkeeping clears so adaptive strategies can observe
        //           future refills naturally.
        void Reset(std::size_t baseBatch) noexcept
        {
            Head = nullptr;
            Count = 0;
            Batch = baseBatch;
            LastRefillTime = {};
            HasRefilled = false;
        }
    };

    struct ThreadCache
    {
        Owner* OwnerInstance = nullptr;
        std::size_t BaseBatch = 0;
        std::array<Magazine, NumClasses> Magazines{};
        SmallObjectTLSProfile Profile{};

        ~ThreadCache() noexcept
        {
            if (OwnerInstance)
            {
                SmallObjectTLSBins::FlushOnThreadExit(*OwnerInstance, *this);
            }
            else
            {
                const std::size_t base = (BaseBatch == 0) ? 1 : BaseBatch;
                Reset(base);
            }
        }

        // Purpose : Reset all magazines to reflect the provided base batch.
        // Contract: `baseBatch` must be >= 1. Safe to invoke redundantly.
        // Notes   : Also updates the cached BaseBatch so the destructor can
        //           reuse the last known policy.
        void Reset(std::size_t baseBatch) noexcept
        {
            BaseBatch = baseBatch;
            for (Magazine& magazine : Magazines)
            {
                magazine.Reset(baseBatch);
            }
            Profile.Reset();
        }
    };

    class ThreadCacheScope final
    {
    public:
        explicit ThreadCacheScope(Owner* owner) noexcept
            : mOwner(owner)
        {
        }

        ThreadCacheScope(const ThreadCacheScope&) = delete;
        ThreadCacheScope& operator=(const ThreadCacheScope&) = delete;

        ~ThreadCacheScope() noexcept
        {
            if (mOwner)
            {
                SmallObjectTLSBins::FlushOnThreadExit(*mOwner);
            }
        }

    private:
        Owner* mOwner;
    };

    // Purpose : Access the thread-local cache instance for the current thread.
    // Contract: Returns a reference valid for the thread's lifetime. Ownership
    //           remains with the TLS subsystem; callers must not store the
    //           address beyond thread scope if the owner may change.
    // Notes   : One cache per thread per template instantiation.
    [[nodiscard]] static ThreadCache& Cache() noexcept
    {
        thread_local ThreadCache cache{};
        return cache;
    }

    // Purpose : Allow owners to request cache flushing on thread termination.
    // Contract: Safe to call multiple times; ignores unrelated owners and resets magazines.
    // Notes   : Delegates to the owning allocator's FlushThreadCache when a binding exists.
    static void FlushOnThreadExit(Owner& owner) noexcept
    {
        ThreadCache& cache = Cache();
        FlushOnThreadExit(owner, cache);
    }

    // Purpose : Produce a deterministic, non-zero fingerprint per thread.
    // Contract: Stable for the life of the thread; never returns zero so owners
    //           can treat zero as "no owner" sentinel.
    // Notes   : Uses std::hash on thread::id which is sufficient for sharding.
    [[nodiscard]] static std::uint64_t ThreadFingerprint() noexcept
    {
        thread_local const std::uint64_t value = []() noexcept {
            std::hash<std::thread::id> hasher;
            std::uint64_t hashed = static_cast<std::uint64_t>(hasher(std::this_thread::get_id()));
            return (hashed == 0ull) ? 0x1ull : hashed;
        }();
        return value;
    }

    [[nodiscard]] static ProfileSnapshot GetProfile() noexcept
    {
#if defined(DNG_MEM_PROFILE_LIGHT) && DNG_MEM_PROFILE_LIGHT
        ThreadCache& cache = Cache();
        ProfileSnapshot snapshot{};
        snapshot.tlsHits = cache.Profile.tlsHits.load(std::memory_order_relaxed);
        snapshot.tlsMisses = cache.Profile.tlsMisses.load(std::memory_order_relaxed);
        return snapshot;
#else
        return {};
#endif
    }

    // Purpose : Convenience to compare arbitrary fingerprints against the
    //           current thread's identity.
    // Contract: Returns true when `fingerprint` matches the calling thread.
    // Notes   : Intended for fast cross-thread free detection.
    [[nodiscard]] static bool MatchesCurrent(std::uint64_t fingerprint) noexcept
    {
        return fingerprint == ThreadFingerprint();
    }

    [[nodiscard]] static ThreadCacheScope MakeScope(Owner& owner) noexcept
    {
        return ThreadCacheScope(&owner);
    }
};

template<typename OwnerT, typename NodeT, std::size_t NumClasses>
void SmallObjectTLSBins<OwnerT, NodeT, NumClasses>::FlushOnThreadExit(Owner& owner, ThreadCache& cache) noexcept
{
    if (cache.OwnerInstance == &owner)
    {
        owner.FlushThreadCache(cache);
        return;
    }

    if (cache.OwnerInstance && cache.OwnerInstance->IsAlive())
    {
        cache.OwnerInstance->FlushThreadCache(cache);
        return;
    }

    cache.OwnerInstance = nullptr;
    const std::size_t base = (cache.BaseBatch == 0) ? 1 : cache.BaseBatch;
    cache.Reset(base);
}

} // namespace core
} // namespace dng
