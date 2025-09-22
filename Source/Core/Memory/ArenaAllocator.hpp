#pragma once

#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/OOM.hpp"

#include <algorithm> // std::min
#include <cstdint>   // std::uintptr_t
#include <climits>   // SIZE_MAX

// Temporary logging macros until Logger.hpp is implemented
#ifndef DNG_LOG_ERROR
#define DNG_LOG_ERROR(category, msg, ...) ((void)0)
#endif
#ifndef DNG_LOG_WARNING
#define DNG_LOG_WARNING(category, msg, ...) ((void)0)
#endif
#ifndef DNG_LOG_FATAL
#define DNG_LOG_FATAL(category, msg, ...) ((void)0)
#endif

// Minimal fallback for unused parameter macro (keeps this header self-contained)
#ifndef DNG_UNUSED
#define DNG_UNUSED(x) (void)(x)
#endif

namespace dng::core {

    /**
     * @brief Opaque marker type for arena position tracking.
     */
    class ArenaMarker {
    private:
        usize m_offset;
        friend class ArenaAllocator;
        explicit ArenaMarker(usize offset) noexcept : m_offset(offset) {}
    public:
        ArenaMarker() noexcept : m_offset(SIZE_MAX) {}
        bool IsValid() const noexcept { return m_offset != SIZE_MAX; }
        usize GetOffset() const noexcept { return m_offset; }
    };

    /**
     * @brief Linear allocator with bump-pointer allocation and reset/rewind.
     */
    class ArenaAllocator : public IAllocator {
    private:
        uint8_t* m_base;
        uint8_t* m_current;
        uint8_t* m_end;
        usize    m_capacity;
        usize    m_peakUsed;

        IAllocator* m_parentAllocator;
        bool        m_ownsMemory;

        void UpdatePeakUsage() noexcept {
            const usize currentUsed = GetUsed();
            if (currentUsed > m_peakUsed) {
                m_peakUsed = currentUsed;
            }
        }

public:
        // Owns its backing store (allocated via parentAllocator)
        ArenaAllocator(IAllocator* parentAllocator, usize capacity) noexcept
            : m_base(nullptr)
            , m_current(nullptr)
            , m_end(nullptr)
            , m_capacity(capacity)
            , m_peakUsed(0)
            , m_parentAllocator(parentAllocator)
            , m_ownsMemory(true)
        {
            DNG_CHECK(parentAllocator != nullptr);
            DNG_CHECK(capacity > 0);

            if (parentAllocator && capacity > 0) {
                m_base = static_cast<uint8_t*>(
                    parentAllocator->Allocate(capacity, alignof(std::max_align_t))
                    );
                if (m_base) {
                    m_current = m_base;
                    m_end = m_base + capacity;
                }
                else {
#if DNG_MEM_FATAL_ON_OOM
                    DNG_LOG_FATAL("Memory", "Failed to allocate arena backing store of {} bytes", capacity);
#else
                    DNG_LOG_ERROR("Memory", "Failed to allocate arena backing store of {} bytes", capacity);
#endif
                }
            }
        }

        // Wrap an external buffer (does not own memory)
        ArenaAllocator(void* buffer, usize size) noexcept
            : m_base(static_cast<uint8_t*>(buffer))
            , m_current(static_cast<uint8_t*>(buffer))
            , m_end(static_cast<uint8_t*>(buffer) + size)
            , m_capacity(size)
            , m_peakUsed(0)
            , m_parentAllocator(nullptr)
            , m_ownsMemory(false)
        {
            DNG_CHECK(buffer != nullptr);
            DNG_CHECK(size > 0);
            if (!buffer || size == 0) {
                m_base = m_current = m_end = nullptr;
                m_capacity = 0;
            }
        }

        ~ArenaAllocator() noexcept override {
            if (m_ownsMemory && m_parentAllocator && m_base) {
                m_parentAllocator->Deallocate(m_base, m_capacity, alignof(std::max_align_t));
            }
        }

        ArenaAllocator(const ArenaAllocator&) = delete;
        ArenaAllocator& operator=(const ArenaAllocator&) = delete;
        ArenaAllocator(ArenaAllocator&&) = delete;
        ArenaAllocator& operator=(ArenaAllocator&&) = delete;

        [[nodiscard]] void* Allocate(usize size, usize alignment = alignof(std::max_align_t)) noexcept override {
            if (size == 0) return nullptr;

            alignment = NormalizeAlignment(alignment);

            if (!m_base || !m_current || !m_end) return nullptr;

            const std::uintptr_t currentAddr = reinterpret_cast<std::uintptr_t>(m_current);
            const std::uintptr_t alignedAddr = static_cast<std::uintptr_t>(
                AlignUp<usize>(static_cast<usize>(currentAddr), alignment)
                );

            const usize alignmentPadding = static_cast<usize>(alignedAddr - currentAddr);

            // Overflow guard on alignmentPadding + size
            if (size > (static_cast<usize>(SIZE_MAX) - alignmentPadding)) {
                // Treat as OOM for consistency with policy
                DNG_MEM_CHECK_OOM(size, alignment, "ArenaAllocator::Allocate");
                return nullptr;
            }

            const usize totalSize = alignmentPadding + size;
            const usize availableSpace = static_cast<usize>(m_end - m_current);

            if (totalSize > availableSpace) {
                DNG_LOG_WARNING("Memory", "ArenaAllocator: Insufficient space. Requested {}, Available {}",
                    totalSize, availableSpace);

                // Trigger OOM policy for the arena (log/fatal as configured).
                DNG_MEM_CHECK_OOM(size, alignment, "ArenaAllocator::Allocate");
                return nullptr;
            }

            void* result = reinterpret_cast<void*>(alignedAddr);
            m_current = reinterpret_cast<uint8_t*>(const_cast<void*>(result)) + size;

            UpdatePeakUsage();
            return result;
        }

        // Note: Individual deallocation is not supported for Arena/Stack.
        // Deallocate is a no-op kept for IAllocator compatibility.
        // Use Reset()/Rewind(marker) [Arena] or Pop(marker) [Stack].
        void Deallocate(void* ptr, usize size = 0, usize alignment = alignof(std::max_align_t)) noexcept override {
            DNG_UNUSED(ptr);
            DNG_UNUSED(size);
            DNG_UNUSED(alignment);
            // no-op (by design). Consider DNG_ASSERT(false) if you prefer hard-fail in Debug.
        }

        // Utility (not part of IAllocator); keep without 'override'
        bool Owns(void* ptr) const noexcept {
            if (!ptr || !m_base || !m_end) return false;
            const uint8_t* bytePtr = static_cast<const uint8_t*>(ptr);
            return bytePtr >= m_base && bytePtr < m_end;
        }

        // =============================
        // Arena-specific API
        // =============================

        usize GetUsed() const noexcept {
            if (!m_base || !m_current) return 0;
            return static_cast<usize>(m_current - m_base);
        }

        usize GetCapacity() const noexcept { return m_capacity; }
        usize GetPeak() const noexcept { return m_peakUsed; }

        usize GetFree() const noexcept {
            if (!m_current || !m_end) return 0;
            return static_cast<usize>(m_end - m_current);
        }

        bool IsValid() const noexcept {
            return m_base && m_current && m_end;
        }

        void Reset() noexcept {
            if (m_base) {
                m_current = m_base;
                m_peakUsed = 0;
            }
        }

        ArenaMarker GetMarker() const noexcept {
            if (!IsValid()) return ArenaMarker();
            return ArenaMarker(static_cast<usize>(m_current - m_base));
        }

        void Rewind(const ArenaMarker& marker) noexcept {
            if (!IsValid() || !marker.IsValid()) return;
            if (marker.GetOffset() > m_capacity) {
                DNG_LOG_WARNING("Memory", "ArenaAllocator: Invalid marker offset {} exceeds capacity {}",
                    marker.GetOffset(), m_capacity);
                return;
            }
            const usize currentOffset = static_cast<usize>(m_current - m_base);
            if (marker.GetOffset() > currentOffset) {
                DNG_LOG_WARNING("Memory", "ArenaAllocator: Marker offset {} ahead of current position {}",
                    marker.GetOffset(), currentOffset);
                return;
            }
            m_current = m_base + marker.GetOffset();
        }
    };

} // namespace dng::core



