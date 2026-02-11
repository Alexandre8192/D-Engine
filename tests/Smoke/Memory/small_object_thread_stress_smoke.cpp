// ============================================================================
// SmallObject Thread Stress Smoke Test
// ----------------------------------------------------------------------------
// Purpose : Stress SmallObjectAllocator from multiple threads with and without
//           TLS bins, including cross-thread deallocation.
// Contract: No exceptions; deterministic workload; returns non-zero on failure.
// ============================================================================

#if __has_include("Core/Memory/SmallObjectAllocator.hpp")
#    include "Core/Memory/SmallObjectAllocator.hpp"
#else
#    include "../../Source/Core/Memory/SmallObjectAllocator.hpp"
#endif

#if __has_include("Core/Memory/DefaultAllocator.hpp")
#    include "Core/Memory/DefaultAllocator.hpp"
#else
#    include "../../Source/Core/Memory/DefaultAllocator.hpp"
#endif

#if __has_include("Core/Memory/TrackingAllocator.hpp")
#    include "Core/Memory/TrackingAllocator.hpp"
#else
#    include "../../Source/Core/Memory/TrackingAllocator.hpp"
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace
{
    struct Entry
    {
        void* ptr{ nullptr };
        std::size_t size{ 0 };
        std::size_t alignment{ 0 };
    };

    int RunThreadStressScenario(bool enableTLSBins)
    {
        ::dng::core::DefaultAllocator parent{};
        ::dng::core::TrackingAllocator tracking(&parent);

        ::dng::core::SmallObjectConfig cfg{};
        cfg.EnableTLSBins = enableTLSBins;
        cfg.SlabSizeBytes = 64u * 1024u;
        cfg.MaxClassSize = 256u;
        cfg.ReturnNullOnOOM = true;
        cfg.ShardCountOverride = 8u;

        constexpr std::size_t kThreadCount = 4u;
        constexpr std::size_t kAllocsPerThread = 1024u;
        constexpr std::array<std::size_t, 6> kSizes = { 16u, 24u, 32u, 48u, 64u, 128u };
        constexpr std::size_t kAlign = alignof(std::max_align_t);

        std::array<std::vector<Entry>, kThreadCount> buckets{};
        for (auto& bucket : buckets)
        {
            bucket.reserve(kAllocsPerThread);
        }

        std::atomic<int> failure{ 0 };

        {
            ::dng::core::SmallObjectAllocator allocator(&tracking, cfg);

            std::array<std::thread, kThreadCount> allocThreads{};
            for (std::size_t t = 0; t < kThreadCount; ++t)
            {
                allocThreads[t] = std::thread([&, t]() {
                    auto& bucket = buckets[t];
                    for (std::size_t i = 0; i < kAllocsPerThread; ++i)
                    {
                        const std::size_t size = kSizes[(i + t) % kSizes.size()];
                        void* const ptr = allocator.Allocate(size, kAlign);
                        if (!ptr)
                        {
                            int expected = 0;
                            (void)failure.compare_exchange_strong(expected, 1);
                            return;
                        }
                        bucket.push_back(Entry{ ptr, size, kAlign });
                    }
                });
            }

            for (auto& thread : allocThreads)
            {
                thread.join();
            }

            if (failure.load() != 0)
            {
                return 1;
            }

            // Force cross-thread frees by rotating ownership of allocation buckets.
            std::array<std::thread, kThreadCount> freeThreads{};
            for (std::size_t t = 0; t < kThreadCount; ++t)
            {
                freeThreads[t] = std::thread([&, t]() {
                    auto& bucket = buckets[(t + 1u) % kThreadCount];
                    for (const Entry& entry : bucket)
                    {
                        allocator.Deallocate(entry.ptr, entry.size, entry.alignment);
                    }
                    bucket.clear();
                });
            }

            for (auto& thread : freeThreads)
            {
                thread.join();
            }
        }

#if DNG_MEM_TRACKING
        if (tracking.GetActiveAllocationCount() != 0u)
        {
            return 2;
        }
#endif

        return 0;
    }
} // namespace

int RunSmallObjectThreadStressSmoke()
{
    const int noTLSCode = RunThreadStressScenario(false);
    if (noTLSCode != 0)
    {
        return noTLSCode;
    }

#if DNG_SMALLOBJ_TLS_BINS
    const int tlsCode = RunThreadStressScenario(true);
    if (tlsCode != 0)
    {
        return 100 + tlsCode;
    }
#endif

    return 0;
}
