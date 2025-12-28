#pragma once
// ============================================================================
// D-Engine - Core/Memory/Allocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Declare the allocator contract (`IAllocator`) and a lightweight
//           non-owning facade (`AllocatorRef`) that normalizes alignment and
//           forwards typed construction helpers.
// Contract: All allocate/reallocate/deallocate operations must use the exact
//           `(size, alignment)` pair that was used when the block was acquired.
//           Alignment parameters are always normalized via `NormalizeAlignment`.
// Notes   : The default `Reallocate` implementation (see Allocator.cpp) follows
//           the contract by allocate-copy-free; concrete allocators may provide
//           faster paths but must document any deviation explicitly.
// ============================================================================

#include <cstddef>      // std::size_t
#include <new>          // placement new
#include <type_traits>  // std::is_trivially_destructible_v, etc.
#include <utility>      // std::forward
#include <memory>       // std::construct_at, std::destroy_at (C++20)
#include <limits>       // std::numeric_limits

#include "Core/Diagnostics/Check.hpp"
#include "Core/Memory/OOM.hpp"
#include "Core/Types.hpp"            // usize, etc.
#include "Core/Memory/Alignment.hpp" // NormalizeAlignment(...)

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

    // ---
    // Purpose : Define the engine-wide allocator contract for all memory backends.
    // Contract: Callers must pair Allocate/Reallocate results with matching (size, alignment) on free.
    // Notes   : Implementations must honour NormalizeAlignment and remain noexcept.
    // ---
    class IAllocator
    {
    public:
        // ---
        // Purpose : Provide a virtual hook for proper cleanup in derived allocators.
        // Contract: Must remain noexcept; implementations release allocator-owned resources only.
        // Notes   : Defaulted because interface owns no state.
        // ---
        virtual ~IAllocator() = default;

        // ---
        // Purpose : Acquire a raw byte buffer honouring the requested alignment.
        // Contract: `size` > 0; implementations normalize alignment; caller owns block and must free with same tuple.
        // Notes   : Returns nullptr on failure; OOM policy handled by the caller.
        // ---
        [[nodiscard]] virtual void* Allocate(usize size, usize alignment) noexcept = 0;

        // ---
        // Purpose : Release a block previously obtained from this allocator.
        // Contract: `ptr` may be null; `(size, alignment)` must match the original normalized request.
        // Notes   : Mismatched tuples yield undefined behaviour; implementations typically assert in debug.
        // ---
        virtual void  Deallocate(void* ptr, usize size, usize alignment) noexcept = 0;

        // ---
        // Purpose : Resize or re-align an existing allocation through the allocator.
        // Contract: Mirrors Allocate when `ptr==nullptr`; acts as Deallocate when `newSize==0`; caller supplies original `(oldSize, alignment)` otherwise.
        // Notes   : Default implementation performs allocate-copy-free; `wasInPlace` toggles true only if address unchanged.
        // ---
        [[nodiscard]] virtual void* Reallocate(void* ptr,
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
    // ---
    // Purpose : Lightweight non-owning facade for invoking allocator operations safely.
    // Contract: Holds a raw IAllocator pointer; all helpers normalize alignment and respect size/alignment tuples.
    // Notes   : Cheap to copy; intended for hot-path call sites needing typed helpers without ownership.
    // ---
    class AllocatorRef
    {
    public:
        // ---
        // Purpose : Construct an invalid view that performs no allocations.
        // Contract: Resulting reference must be rebound before use; all helpers return nullptr when invalid.
        // Notes   : constexpr to allow static initialization.
        // ---
        constexpr AllocatorRef() noexcept : m_Alloc(nullptr) {}
        // ---
        // Purpose : Bind the wrapper to an existing allocator instance.
        // Contract: `alloc` must outlive the reference; no ownership transfer occurs.
        // Notes   : constexpr enabling compile-time wiring of global allocators.
        // ---
        explicit constexpr AllocatorRef(IAllocator* alloc) noexcept : m_Alloc(alloc) {}

        // ---
        // Purpose : Check whether the wrapper currently targets a valid allocator.
        // Contract: No side effects; safe in constexpr.
        // Notes   : Useful for branchless call sites that tolerate null allocators.
        // ---
        [[nodiscard]] constexpr bool        IsValid() const noexcept { return m_Alloc != nullptr; }
        // ---
        // Purpose : Expose the underlying allocator pointer for advanced usage.
        // Contract: Returned pointer may be null; caller must not assume ownership.
        // Notes   : Enables interoperability with legacy call paths needing raw interfaces.
        // ---
        [[nodiscard]] constexpr IAllocator* Get()     const noexcept { return m_Alloc; }

        // ---- Raw byte APIs ---------------------------------------------------

        // ---
        // Purpose : Allocate an untyped byte range through the wrapped allocator.
        // Contract: `size` > 0; alignment normalized; returns nullptr when wrapper invalid or allocation fails (OOM policy triggered separately).
        // Notes   : Calls DNG_MEM_CHECK_OOM to honour global fatal/non-fatal configuration.
        // ---
        [[nodiscard]] void* AllocateBytes(usize size,
            usize alignment = alignof(std::max_align_t)) noexcept
        {
            if (!m_Alloc || size == 0)
                return nullptr;

            alignment = NormalizeAlignment(alignment);
            void* memory = m_Alloc->Allocate(size, alignment);
            if (!memory)
            {
                DNG_MEM_CHECK_OOM(size, alignment, "AllocatorRef::AllocateBytes");
            }
            return memory;
        }

        // ---
        // Purpose : Deallocate a byte range previously acquired via this wrapper.
        // Contract: `(size, alignment)` must match original normalized request; null pointers are ignored.
        // Notes   : Wrapper validity checked; no OOM handling required.
        // ---
        void DeallocateBytes(void* ptr,
            usize size,
            usize alignment = alignof(std::max_align_t)) noexcept
        {
            if (!m_Alloc || !ptr)
                return;

            alignment = NormalizeAlignment(alignment);
            m_Alloc->Deallocate(ptr, size, alignment);
        }

        // ---
        // Purpose : Resize or re-align an existing allocation originating from this wrapper.
        // Contract: Mirrors `IAllocator::Reallocate`; triggers OOM policy when `newSize>0` and allocation fails.
        // Notes   : Pass-through for `wasInPlace` flag.
        // ---
        [[nodiscard]] void* ReallocateBytes(void* ptr,
            usize oldSize,
            usize newSize,
            usize alignment = alignof(std::max_align_t),
            bool* wasInPlace = nullptr) noexcept
        {
            if (!m_Alloc)
                return nullptr;

            alignment = NormalizeAlignment(alignment);
            void* memory = m_Alloc->Reallocate(ptr, oldSize, newSize, alignment, wasInPlace);
            if (!memory && newSize > 0)
            {
                DNG_MEM_CHECK_OOM(newSize, alignment, "AllocatorRef::ReallocateBytes");
            }
            return memory;
        }

        // ---- Typed helpers ---------------------------------------------------

        // ---
        // Purpose : Allocate and construct a single object using the wrapped allocator.
        // Contract: Propagates constructor noexceptness; returns nullptr if wrapper invalid or allocation fails.
        // Notes   : OOM path escalated through DNG_MEM_CHECK_OOM before returning nullptr.
        // ---
        template <typename T, typename... Args>
        [[nodiscard]] T* New(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            void* mem = AllocateBytes(sizeof(T), alignof(T));
            if (!mem)
            {
                DNG_MEM_CHECK_OOM(sizeof(T), alignof(T), "AllocatorRef::New");
                return nullptr;
            }

            // C++20: unconditionally use std::construct_at
            return std::construct_at(reinterpret_cast<T*>(mem), std::forward<Args>(args)...);
        }

        // ---
        // Purpose : Destroy a single object and release its storage via the wrapped allocator.
        // Contract: Accepts null pointers; caller guarantees pointer came from `New` with same type and allocator.
        // Notes   : Performs destruction before handing bytes back to allocator.
        // ---
        template <typename T>
        void Delete(T* obj) noexcept
        {
            if (!obj)
                return;

            std::destroy_at(obj);
            DeallocateBytes(static_cast<void*>(obj), sizeof(T), alignof(T));
        }

        // ---
        // Purpose : Allocate and default-construct an array of objects.
        // Contract: `count` > 0; overflow in total byte count guarded; returns nullptr on failure after OOM policy invocation.
        // Notes   : Skips manual construction for trivially default-constructible types.
        // ---
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
            {
                DNG_MEM_CHECK_OOM(total, alignof(T), "AllocatorRef::NewArray");
                return nullptr;
            }

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

        // ---
        // Purpose : Destroy `count` elements and release the contiguous storage.
        // Contract: Pointer must originate from `NewArray` with matching `count` and allocator; accepts null.
        // Notes   : Skips destruction for trivially destructible types for performance.
        // ---
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
