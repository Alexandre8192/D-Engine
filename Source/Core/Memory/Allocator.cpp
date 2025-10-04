// ============================================================================
// D-Engine -- Core Memory
// File: Core/Memory/Allocator.cpp
// ----------------------------------------------------------------------------
// PURPOSE
// -------
// Provide the **default** implementation of IAllocator::Reallocate(...) used by
// the engine's allocator interface. This fallback implementation is intentionally
// conservative and portable across all platforms/allocators.
//
// DESIGN OVERVIEW
// ---------------
// 1) No "in-place" mutation by default:
//    - This implementation never tries to grow/shrink or re-align the existing
//      block in place. It always follows the allocate/copy/free pattern when
//      ptr != nullptr and newSize > 0.
//    - Allocator specializations that support in-place reallocation should
//      override Reallocate(...) and implement their policy, keeping the public
//      contract intact.
//
// 2) Strict contract for (size, alignment):
//    - Deallocate/Reallocate must receive the SAME (size, alignment) that was
//      used to allocate `ptr` (after NormalizeAlignment).
//    - Passing oldSize == 0 with a non-null `ptr` is considered a misuse. We
//      assert in debug and return nullptr without touching the original block
//      in release to avoid undefined behavior.
//
// 3) OOM handling is centralized via DNG_MEM_CHECK_OOM:
//    - In fatal mode, DNG_MEM_CHECK_OOM(...) is expected not to return
//      (e.g., it may abort).
//    - In non-fatal mode, we return nullptr and the caller keeps ownership of
//      the original block.
//
// 4) Caller intent is preserved:
//    - If the caller requests a different alignment, we do not treat
//      (oldSize == newSize) as a no-op. The new block is allocated with the
//      requested alignment.
//
// THREAD SAFETY
// -------------
// - This function does not hold global state and relies on the concrete
//   allocator instance for any thread-safety guarantees.
// - Copying/truncating is performed with std::memcpy on POD payload. For
//   objects, callers are expected to manage construction semantics themselves.
//
// ============================================================================

#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/OOM.hpp"      // DNG_MEM_CHECK_OOM(...) + OOM policy

#include <cstring>                  // std::memcpy
#include <algorithm>                // std::min

namespace dng::core
{
    // ------------------------------------------------------------------------
    // Default IAllocator::Reallocate
    // ------------------------------------------------------------------------
    void* IAllocator::Reallocate(void* ptr,
        usize oldSize,
        usize newSize,
        usize alignment,
        bool* wasInPlace) noexcept
    {
        // Normalize alignment up-front. Guarantees:
        //  - alignment is a power of two
        //  - alignment >= alignof(std::max_align_t)
        //  - alignment >= 1
        alignment = NormalizeAlignment(alignment);

        // Conservatively assume this will MOVE (not in-place).
        if (wasInPlace) *wasInPlace = false;

        // Misuse guard: if `ptr` is non-null, `oldSize` must be the original size.
        if (ptr && oldSize == 0)
        {
            DNG_ASSERT(false && "Reallocate misuse: oldSize must be provided when ptr != nullptr");
            // Do not touch `ptr`. Keep the original block valid.
            return nullptr;
        }

        // If the new size is zero, this is a deallocation request.
        if (newSize == 0)
        {
            if (ptr)
            {
                // Deallocate using the exact original (size, alignment) contract.
                Deallocate(ptr, oldSize, alignment);
            }
            return nullptr;
        }

        // If `ptr` is null, this is equivalent to a fresh allocation.
        if (!ptr)
        {
            return Allocate(newSize, alignment);
        }

        // At this point: ptr != nullptr, newSize > 0, oldSize > 0
        // We DO NOT treat (oldSize == newSize) as a no-op, because the caller
        // may be requesting a different alignment. We honor that by allocating
        // a new block with the desired alignment and copying over the payload.

        // 1) Allocate a new block with the requested size and alignment.
        void* newPtr = Allocate(newSize, alignment);
        if (!newPtr)
        {
            // Centralized OOM policy:
            // - Fatal mode: DNG_MEM_CHECK_OOM(...) must not return.
            // - Non-fatal mode: return nullptr; original block remains valid.
            DNG_MEM_CHECK_OOM(newSize, alignment, "IAllocator::Reallocate");

#if !DNG_MEM_FATAL_ON_OOM
            return nullptr; // propagate failure
#else
            // Fatal mode should not return; defensive fallback for compilers.
            return nullptr;
#endif
        }

        // Defensive guard: if an exotic allocator returns the same address while
        // the old block is still owned, copying + Deallocate would double-free.
        if (newPtr == ptr)
        {
            if (wasInPlace) *wasInPlace = true;
            return ptr;
        }

        // 2) Copy the payload: copy min(oldSize, newSize). Truncation occurs if
        //    shrinking; growth leaves the tail uninitialized (callers construct).
        if (oldSize > 0)
        {
            const usize copySize = std::min(oldSize, newSize);
            if (copySize > 0)
            {
                std::memcpy(newPtr, ptr, copySize);
            }
        }

        // 3) Free the old block with the original (size, alignment).
        Deallocate(ptr, oldSize, alignment);

        // 4) Return the new block. wasInPlace remains false by default.
        return newPtr;
    }

} // namespace dng::core
