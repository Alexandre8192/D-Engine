#pragma once
// ============================================================================
// D-Engine - Core/Memory/DefaultAllocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a portable fallback allocator that honours the engine's
//           alignment rules while delegating storage to ::operator new/delete.
// Contract: Stateless and thread-safe. Callers may pass any alignment value;
//           the allocator normalizes it via NormalizeAlignment and requires
//           the exact (size, alignment) pair on Deallocate(). When
//           DNG_MEM_PARANOID_META is enabled, runtime checks validate the
//           contract. Failing allocations dispatch through DNG_MEM_CHECK_OOM.
// Notes   : No platform-specific APIs are used, keeping the allocator fully
//           portable and making it ideal as the root for other allocators.
// ============================================================================

// Use engine-absolute includes to avoid path fragility
#include "Core/Types.hpp"
#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Diagnostics/Check.hpp"
#include "Core/Memory/OOM.hpp"

#include <cstdint>    // std::uintptr_t
#include <limits>
#include <new>        // std::nothrow

namespace dng { namespace core {

    // ------------------------------------------------------------------------
    // DefaultAllocator
    // ------------------------------------------------------------------------
    // Purpose : Translate the engine allocator contract onto ::operator new so
    //           every platform gains a consistent, alignment-aware fallback.
    // Contract: Thread-safe because it stores no mutable state. Allocate()
    //           normalizes alignment, records the original raw pointer in a
    //           small header, and Deallocate() must receive the same
    //           (size, alignment) tuple used at allocation time. Reallocate()
    //           falls back to IAllocator::Reallocate (allocate/copy/free).
    // Notes   : Header metadata carries a magic to detect corruption and can
    //           optionally store size/alignment when paranoia tracking is on.
    // ------------------------------------------------------------------------
    class DefaultAllocator final : public IAllocator {
    private:
        static constexpr u32 HEADER_MAGIC = 0xD15A110Cu; // "D-alloc" fun magic (hex)

        struct alignas(alignof(std::max_align_t)) AllocationHeader {
            void* rawPtr;  // original pointer returned by ::operator new
            u32   magic;   // debug guard
#if DNG_MEM_PARANOID_META
            usize size;    // original requested size (post normalization)
            usize align;   // original requested alignment (normalized)
#endif
        };

        static_assert(sizeof(AllocationHeader) % alignof(std::max_align_t) == 0,
            "Header must be sized as a multiple of max_align_t");

        static constexpr usize DEFAULT_ALIGNMENT = alignof(std::max_align_t);

        static inline bool IsHeaderValid(const AllocationHeader* h) noexcept {
            return h && h->magic == HEADER_MAGIC && h->rawPtr != nullptr;
        }

    public:
        DefaultAllocator() = default;
        ~DefaultAllocator() override = default;

        [[nodiscard]] void* Allocate(usize size,
            usize alignment = alignof(std::max_align_t)) noexcept override
        {
            if (size == 0) return nullptr;

            alignment = NormalizeAlignment(alignment);
            DNG_ASSERT(IsPowerOfTwo(alignment) && "NormalizeAlignment must produce power-of-two");

            if (alignment > static_cast<usize>(DNG_MAX_REASONABLE_ALIGNMENT)) {
                DNG_MEM_CHECK_OOM(size, alignment, "DefaultAllocator::Allocate");
                return nullptr;
            }

            constexpr usize kHeaderSize = sizeof(AllocationHeader);
            const usize extra = alignment - 1;
            const usize maxv = (std::numeric_limits<usize>::max)();
            if (size > maxv - kHeaderSize - extra) {
                DNG_MEM_CHECK_OOM(size, alignment, "DefaultAllocator::Allocate");
                return nullptr;
            }
            const usize totalSize = kHeaderSize + size + extra;

            void* raw = ::operator new(totalSize, std::nothrow);
            if (!raw) {
                DNG_MEM_CHECK_OOM(size, alignment, "DefaultAllocator::Allocate");
                return nullptr;
            }

            void* afterHeader = static_cast<u8*>(raw) + kHeaderSize;
            const usize alignedAddr = AlignUp<usize>(
                static_cast<usize>(reinterpret_cast<std::uintptr_t>(afterHeader)),
                alignment);
            void* userPtr = reinterpret_cast<void*>(alignedAddr);

            auto* header = reinterpret_cast<AllocationHeader*>(
                static_cast<u8*>(userPtr) - kHeaderSize);
            header->rawPtr = raw;
            header->magic = HEADER_MAGIC;
#if DNG_MEM_PARANOID_META
            header->size = size;
            header->align = alignment;
#endif

            DNG_CHECK(IsAligned<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(userPtr), alignment) &&
                "Returned pointer is not properly aligned");
            return userPtr;
        }

        void Deallocate(void* ptr,
            usize size = 0,
            usize alignment = DEFAULT_ALIGNMENT) noexcept override
        {
            if (!ptr) return;

            alignment = NormalizeAlignment(alignment);
            DNG_ASSERT(IsPowerOfTwo(alignment));

            const usize headerSize = sizeof(AllocationHeader);
            auto* header = reinterpret_cast<AllocationHeader*>(
                static_cast<u8*>(ptr) - headerSize);

            DNG_CHECK(IsHeaderValid(header) && "Pointer not owned by DefaultAllocator or corrupted");

#if DNG_MEM_PARANOID_META
            if (IsHeaderValid(header)) {
                DNG_ASSERT((size == 0 || size == header->size) &&
                    "Deallocate size mismatch (must equal original allocation size)");
                DNG_ASSERT(alignment == header->align &&
                    "Deallocate alignment mismatch (must equal original allocation alignment)");
            }
#else
            (void)size; // keep signature symmetric when paranoia disabled
#endif

            if (IsHeaderValid(header)) {
                ::operator delete(header->rawPtr);
            }
        }

        [[nodiscard]] void* Reallocate(void* ptr, usize oldSize, usize newSize,
            usize alignment = DEFAULT_ALIGNMENT,
            bool* wasInPlace = nullptr) noexcept override
        {
            if (wasInPlace) *wasInPlace = false;
            return IAllocator::Reallocate(ptr, oldSize, newSize, alignment, wasInPlace);
        }
    };
} // namespace core
} // namespace dng
