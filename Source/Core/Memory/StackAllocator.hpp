#pragma once
// ============================================================================
// D-Engine - Core/Memory/StackAllocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Implement a strict LIFO allocator on top of `ArenaAllocator`, using
//           markers to guarantee constant-time pop/rewind. Uses the engine
//           logging front-end; no local fallbacks.
// Contract: All pushes normalise alignment through the arena. Callers MUST free
//           via `Pop(marker)` (thread-affine) or `Reset()`; `Deallocate()` is a
//           documented no-op preserved only for the `IAllocator` interface. This
//           header requires the logging front-end via Logger.hpp; no macro
//           redefinition occurs here.
// Notes   : Not thread-safe. Debug builds maintain a marker stack to validate
//           LIFO discipline and emit diagnostics on misuse. We avoid shadowing
//           `DNG_LOG_*` to prevent silent diagnostic loss due to include order.
// ============================================================================

#include "Core/Logger.hpp"
#include "Core/Types.hpp"
#include "Core/Memory/ArenaAllocator.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/OOM.hpp"   // Not used directly here if Arena handles OOM, but safe to include
#include <cstddef>  // std::max_align_t
#include <climits>  // SIZE_MAX
#include <cassert>  // assert

/**
 * StackAllocator
 *
 * LIFO-only allocator built on top of ArenaAllocator using markers.
 *
 * - Individual `Deallocate(ptr, size, align)` is **NOT supported** by design.
 *   Free happens via `Pop(marker)` (strict LIFO) or `Reset()` (full clear).
 *
 * Usage example:
 *   StackAllocator stack(parent, 1<<20);
 *   auto m0 = stack.Push(64);          // capture marker for 64 bytes
 *   void* p0 = stack.PushAndGetPointer(128); // get pointer directly
 *   auto m1 = stack.Push(256, 32);     // 32-aligned
 *
 *   stack.Pop(m1);                      // frees 256
 *
 *   // stack.Deallocate(...) is a NO-OP by design; prefer Pop/Reset
 *   stack.Reset();                      // clears all (warns in Debug if markers remain)
 */

namespace dng::core {

    /**
     * @brief Opaque marker for stack position tracking (captures pre-allocation position).
     *
     * Internally wraps an ArenaMarker (offset) + a stack index (Debug only).
     */
    class StackMarker {
    private:
        ArenaMarker m_arenaMarker{};
        usize       m_stackIndex{ SIZE_MAX };

        friend class StackAllocator;

        explicit StackMarker(const ArenaMarker& arenaMarker, usize stackIndex) noexcept
            : m_arenaMarker(arenaMarker), m_stackIndex(stackIndex) {
        }

    public:
        StackMarker() noexcept = default;

        [[nodiscard]] bool IsValid() const noexcept {
            return m_arenaMarker.IsValid() && m_stackIndex != SIZE_MAX;
        }

        [[nodiscard]] usize GetOffset() const noexcept { return m_arenaMarker.GetOffset(); }
        [[nodiscard]] usize GetStackIndex() const noexcept { return m_stackIndex; }
        [[nodiscard]] const ArenaMarker& GetArenaMarker() const noexcept { return m_arenaMarker; }
    };

    class StackAllocator : public ArenaAllocator {
    private:
        // Minimal marker stack used only in debug builds to validate LIFO discipline.
        // Purpose : Track the sequence of push markers to assert LIFO order on Pop.
        // Contract: Fixed-capacity buffer. On overflow, markers are no longer tracked
        //           but allocations still work; a warning is logged once.
        struct MarkerStack
        {
            static constexpr usize kMaxMarkers = CompiledStackAllocatorMaxMarkers();

            StackMarker markers[kMaxMarkers]{};
            usize       size{ 0 };
            bool        overflowed{ false };

            bool empty() const noexcept { return size == 0; }
            usize get_size() const noexcept { return size; }
            const StackMarker& back() const noexcept { return markers[size - 1]; }

            void push_back(const StackMarker& marker) noexcept
            {
                if (size >= kMaxMarkers)
                {
                    if (!overflowed)
                    {
                        overflowed = true;
                        DNG_LOG_WARNING(
                            "Memory",
                            "StackAllocator: MarkerStack capacity reached ({}); "
                            "further markers will not be tracked.",
                            kMaxMarkers);
                    }
                    return;
                }

                markers[size++] = marker;
            }

            void pop_back() noexcept
            {
                if (size > 0)
                {
                    --size;
                }
            }

            void clear() noexcept
            {
                size = 0;
                overflowed = false;
            }
        };

#ifndef NDEBUG
        MarkerStack m_markerStack;
#endif

        bool ValidateLifoOrder(const StackMarker& marker) const noexcept {
#ifdef NDEBUG
            (void)marker;
            return true;
#else
            if (!marker.IsValid()) {
                DNG_LOG_ERROR("Memory", "StackAllocator: Invalid marker provided to Pop().");
                return false;
            }
            if (m_markerStack.empty()) {
                DNG_LOG_ERROR("Memory", "StackAllocator: Pop() called on empty stack.");
                return false;
            }
            const StackMarker& top = m_markerStack.back();
            if (marker.m_stackIndex != top.m_stackIndex || marker.GetOffset() != top.GetOffset()) {
                DNG_LOG_ERROR("Memory",
                    "StackAllocator: LIFO violation. Expected idx={}, off={}, got idx={}, off={}.",
                    top.m_stackIndex, top.GetOffset(), marker.m_stackIndex, marker.GetOffset());
                return false;
            }
            return true;
#endif
        }

    public:
        // Construct with a parent allocator (for arena backing) and capacity (in bytes)
        StackAllocator(IAllocator* parentAllocator, usize capacity) noexcept
            : ArenaAllocator(parentAllocator, capacity)
        {}

        // Construct on a fixed external buffer
        StackAllocator(void* buffer, usize size) noexcept
            : ArenaAllocator(buffer, size)
        {}

        ~StackAllocator() noexcept override {
#ifndef NDEBUG
            if (!m_markerStack.empty()) {
                DNG_LOG_WARNING("Memory",
                    "StackAllocator: Destructor called with {} unpopped markers.",
                    m_markerStack.get_size());
            }
#endif
        }

        StackAllocator(const StackAllocator&) = delete;
        StackAllocator& operator=(const StackAllocator&) = delete;
        StackAllocator(StackAllocator&&) = delete;
        StackAllocator& operator=(StackAllocator&&) = delete;

        /**
         * @brief Pushes a new region of `size` bytes with `alignment`, returning a marker.
         *        Returns an invalid marker on failure.
         */
        [[nodiscard]] StackMarker Push(usize size, usize alignment) noexcept
        {
            if (size == 0) return StackMarker();

            // Capture pre-allocation position
            ArenaMarker pre = GetMarker();
            if (!pre.IsValid()) return StackMarker();

            // Delegate allocation to Arena (OOM handled inside Arena if patched)
            void* ptr = Allocate(size, alignment);
            if (!ptr) return StackMarker();

#ifndef NDEBUG
            const usize stackIndex = m_markerStack.get_size();
            StackMarker marker(pre, stackIndex);
            m_markerStack.push_back(marker);
            return marker;
#else
            return StackMarker(pre, 0);
#endif
        }

        [[nodiscard]] StackMarker Push(usize size) noexcept {
            return Push(size, alignof(std::max_align_t));
        }

        /**
         * @brief Allocates and returns both the pointer and the marker.
         *
         * @return nullptr on failure, and outMarker is invalid.
         */
        [[nodiscard]] void* PushAndGetPointer(usize size,
            usize alignment,
            StackMarker& outMarker) noexcept
        {
            if (size == 0) { outMarker = StackMarker(); return nullptr; }

            ArenaMarker pre = GetMarker();
            if (!pre.IsValid()) { outMarker = StackMarker(); return nullptr; }

            void* ptr = Allocate(size, alignment);  // OOM handled by Arena if patched
            if (!ptr) { outMarker = StackMarker(); return nullptr; }

#ifndef NDEBUG
            const usize stackIndex = m_markerStack.get_size();
            outMarker = StackMarker(pre, stackIndex);
            m_markerStack.push_back(outMarker);
#else
            outMarker = StackMarker(pre, 0);
#endif
            return ptr;
        }

        [[nodiscard]] void* PushAndGetPointer(usize size, StackMarker& outMarker) noexcept {
            return PushAndGetPointer(size, alignof(std::max_align_t), outMarker);
        }

        /**
         * @brief Pops the last pushed region (strict LIFO). Rewinds the arena to the marker.
         */
        void Pop(const StackMarker& marker) noexcept {
            if (!ValidateLifoOrder(marker)) {
#ifndef NDEBUG
                DNG_ASSERT(false && "StackAllocator: LIFO violation in Pop(marker).");
#endif
                return;
            }

            if (marker.IsValid()) {
                Rewind(marker.GetArenaMarker()); // back to pre-allocation position
            }

#ifndef NDEBUG
            if (!m_markerStack.empty()) m_markerStack.pop_back();
#endif
        }

        /**
         * @brief Returns current depth (Debug). In Release returns 0.
         */
        [[nodiscard]] usize GetStackDepth() const noexcept {
#ifndef NDEBUG
            return m_markerStack.get_size();
#else
            return 0;
#endif
        }

        /**
         * @brief Clears the stack (rewinds to base). In Debug, resets depth to 0.
         */
        void Reset() noexcept {
#ifndef NDEBUG
            m_markerStack.clear();
#endif
            ArenaAllocator::Reset();
        }

        /**
         * @brief No-op by design. Use Pop(marker) or Reset().
         */
        void Deallocate(void* ptr, usize /*size*/, usize /*alignment*/) noexcept override
        {
#if defined(DNG_STRICT_STACK_DEALLOC_ASSERTS) && DNG_STRICT_STACK_DEALLOC_ASSERTS
            // Hard-fail in Debug if someone tries to call Deallocate().
            DNG_LOG_FATAL("Memory", "StackAllocator: Deallocate() is not supported. Use Pop(marker) or Reset().");
            DNG_ASSERT(false && "StackAllocator: Deallocate() is not supported. Use Pop(marker) or Reset().");
#else
            // Marker-only free policy: Deallocate() intentionally does nothing and documents the correct usage path.
            (void)ptr;
#ifndef NDEBUG
            DNG_LOG_WARNING("Memory", "StackAllocator::Deallocate() is a no-op. Use Pop(marker) or Reset().");
#endif
#endif
        }

        /**
         * @brief Not supported for strict stack discipline. Always returns nullptr.
         */
        void* Reallocate(void* /*ptr*/,
            usize /*oldSize*/,
            usize /*newSize*/,
            usize /*alignment*/,
            bool* wasInPlace) noexcept override
        {
            if (wasInPlace) *wasInPlace = false;
            DNG_LOG_WARNING("Memory", "StackAllocator::Reallocate() is not supported. Allocate a new block and adjust scope.");
            return nullptr;
        }
    };

} // namespace dng::core
