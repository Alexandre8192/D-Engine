#pragma once
// ============================================================================
// D-Engine - Memory Core
// File: Core/Memory/Allocator.hpp
// ----------------------------------------------------------------------------
// This header defines:
//   - IAllocator: the engine's minimal allocator interface
//   - AllocatorRef: a thin, non-owning wrapper around IAllocator*
// 
// DESIGN NOTES
// ------------
// * We keep the contract very explicit: callers may pass any `alignment`
//   (including 0 = "default"), and implementations MUST normalize alignment.
// * `Deallocate` / `Reallocate` require the SAME (size, alignment) that was
//   used for the original allocation, unless a concrete allocator documents
//   a different policy.
// * The default Reallocate implementation (see Allocator.cpp) will allocate/
//   copy/free to honor a requested alignment change; it does not attempt to
//   re-align in place.
// * This header does not depend on the engine's OOM policy; that is handled
//   in the .cpp by DNG_MEM_CHECK_OOM(...).
// ============================================================================

#include <cstddef>      // std::size_t
#include <new>          // placement new
#include <type_traits>  // std::is_trivially_destructible_v, etc.
#include <utility>      // std::forward
#include <memory>       // std::construct_at, std::destroy_at (C++20)
#include <limits>       // std::numeric_limits

#include "Core/Types.hpp"                // usize, etc.
#include "Core/Memory/Alignment.hpp"     // NormalizeAlignment(...)

// Optional debug assert fallback if the engine assert is not available here.
#ifndef DNG_ASSERT
#include <cassert>
#define DNG_ASSERT(x) assert(x)
#endif

namespace dng::core
{
    // ------------------------------------------------------------------------
    // Memory Contracts (explicit, engine-wide)
    // ------------------------------------------------------------------------
    //
    // Alignment normalization:
    // - All allocation APIs accept an arbitrary `alignment`.
    // - NormalizeAlignment(alignment) guarantees:
    //     * result is a power-of-two
    //     * result >= alignof(std::max_align_t)
    //     * result >= 1
    // - Callers MAY pass 0 to mean "default". Implementations MUST normalize.
    // - The AllocatorRef wrapper also normalizes before delegating.
    //
    // Size/alignment contract:
    // - `Deallocate(ptr, size, alignment)` and
    //   `Reallocate(ptr, oldSize, newSize, alignment)` REQUIRE the exact same
    //   `(size, alignment)` as were used when `ptr` was originally allocated,
    //   unless a concrete allocator explicitly documents a different policy.
    // - Violating this is undefined behavior; debug builds should assert.
    //
    // Behavior notes:
    // - Passing `newSize == 0` to `Reallocate` is equivalent to `Deallocate`
    //   and returns `nullptr`.
    // - Passing `ptr == nullptr` to `Reallocate` is equivalent to `Allocate`.
    // - The default `Reallocate` implementation may MOVE the block even if
    //   `oldSize == newSize` (e.g., to honor a different requested alignment).
    //   Custom allocators that can re-align or grow/shrink in place are free
    //   to override and provide stronger guarantees.
    // ------------------------------------------------------------------------

    class IAllocator
    {
    public:
        virtual ~IAllocator() = default;

        // Allocate a block of `size` bytes with at least `alignment` alignment.
        // Implementations must normalize alignment internally.
        //
        // Returns: pointer to a usable memory block, or nullptr on failure.
        // Contract: On success, the returned block is owned by the caller and
        //           must be released via Deallocate/Reallocate with the SAME
        //           `(size, alignment)` (see contract above).
        virtual void* Allocate(usize size, usize alignment) noexcept = 0;

        // Free a block previously returned by Allocate/Reallocate.
        // `size` and `alignment` MUST match the original allocation of `ptr`
        // (after normalization). Debug builds should assert on mismatch.
        //
        // Notes:
        // - Passing `nullptr` is a no-op (implementation may choose to assert).
        // - Passing a non-matching `(size, alignment)` is undefined behavior.
        virtual void  Deallocate(void* ptr, usize size, usize alignment) noexcept = 0;

        // Reallocate an existing block to `newSize` with the requested `alignment`.
        //
        // Contracts:
        // - If `ptr == nullptr`, behaves like Allocate(newSize, alignment).
        // - If `newSize == 0`, behaves like Deallocate(ptr, oldSize, alignment)
        //   and returns nullptr.
        // - If `ptr != nullptr`, `oldSize > 0` MUST be the original size, and
        //   `alignment` MUST match the original one (after normalization),
        //   unless the concrete allocator documents a different rule.
        //
        // Default behavior (see .cpp):
        // - The default implementation may MOVE the block (allocate/copy/free),
        //   including when `oldSize == newSize`, to honor a different requested
        //   alignment. It does NOT attempt "in-place re-alignment."
        // - `wasInPlace` will be set to `true` ONLY if the returned pointer
        //   equals `ptr` (which the default impl does not perform unless the
        //   underlying Allocate returns the same address, which is atypical).
        //
        // Overrides:
        // - A custom allocator is free to implement true in-place reallocation
        //   or re-alignment, as long as it preserves the public contract.
        virtual void* Reallocate(void* ptr,
            usize oldSize,
            usize newSize,
            usize alignment,
            bool* wasInPlace = nullptr) noexcept;
    };

    // ------------------------------------------------------------------------
    // AllocatorRef - thin non-owning wrapper around IAllocator*
    // ------------------------------------------------------------------------
    //
    // Purpose:
    // - Make call sites simpler and safer:
    //     * Coerce alignment via NormalizeAlignment before delegating.
    //     * Provide typed helpers (New/Delete, NewArray/DeleteArray) that
    //       respect the allocator's size/alignment contract.
    // - No ownership semantics: this is a lightweight view on an IAllocator.
    //
    // Notes:
    // - If `IsValid()` is false, Allocate/Deallocate helpers are no-ops (or
    //   return nullptr). This keeps call sites compact without branching.
    // - Typed helpers use std::construct_at / std::destroy_at (C++20).
    // ------------------------------------------------------------------------
    class AllocatorRef
    {
    public:
        constexpr AllocatorRef() noexcept : m_Alloc(nullptr) {}
        explicit constexpr AllocatorRef(IAllocator* alloc) noexcept : m_Alloc(alloc) {}

        [[nodiscard]] constexpr bool        IsValid() const noexcept { return m_Alloc != nullptr; }
        [[nodiscard]] constexpr IAllocator* Get()     const noexcept { return m_Alloc; }

        // ---- Raw byte APIs ---------------------------------------------------

        // Allocate `size` bytes with the requested `alignment` (0 means "default").
        // Returns nullptr if the wrapper is invalid or if allocation fails.
        [[nodiscard]] void* AllocateBytes(usize size,
            usize alignment = alignof(std::max_align_t)) noexcept
        {
            if (!m_Alloc || size == 0)
                return nullptr;

            alignment = NormalizeAlignment(alignment);
            return m_Alloc->Allocate(size, alignment);
        }

        // Free a block previously obtained through this allocator with the SAME
        // `(size, alignment)` as the original allocation (after normalization).
        // No-op if wrapper invalid or `ptr == nullptr`.
        void DeallocateBytes(void* ptr,
            usize size,
            usize alignment = alignof(std::max_align_t)) noexcept
        {
            if (!m_Alloc || !ptr)
                return;

            alignment = NormalizeAlignment(alignment);
            m_Alloc->Deallocate(ptr, size, alignment);
        }

        // Reallocate a block to `newSize` with the requested `alignment`.
        // See IAllocator::Reallocate for the full contract.
        [[nodiscard]] void* ReallocateBytes(void* ptr,
            usize oldSize,
            usize newSize,
            usize alignment = alignof(std::max_align_t),
            bool* wasInPlace = nullptr) noexcept
        {
            if (!m_Alloc)
                return nullptr;

            alignment = NormalizeAlignment(alignment);
            return m_Alloc->Reallocate(ptr, oldSize, newSize, alignment, wasInPlace);
        }

        // ---- Typed helpers ---------------------------------------------------

        // Construct a single T with forwarded ctor args.
        // Returns nullptr on allocation failure or invalid wrapper.
        template <typename T, typename... Args>
        [[nodiscard]] T* New(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            void* mem = AllocateBytes(sizeof(T), alignof(T));
            if (!mem)
                return nullptr;

            // C++20: unconditionally use std::construct_at
            return std::construct_at(reinterpret_cast<T*>(mem), std::forward<Args>(args)...);
        }

        // Destroy a single T and deallocate using the original (size, alignment).
        template <typename T>
        void Delete(T* obj) noexcept
        {
            if (!obj)
                return;

            std::destroy_at(obj);
            DeallocateBytes(static_cast<void*>(obj), sizeof(T), alignof(T));
        }

        // Construct an array of `count` Ts.
        // - Guards against overflow in `sizeof(T) * count`.
        // - Trivially default-constructible types do not require per-element construction.
        template <typename T>
        [[nodiscard]] T* NewArray(usize count) noexcept
        {
            if (!m_Alloc || count == 0)
                return nullptr;

            // Overflow guard: total = sizeof(T) * count must fit in `usize`.
            constexpr usize kElemSize = static_cast<usize>(sizeof(T));
            if (kElemSize > 0)
            {
                const usize maxCount = std::numeric_limits<usize>::max() / kElemSize;
                if (count > maxCount)
                {
                    DNG_ASSERT(false && "AllocatorRef::NewArray: size overflow (sizeof(T) * count)");
                    return nullptr;
                }
            }
            // If kElemSize == 0 (theoretically impossible for object types), total stays 0.

            const usize total = kElemSize * count;
            void* mem = AllocateBytes(total, alignof(T));
            if (!mem)
                return nullptr;

            T* first = reinterpret_cast<T*>(mem);

            if constexpr (std::is_trivially_default_constructible_v<T>)
            {
                // POD-ish types: no element-wise construction needed.
                return first;
            }
            else
            {
                // Basic guarantee: construct elements one by one.
                T* it = first;
                for (usize i = 0; i < count; ++i, ++it)
                {
                    std::construct_at(it);
                }
                return first;
            }
        }

        // Destroy an array of `count` Ts and free the entire block.
        // Assumes the array was allocated by NewArray<T>(count) with this wrapper.
        template <typename T>
        void DeleteArray(T* ptr, usize count) noexcept
        {
            if (!ptr || !m_Alloc)
                return;

            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                T* it = ptr;
                for (usize i = 0; i < count; ++i, ++it)
                {
                    std::destroy_at(it);
                }
            }

            constexpr usize kElemSize = static_cast<usize>(sizeof(T));
            // If the array came from NewArray with the same (T, count), this is safe.
            // If the caller passes the wrong `count`, behavior is undefined by contract.
            DeallocateBytes(static_cast<void*>(ptr), kElemSize * count, alignof(T));
        }

    private:
        IAllocator* m_Alloc;
    };

} // namespace dng::core
