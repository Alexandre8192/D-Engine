#pragma once
// ============================================================================
// D-Engine - Core/Memory/ArenaAllocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a deterministic bump allocator with marker-based rewind so
//           hot paths can reserve transient memory without per-block frees.
//           Uses the engine logging front-end; no local fallbacks.
// Contract: All requests normalise `alignment` through `NormalizeAlignment` and
//           require callers to release memory via `Reset()` or `Rewind(marker)`;
//           `Deallocate()` is intentionally a no-op and exists only to satisfy
//           `IAllocator`. The allocator is not thread-safe. This header requires
//           the logging front-end via Logger.hpp; no macro redefinition occurs here.
// Notes   : Designed for frame or scope-local allocations. Markers capture the
//           current offset so rewinding is O(1). Peak usage is tracked for
//           diagnostics. Backing storage can be owned (parent allocator) or
//           provided externally. We avoid shadowing `DNG_LOG_*` to prevent silent
//           diagnostic loss due to include order.
// ============================================================================

#include "Core/Logger.hpp"
#include "Core/Types.hpp"
#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/OOM.hpp"

#include <algorithm> // std::min
#include <cstdint>   // std::uintptr_t
#include <climits>   // SIZE_MAX
#include <utility>   // std::exchange

// Minimal fallback for unused parameter macro (keeps this header self-contained)
#ifndef DNG_UNUSED
#define DNG_UNUSED(x) (void)(x)
#endif

namespace dng::core {

    // --- ArenaMarker --------------------------------------------------------
    // Purpose : Compact handle storing the arena offset captured before an
    //           allocation so callers can rewind.
    // Contract: Only markers obtained from this allocator are meaningful. Thread
    //           affinity follows the owning allocator (single-threaded expectation).
    // Notes   : An invalid marker carries SIZE_MAX and is ignored by Rewind().
    class ArenaMarker {
    private:
        usize m_offset;
        friend class ArenaAllocator;
        explicit ArenaMarker(usize offset) noexcept : m_offset(offset) {}
    public:
        ArenaMarker() noexcept : m_offset(SIZE_MAX) {}
        [[nodiscard]] bool IsValid() const noexcept { return m_offset != SIZE_MAX; }
        [[nodiscard]] usize GetOffset() const noexcept { return m_offset; }
    };

    // --- ArenaAllocator -----------------------------------------------------
    // Purpose : Lightweight bump allocator with optional ownership of the
    //           backing buffer.
    // Contract: `Allocate` honours `NormalizeAlignment` and returns nullptr on
    //           exhaustion (OOM policy invoked). Memory must be released via
    //           `Reset()` or `Rewind(marker)`; `Deallocate()` is a documented
    //           no-op.
    // Notes   : Not thread-safe. Tracking fields (`m_peakUsed`) are updated
    //           opportunistically for diagnostics. Use ScopedMarker to wrap
    //           transient regions so unwinding paths automatically rewind.
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
        // --- ScopedMarker -------------------------------------------------
        // Purpose : RAII helper capturing the current marker and rewinding on
        //           scope exit (including unwinding paths).
        // Contract: ArenaAllocator must outlive the ScopedMarker; moves transfer
        //           ownership and leave the source inactive. Copying is disabled.
        // Notes   : Thread-safety matches ArenaAllocator (single-owner expected).
        class ScopedMarker {
        public:
            explicit ScopedMarker(ArenaAllocator& arena) noexcept
                : m_arena(&arena)
                , m_marker(arena.GetMarker())
            {
            }

            ScopedMarker(const ScopedMarker&) = delete;
            ScopedMarker& operator=(const ScopedMarker&) = delete;

            ScopedMarker(ScopedMarker&& other) noexcept
                : m_arena(std::exchange(other.m_arena, nullptr))
                , m_marker(other.m_marker)
            {
            }

            ScopedMarker& operator=(ScopedMarker&& other) noexcept
            {
                if (this != &other)
                {
                    Release();
                    m_arena = std::exchange(other.m_arena, nullptr);
                    m_marker = other.m_marker;
                }
                return *this;
            }

            ~ScopedMarker() noexcept
            {
                Release();
            }

            void Release() noexcept
            {
                if (m_arena && m_marker.IsValid())
                {
                    m_arena->Rewind(m_marker);
                }
                m_arena = nullptr;
            }

            [[nodiscard]] bool IsActive() const noexcept { return m_arena != nullptr; }
            [[nodiscard]] ArenaMarker GetMarker() const noexcept { return m_marker; }

        private:
            ArenaAllocator* m_arena;
            ArenaMarker     m_marker;
        };

        // ---
        // Purpose : Construct an arena that acquires its backing buffer from a parent allocator.
        // Contract: `parentAllocator` must outlive the arena; `capacity` > 0; allocation happens immediately and honours NormalizeAlignment.
        // Notes   : Ownership flag tracks whether destruction should return memory to the parent allocator.
        // ---
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

        // ---
        // Purpose : Bind the arena to caller-supplied storage without taking ownership.
        // Contract: `buffer` must remain valid for the allocator lifetime; `size` > 0; no allocation occurs inside the constructor.
        // Notes   : Subsequent Reset/Rewind operations never touch the parent allocator because memory is external.
        // ---
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

        // ---
        // Purpose : Emit diagnostics and release owned backing storage when the arena goes out of scope.
        // Contract: Noexcept; only returns memory when `m_ownsMemory` is true and the parent allocator is still available.
        // Notes   : External buffers remain untouched; logging helps correlate shutdown ordering.
        // ---
        ~ArenaAllocator() noexcept override {
            if (m_ownsMemory && m_parentAllocator && m_base) {
                DNG_LOG_INFO("Memory.Arena", "~ArenaAllocator releasing {} bytes (base={}, ownsMemory=1)",
                    static_cast<unsigned long long>(m_capacity),
                    static_cast<const void*>(m_base));
                m_parentAllocator->Deallocate(m_base, m_capacity, alignof(std::max_align_t));
                DNG_LOG_INFO("Memory.Arena", "~ArenaAllocator release complete for base={}", static_cast<const void*>(m_base));
            }
        }

        ArenaAllocator(const ArenaAllocator&) = delete;
        ArenaAllocator& operator=(const ArenaAllocator&) = delete;
        ArenaAllocator(ArenaAllocator&&) = delete;
        ArenaAllocator& operator=(ArenaAllocator&&) = delete;

        // ---
        // Purpose : Hand out contiguous slices from the bump pointer while honouring alignment.
        // Contract: `size` > 0; alignment normalised before use; returns nullptr and triggers OOM policy when exhausted.
        // Notes   : Single-threaded; does not grow beyond the initial buffer and never returns memory to the parent until destruction.
        // ---
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
        // ---
        // Purpose : Satisfy the IAllocator interface; arenas do not support per-block frees.
        // Contract: Safe to call with any pointer; parameters are ignored; no ownership transfer occurs.
        // Notes   : Callers must use Reset/Rewind for reclamation; consider enabling strict asserts if accidental usage is common.
        // ---
        void Deallocate(void* ptr, usize size = 0, usize alignment = alignof(std::max_align_t)) noexcept override {
            DNG_UNUSED(ptr);
            DNG_UNUSED(size);
            DNG_UNUSED(alignment);
            // no-op (by design). Consider DNG_ASSERT(false) if you prefer hard-fail in Debug.
        }

        // Utility (not part of IAllocator); keep without 'override'
        // ---
        // Purpose : Check whether a pointer resides within the arena's backing buffer.
        // Contract: Null pointers return false; result is valid only for this allocator instance.
        // Notes   : Helpful for assertions when layering allocators or debugging stray frees.
        // ---
        [[nodiscard]] bool Owns(void* ptr) const noexcept {
            if (!ptr || !m_base || !m_end) return false;
            const uint8_t* bytePtr = static_cast<const uint8_t*>(ptr);
            return bytePtr >= m_base && bytePtr < m_end;
        }

        // =============================
        // Arena-specific API
        // =============================

        // ---
        // Purpose : Report the number of bytes consumed since the last Reset.
        // Contract: No synchronization; returns 0 when the arena is invalid.
        // Notes   : Includes alignment padding because the bump pointer monotonically increases.
        // ---
        [[nodiscard]] usize GetUsed() const noexcept {
            if (!m_base || !m_current) return 0;
            return static_cast<usize>(m_current - m_base);
        }

        // ---
        // Purpose : Expose the total byte capacity of the arena's backing buffer.
        // Contract: Constant after construction; zero when constructed with invalid input.
        // Notes   : Useful for instrumentation and guardrails around high-water usage.
        // ---
        [[nodiscard]] usize GetCapacity() const noexcept { return m_capacity; }

        // ---
        // Purpose : Observe the historical peak usage tracked opportunistically during allocations.
        // Contract: Single-threaded; peak resets when Reset() is called.
        // Notes   : May lag by one allocation if the allocator is not valid.
        // ---
        [[nodiscard]] usize GetPeak() const noexcept { return m_peakUsed; }

        // ---
        // Purpose : Return the number of bytes still available before the arena exhausts.
        // Contract: No synchronization; returns 0 when the arena is invalid.
        // Notes   : Equivalent to `capacity - used` but cheaper than recomputing externally.
        // ---
        [[nodiscard]] usize GetFree() const noexcept {
            if (!m_current || !m_end) return 0;
            return static_cast<usize>(m_end - m_current);
        }

        // ---
        // Purpose : Determine whether the arena has a usable backing buffer.
        // Contract: Returns true only when base/current/end pointers are initialised.
        // Notes   : Helpful before calling Reset/Rewind from defensive code paths.
        // ---
        [[nodiscard]] bool IsValid() const noexcept {
            return m_base && m_current && m_end;
        }

        // ---
        // Purpose : Rewind the bump pointer to the beginning and clear peak diagnostics.
        // Contract: Noexcept; safe to call repeatedly; only acts when the arena owns valid storage.
        // Notes   : Does not zero memory; callers should poison manually if required.
        // ---
        void Reset() noexcept {
            if (m_base) {
                m_current = m_base;
                m_peakUsed = 0;
            }
        }

        // ---
        // Purpose : Capture the current bump offset for later rewinds.
        // Contract: Returns an invalid marker when the arena lacks storage.
        // Notes   : Markers are cheap value types; callers are responsible for LIFO discipline.
        // ---
        [[nodiscard]] ArenaMarker GetMarker() const noexcept {
            if (!IsValid()) return ArenaMarker();
            return ArenaMarker(static_cast<usize>(m_current - m_base));
        }

        // ---
        // Purpose : Restore the bump pointer to a previously captured marker.
        // Contract: Ignores invalid allocators or markers; logs when offsets exceed capacity or move forward in time.
        // Notes   : Does not zero memory or adjust peak usage; intended for stack-like scopes on a single thread.
        // ---
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
