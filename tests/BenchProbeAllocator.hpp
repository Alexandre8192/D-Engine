#pragma once
// ============================================================================
// D-Engine - tests/BenchProbeAllocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a lightweight instrumentation wrapper around an existing
//           allocator so tests and benchmarks can observe cumulative churn
//           (calls/bytes) without relying on the global TrackingAllocator.
// Contract: Forward every Allocate/Deallocate to the wrapped backend while
//           atomically counting totals. Thread-safe for concurrent alloc/free
//           pairs. The backend pointer must remain valid for the lifetime of
//           the probe.
// Notes   : Tests can sample counters via CaptureMonotonic() to compute deltas
//           across arbitrary regions. The probe does not attempt to virtualize
//           Reallocate; it simply delegates to the backend's default.
// ============================================================================

#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/Alignment.hpp"

#include <atomic>
#include <cstdint>

namespace dng
{
namespace tests
{
    struct ProbeCounters
    {
        std::uint64_t TotalAllocCalls{};
        std::uint64_t TotalFreeCalls{};
        std::uint64_t TotalBytesAllocated{};
        std::uint64_t TotalBytesFreed{};
    };

    class BenchProbeAllocator final : public dng::core::IAllocator
    {
    public:
    explicit BenchProbeAllocator(dng::core::IAllocator* backend) noexcept
            : m_backend(backend)
        {
        }

    [[nodiscard]] dng::core::IAllocator* GetBackend() const noexcept
        {
            return m_backend;
        }

    [[nodiscard]] void* Allocate(dng::core::usize size, dng::core::usize alignment) noexcept override
        {
            if (!m_backend || size == 0)
                return nullptr;

            alignment = dng::core::NormalizeAlignment(alignment);
            void* ptr = m_backend->Allocate(size, alignment);
            if (ptr)
            {
                m_allocCalls.fetch_add(1, std::memory_order_relaxed);
                m_bytesAlloc.fetch_add(static_cast<std::uint64_t>(size), std::memory_order_relaxed);
            }
            return ptr;
        }

    void Deallocate(void* ptr, dng::core::usize size, dng::core::usize alignment) noexcept override
        {
            if (!m_backend || !ptr)
                return;

            alignment = dng::core::NormalizeAlignment(alignment);
            m_backend->Deallocate(ptr, size, alignment);
            m_freeCalls.fetch_add(1, std::memory_order_relaxed);
            m_bytesFree.fetch_add(static_cast<std::uint64_t>(size), std::memory_order_relaxed);
        }

        [[nodiscard]] ProbeCounters CaptureMonotonic() const noexcept
        {
            ProbeCounters snapshot{};
            snapshot.TotalAllocCalls     = m_allocCalls.load(std::memory_order_relaxed);
            snapshot.TotalFreeCalls      = m_freeCalls.load(std::memory_order_relaxed);
            snapshot.TotalBytesAllocated = m_bytesAlloc.load(std::memory_order_relaxed);
            snapshot.TotalBytesFreed     = m_bytesFree.load(std::memory_order_relaxed);
            return snapshot;
        }

        [[nodiscard]] void* AllocateBytes(std::size_t size, std::size_t alignment) noexcept
        {
            return Allocate(static_cast<dng::core::usize>(size), static_cast<dng::core::usize>(alignment));
        }

        void DeallocateBytes(void* ptr, std::size_t size, std::size_t alignment) noexcept
        {
            Deallocate(ptr, static_cast<dng::core::usize>(size), static_cast<dng::core::usize>(alignment));
        }

    private:
        dng::core::IAllocator* m_backend{};
        std::atomic<std::uint64_t> m_allocCalls{0};
        std::atomic<std::uint64_t> m_freeCalls{0};
        std::atomic<std::uint64_t> m_bytesAlloc{0};
        std::atomic<std::uint64_t> m_bytesFree{0};
    };

} // namespace tests
} // namespace dng
