#pragma once

// Use engine-absolute includes to avoid path fragility
#include "Core/Types.hpp"
#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/OOM.hpp"
#include "Core/Platform/PlatformDefines.hpp"
#include "Core/Platform/PlatformMacros.hpp"

// std
#include <new>        // std::nothrow
#include <cstdint>    // std::uintptr_t
#include <cstdlib>    // std::free (if ever needed), but we only use operator delete
#include <limits>

namespace dng::core {
    /**
     * Default system allocator (platform-agnostic).
     *
     * Strategy:
     *  - Over-allocate (header + payload + alignment slack).
     *  - Compute an aligned user pointer past the header.
     *  - Store the original raw pointer (from ::operator new) in the header.
     *  - Deallocate by reading the header and deleting the original pointer.
     *
     * Pros:
     *  - Single code path, portable, supports any power-of-two alignment.
     *  - No reliance on platform-specific aligned allocation APIs.
     *
     * Overhead:
     *  - Header is sized & aligned to alignof(std::max_align_t).
     *  - With DNG_MEM_PARANOID_META=0 (default): rawPtr + magic (minimal).
     *  - With DNG_MEM_PARANOID_META=1: also stores size+align for runtime checks.
     */
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

        // (Kept for potential internal use)
        static DNG_FORCEINLINE usize Normalize(usize alignment) noexcept {
            // 0 or non-power-of-two -> normalize to default
            if (alignment == 0) return DEFAULT_ALIGNMENT;
            return IsPowerOfTwo(alignment) ? alignment : DEFAULT_ALIGNMENT;
        }

        static DNG_FORCEINLINE void* AlignForward(void* p, usize alignment) noexcept {
            const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(p);
            const std::uintptr_t aligned = (raw + (alignment - 1)) & ~(alignment - 1);
            return reinterpret_cast<void*>(aligned);
        }

        static DNG_FORCEINLINE bool IsHeaderValid(const AllocationHeader* h) noexcept {
            return h && h->magic == HEADER_MAGIC && h->rawPtr != nullptr;
        }

    public:
        DefaultAllocator() = default;
        ~DefaultAllocator() override = default;

        [[nodiscard]] void* Allocate(usize size,
            usize alignment = alignof(std::max_align_t)) noexcept override
        {
            if (size == 0) return nullptr;

            // Normalize alignment first (contract: callers may pass arbitrary values, including 0).
            alignment = NormalizeAlignment(alignment);
            DNG_ASSERT(IsPowerOfTwo(alignment) && "NormalizeAlignment must produce power-of-two");

            // Global guard: treat extremely large alignments as OOM (policy decision).
            if (alignment > static_cast<usize>(DNG_MAX_REASONABLE_ALIGNMENT)) {
                DNG_MEM_CHECK_OOM(size, alignment, "DefaultAllocator::Allocate");
                return nullptr;
            }

            // Total size: header + payload + slack for alignment
            constexpr usize kHeaderSize = sizeof(AllocationHeader);
            const usize extra = alignment - 1;

            // Overflow protection for kHeaderSize + size + extra
            const usize maxv = (std::numeric_limits<usize>::max)();
            if (size > maxv - kHeaderSize - extra) {
                // Handle as OOM for consistency
                DNG_MEM_CHECK_OOM(size, alignment, "DefaultAllocator::Allocate");
                return nullptr;
            }
            const usize totalSize = kHeaderSize + size + extra;

            // Raw allocation (we handle alignment ourselves using the header)
            void* raw = ::operator new(totalSize, std::nothrow);
            if (!raw) {
                // Centralized OOM policy (fatal may not return)
                DNG_MEM_CHECK_OOM(size, alignment, "DefaultAllocator::Allocate");
                return nullptr;
            }

            // Address immediately after the header
            void* afterHeader = static_cast<u8*>(raw) + kHeaderSize;

            // Compute aligned user pointer upwards from afterHeader
            const usize alignedAddr = AlignUp<usize>(
                static_cast<usize>(reinterpret_cast<std::uintptr_t>(afterHeader)),
                alignment
            );
            void* userPtr = reinterpret_cast<void*>(alignedAddr);

            // Place the header immediately before userPtr
            auto* header = reinterpret_cast<AllocationHeader*>(
                static_cast<u8*>(userPtr) - kHeaderSize
                );
            header->rawPtr = raw;
            header->magic = HEADER_MAGIC;
#if DNG_MEM_PARANOID_META
            header->size = size;
            header->align = alignment;
#endif

            DNG_CHECK(IsAligned(userPtr, alignment) && "Returned pointer is not properly aligned");
            return userPtr;
        }

        void Deallocate(void* ptr,
            usize size = 0,
            usize alignment = DEFAULT_ALIGNMENT) noexcept override
        {
            if (!ptr) return;

            // Normalize alignment to honor the public contract (0 allowed at call sites)
            alignment = NormalizeAlignment(alignment);
            DNG_ASSERT(IsPowerOfTwo(alignment));

            const usize headerSize = sizeof(AllocationHeader);
            auto* header = reinterpret_cast<AllocationHeader*>(
                static_cast<u8*>(ptr) - headerSize
                );

            // Debug guard: avoid UB if a foreign/corrupted pointer is passed
            DNG_CHECK(IsHeaderValid(header) && "Pointer not owned by DefaultAllocator or corrupted");

#if DNG_MEM_PARANOID_META
            // In paranoia mode, validate the (size, alignment) contract at runtime.
            if (IsHeaderValid(header)) {
                DNG_ASSERT((size == 0 || size == header->size) &&
                    "Deallocate size mismatch (must equal original allocation size)");
                DNG_ASSERT(alignment == header->align &&
                    "Deallocate alignment mismatch (must equal original allocation alignment)");
            }
#endif

            if (IsHeaderValid(header)) {
                ::operator delete(header->rawPtr);
            }
        }

        [[nodiscard]] void* Reallocate(void* ptr, usize oldSize, usize newSize,
            usize alignment = DEFAULT_ALIGNMENT,
            bool* wasInPlace = nullptr) noexcept override
        {
            // Delegate to the generic fallback (allocate/copy/free)
            if (wasInPlace) *wasInPlace = false;
            return IAllocator::Reallocate(ptr, oldSize, newSize, alignment, wasInPlace);
        }

    private:
        void HandleOutOfMemory(const char* /*message*/) noexcept {
#if DNG_MEM_FATAL_ON_OOM
            DNG_CHECK(false && "Out of memory - fatal error enabled");
#endif
            // Otherwise: return nullptr at call sites
        }
    };
} // namespace dng::core
