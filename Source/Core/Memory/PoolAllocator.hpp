#pragma once
// ============================================================================
// D-Engine - Core/Memory/PoolAllocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Serve fixed-size allocations with O(1) free-list performance while
//           respecting the engine-wide allocator contract. Ideal for hot-path
//           systems that repeatedly request equally-sized objects (e.g., ECS
//           components, job descriptors).
// Contract: Not thread-safe. Blocks are homogeneous: Allocate() succeeds only
//           when `size == GetBlockSize()` and the caller provides an alignment
//           that does not exceed GetBlockAlignment(). Deallocate() must receive
//           the original (size, alignment). Exhaustion routes through
//           DNG_MEM_CHECK_OOM so policy toggles remain in control.
// Notes   : The allocator can either own its backing store (via a parent
//           allocator) or wrap an externally managed buffer. Free-list nodes
//           are embedded in freed blocks to avoid extra metadata.
// ============================================================================

#include "Core/Diagnostics/Check.hpp"
#include "Core/Logger.hpp"
#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/OOM.hpp"
#include "Core/Platform/PlatformCompiler.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace dng::core {

    // ------------------------------------------------------------------------
    // PoolAllocator
    // ------------------------------------------------------------------------
    // Purpose : Provide fixed-size allocations backed by an intrusive
    //           free-list for predictable latency.
    // Contract: Not thread-safe. Allocate() succeeds only when `size` matches
    //           `GetBlockSize()` and `alignment` (normalized) is <=
    //           `GetBlockAlignment()`. Deallocate() requires the same
    //           `(size, alignment)` pair; mismatches are diagnosed in debug
    //           builds but intentionally ignored in release to avoid UB. When
    //           the pool exhausts, DNG_MEM_CHECK_OOM fires.
    // Notes   : Free nodes reuse the payload storage (no extra headers).
    //           Blocks can come from an owning parent allocator or an external
    //           user-provided buffer.
    // ------------------------------------------------------------------------
    class PoolAllocator : public IAllocator {

    private:

        struct FreeNode { FreeNode* next; };

        std::uint8_t* m_buffer = nullptr;   // raw backing store (owned if m_parent != nullptr)
        std::uint8_t* m_poolStart = nullptr; // aligned start of usable pool memory
        usize         m_bufferSize = 0;      // bytes reserved from parent/external buffer
        usize         m_capacity = 0;        // total bytes available for pool blocks
        usize         m_blockSize = 0;       // requested/public block size
        usize         m_blockAlign = 0;      // alignment guarantee for blocks
        usize         m_stride = 0;          // internal stride (>= blockSize, multiple of alignment)
        usize         m_blockCount = 0;      // number of blocks
        usize         m_freeCount = 0;       // number of free blocks
        FreeNode*     m_freeList = nullptr;  // head of free list

        IAllocator* m_parent = nullptr;  // parent allocator for backing store (may be null if external buffer)

        bool          m_ownsMemory = false;

        // Compute stride as blockSize rounded up to alignment

        static DNG_FORCEINLINE usize ComputeStride(usize blockSize, usize alignment) noexcept {

            alignment = NormalizeAlignment(alignment);

            // AlignUp over unsigned integral (usize)

            const usize aligned = AlignUp<usize>(blockSize, alignment);

            return aligned;

        }

        static DNG_FORCEINLINE bool PtrInRange(const void* p, const void* base, usize cap) noexcept {

            auto pb = reinterpret_cast<const std::uint8_t*>(base);

            auto pu = reinterpret_cast<const std::uint8_t*>(p);

            return pu >= pb && pu < (pb + cap);

        }

        void BuildFreeList() noexcept {
            m_freeList = nullptr;
            m_freeCount = 0;
            m_poolStart = nullptr;

            if (!m_buffer || m_stride == 0) {
                m_blockCount = 0;
                m_capacity = 0;
                return;
            }

            const std::uintptr_t rawStart = reinterpret_cast<std::uintptr_t>(m_buffer);
            const std::uintptr_t alignedStart = AlignUp<std::uintptr_t>(rawStart, m_blockAlign);
            if (alignedStart < rawStart) {
                DNG_CHECK(false && "PoolAllocator: alignment overflow when building free list");
                m_blockCount = 0;
                m_capacity = 0;
                return;
            }

            const usize offset = static_cast<usize>(alignedStart - rawStart);
            if (offset >= m_bufferSize) {
                if (Logger::IsEnabled(LogLevel::Warn, "Memory")) {
                    DNG_LOG_WARNING("Memory", "PoolAllocator: buffer too small after alignment adjustment.");
                }
                m_blockCount = 0;
                m_capacity = 0;
                return;
            }

            const usize usableBytes = m_bufferSize - offset;
            const usize previousCount = m_blockCount;
            const usize possibleCount = usableBytes / m_stride;
            if (possibleCount == 0) {
                m_blockCount = 0;
                m_capacity = 0;
                return;
            }

            if (previousCount != 0 && possibleCount < previousCount) {
                if (Logger::IsEnabled(LogLevel::Warn, "Memory")) {
                    DNG_LOG_WARNING("Memory", "PoolAllocator: reduced block count from {} to {} due to alignment slack.", previousCount, possibleCount);
                }
            }

            m_blockCount = (previousCount == 0) ? possibleCount : ((possibleCount < previousCount) ? possibleCount : previousCount);
            m_capacity = m_blockCount * m_stride;
            m_poolStart = reinterpret_cast<std::uint8_t*>(alignedStart);

            auto* it = m_poolStart;
            for (usize i = 0; i < m_blockCount; ++i) {
                auto* node = reinterpret_cast<FreeNode*>(it);
                node->next = m_freeList;
                m_freeList = node;
                ++m_freeCount;
                it += m_stride;
            }

            DNG_CHECK(m_freeCount == m_blockCount);
        }

        [[nodiscard]] bool InitWithParent(IAllocator* parent, usize blockSize, usize blockAlignment, usize blockCount) noexcept {
            m_parent = parent;
            m_ownsMemory = (parent != nullptr);

            m_blockAlign = NormalizeAlignment(blockAlignment);
            m_blockSize = blockSize;
            m_stride = ComputeStride(blockSize, m_blockAlign);
            m_blockCount = blockCount;

            const usize maxv = (std::numeric_limits<usize>::max)();
            if (blockCount == 0 || m_stride == 0 || m_stride > maxv / blockCount) {
                DNG_MEM_CHECK_OOM(blockSize, m_blockAlign, "PoolAllocator::InitWithParent");
                return false;
            }

            const usize requestedCount = blockCount;
            const usize requiredBytes = m_stride * requestedCount;
            const usize slack = (m_blockAlign > 0) ? (m_blockAlign - 1) : 0;
            if (requiredBytes > maxv - slack) {
                DNG_MEM_CHECK_OOM(requiredBytes, m_blockAlign, "PoolAllocator::InitWithParent");
                return false;
            }

            m_capacity = requiredBytes;
            m_bufferSize = requiredBytes + slack;

            if (m_parent) {
                m_buffer = static_cast<std::uint8_t*>(
                    m_parent->Allocate(m_bufferSize, alignof(std::max_align_t))
                );
                if (!m_buffer) {
                    DNG_MEM_CHECK_OOM(m_bufferSize, alignof(std::max_align_t), "PoolAllocator::InitWithParent");
                    return false;
                }
            }

            BuildFreeList();
            return m_blockCount > 0;
        }

        [[nodiscard]] bool InitWithExternal(void* buffer, usize bufferSize, usize blockSize, usize blockAlignment) noexcept {
            m_parent = nullptr;
            m_ownsMemory = false;

            m_blockAlign = NormalizeAlignment(blockAlignment);
            m_blockSize = blockSize;
            m_stride = ComputeStride(blockSize, m_blockAlign);

            if (!buffer || bufferSize < m_stride) {
                if (Logger::IsEnabled(LogLevel::Error, "Memory")) {
                    DNG_LOG_ERROR("Memory", "PoolAllocator: invalid external buffer or too small.");
                }
                return false;
            }

            m_blockCount = static_cast<usize>(bufferSize / m_stride);
            m_bufferSize = bufferSize;
            m_buffer = static_cast<std::uint8_t*>(buffer);

            BuildFreeList();
            return m_blockCount > 0;
        }

    public:

        // Construct using a parent allocator that provides the backing store

        PoolAllocator(IAllocator* parentAllocator, usize blockSize, usize blockAlignment, usize blockCount) noexcept {

            const bool ok = InitWithParent(parentAllocator, blockSize, blockAlignment, blockCount);

            DNG_CHECK(ok);

        }

        // Construct on top of an external buffer (does not own memory)

        PoolAllocator(void* buffer, usize bufferSize, usize blockSize, usize blockAlignment) noexcept {

            const bool ok = InitWithExternal(buffer, bufferSize, blockSize, blockAlignment);

            DNG_CHECK(ok);

        }

        ~PoolAllocator() noexcept override {
            if (m_ownsMemory && m_parent && m_buffer) {
                m_parent->Deallocate(m_buffer, m_bufferSize, alignof(std::max_align_t));
            }
            m_buffer = nullptr;
            m_poolStart = nullptr;
            m_bufferSize = 0;
            m_freeList = nullptr;
            m_freeCount = 0;
            m_blockCount = 0;
            m_capacity = 0;
        }

        PoolAllocator(const PoolAllocator&) = delete;

        PoolAllocator& operator=(const PoolAllocator&) = delete;

        PoolAllocator(PoolAllocator&&) = delete;

        PoolAllocator& operator=(PoolAllocator&&) = delete;

        // IAllocator interface -------------------------------------------------

        [[nodiscard]] void* Allocate(usize size, usize alignment = alignof(std::max_align_t)) noexcept override {

            if (size == 0) return nullptr;

            alignment = NormalizeAlignment(alignment);

            // Enforce exact size (contract) and alignment compatibility.

            // We require size == m_blockSize to avoid ambiguous ownership.

            if (size != m_blockSize) {

                DNG_ASSERT(false && "PoolAllocator: size mismatch on Allocate (must equal block size).");

                return nullptr;

            }

            if (alignment > m_blockAlign || !IsPowerOfTwo(alignment)) {

                DNG_ASSERT(false && "PoolAllocator: alignment not supported by this pool.");

                return nullptr;

            }

            if (!m_freeList) {

                // Pool exhausted -> treat as allocation failure (same policy as OOM)

                DNG_MEM_CHECK_OOM(size, alignment, "PoolAllocator::Allocate");

                return nullptr;

            }

            FreeNode* node = m_freeList;

            m_freeList = node->next;

            m_freeCount--;

            return static_cast<void*>(node);

        }

        void Deallocate(void* ptr, usize size, usize alignment) noexcept override {
            if (!ptr) return;

            alignment = NormalizeAlignment(alignment);

            if (size != m_blockSize) {
                DNG_ASSERT(false && "PoolAllocator: size mismatch on Deallocate");
                return;
            }
            if (alignment > m_blockAlign || !IsPowerOfTwo(alignment)) {
                DNG_ASSERT(false && "PoolAllocator: alignment mismatch on Deallocate");
                return;
            }

            if (!PtrInRange(ptr, m_poolStart, m_capacity)) {
                DNG_ASSERT(false && "PoolAllocator: pointer does not belong to this pool");
                return;
            }
            if (!IsAligned(reinterpret_cast<std::uintptr_t>(ptr), m_blockAlign)) {
                DNG_ASSERT(false && "PoolAllocator: pointer not aligned to pool alignment");
                return;
            }

            const auto base = reinterpret_cast<std::uintptr_t>(m_poolStart);
            const auto p = reinterpret_cast<std::uintptr_t>(ptr);
            if (((p - base) % m_stride) != 0) {
                DNG_ASSERT(false && "PoolAllocator: pointer not at block start");
                return;
            }

            auto* node = static_cast<FreeNode*>(ptr);
            node->next = m_freeList;
            m_freeList = node;
            ++m_freeCount;
        }

        [[nodiscard]] void* Reallocate(void* ptr,

            usize oldSize,

            usize newSize,

            usize alignment = alignof(std::max_align_t),

            bool* wasInPlace = nullptr) noexcept override

        {

            // Pool cannot grow/shrink blocks in place; delegate to default fallback

            if (wasInPlace) *wasInPlace = false;

            return IAllocator::Reallocate(ptr, oldSize, newSize, alignment, wasInPlace);

        }

        // Pool-specific queries -----------------------------------------------

        [[nodiscard]] usize GetBlockSize() const noexcept { return m_blockSize; }

        [[nodiscard]] usize GetBlockAlignment() const noexcept { return m_blockAlign; }

        [[nodiscard]] usize GetStride() const noexcept { return m_stride; }

        [[nodiscard]] usize GetTotalBlocks() const noexcept { return m_blockCount; }

        [[nodiscard]] usize GetAvailableBlocks() const noexcept { return m_freeCount; }

        [[nodiscard]] float GetUtilization() const noexcept {

            if (m_blockCount == 0) return 0.0f;

            const usize used = m_blockCount - m_freeCount;

            return static_cast<float>(used) / static_cast<float>(m_blockCount);

        }

    };

} // namespace dng::core
