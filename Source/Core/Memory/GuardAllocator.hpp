#pragma once
// ============================================================================
// D-Engine - GuardAllocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Debug-only allocator that surrounds every allocation with software
//           redzones and operating-system guard pages. By reserving a dedicated
//           virtual-memory island per allocation we can detect buffer overruns,
//           underruns, and use-after-free scenarios deterministically. Relies
//           exclusively on the canonical alignment helpers for padding/offset
//           computations.
// Contract: Header-only, dependency-free (beyond CoreMinimal.hpp). All public
//           functions honour the IAllocator contract: callers must provide the
//           same (size, alignment) pair on Deallocate. Construction requires a
//           non-null parent allocator that handles bookkeeping when guards are
//           compiled out. Thread safety is delegated to the parent allocator.
//           All alignment values are normalized and power-of-two checked before
//           use.
// Notes   : The allocator is typically enabled only when DNG_MEM_GUARDS != 0.
//           When guards are disabled, GuardAllocator degrades to a thin pass-
//           through wrapper over the parent allocator so production builds pay
//           no extra cost. No local alignment helpers are used, eliminating
//           drift from the engine's canonical semantics.
// ============================================================================

#include "Core/CoreMinimal.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/PageAllocator.hpp"
#include "Core/Memory/Alignment.hpp" // AlignUp / NormalizeAlignment / IsPowerOfTwo
#include "Core/Memory/Allocator.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace dng
{
namespace memory
{
    namespace detail
    {
        using usize = std::size_t;

        // ---
        // Purpose : Pack the metadata required to tear down a guarded allocation.
        //           Stored at the beginning of the committed payload region so we
        //           can reconstruct the reserved range during Deallocate().
        // Contract: Lifetime is managed manually via placement new. Magic field
        //           must remain intact; corruption implies memory stomping.
        // Notes   : frontPadding encodes the byte distance from commitBase to the
        //           first user byte so we can rebuild redzone extents precisely.
        // ---
        struct GuardHeader final
        {
            static constexpr std::uint64_t kMagic = 0x444E474755415244ull; // "DNGGUARD"

            std::uint64_t magic{ kMagic };
            void*         reservedBase{ nullptr };
            usize         reservedSize{ 0 };
            usize         commitSize{ 0 };
            usize         requestedSize{ 0 };
            usize         alignment{ 0 };
            usize         frontPadding{ 0 };
            const char*   tag{ nullptr };
        };

        static_assert(alignof(GuardHeader) <= alignof(std::max_align_t),
            "GuardHeader must not require alignment stricter than max_align_t");

        [[nodiscard]] inline bool IsHeaderValid(const GuardHeader* header) noexcept
        {
            return (header != nullptr) && (header->magic == GuardHeader::kMagic);
        }

        // ---
        // Purpose : Convenience wrapper around PageAllocator::PageSize() so we
        //           can keep page-size queries localized within the detail layer.
        // Contract: Thread-safe, returns the OS native page size (>= 4 KiB).
        // Notes   : Cached by PageAllocator; repeated calls are inexpensive.
        // ---
        [[nodiscard]] inline usize GuardPageSize() noexcept
        {
            return PageSize();
        }

        static constexpr usize        kRedzoneBytes    = 32;
        static constexpr std::uint8_t kRedzonePattern  = 0xCD;
        static constexpr usize        kHeaderAlignment = alignof(std::max_align_t);

        static constexpr usize kHeaderStorage = ::dng::core::AlignUp<usize>(sizeof(GuardHeader), kHeaderAlignment);

        static_assert(kHeaderStorage >= sizeof(GuardHeader),
            "Header storage must at least fit GuardHeader");

        // ---
        // Purpose : Write the canonical debug pattern into a buffer so byte-wise
        //           validation can detect over/underwrites.
        // Contract: Safe to call with nullptr/zero bytes (no work performed).
        // Notes   : Pattern fill is only emitted in debug builds to avoid the
        //           production cost of memset on every allocation.
        // ---
        inline void FillPattern(void* ptr, usize bytes) noexcept
        {
#if !defined(NDEBUG)
            if (ptr && bytes > 0)
            {
                std::memset(ptr, static_cast<int>(kRedzonePattern), bytes);
            }
#else
            (void)ptr;
            (void)bytes;
#endif
        }

        // ---
        // Purpose : Verify that a redzone still contains the expected pattern.
        // Contract: Returns true when the region is intact. Caller decides how
        //           to react to corruption (typically logs + assertion).
        // Notes   : Validation is compiled out of release builds.
        // ---
        [[nodiscard]] inline bool CheckPattern(const std::uint8_t* ptr, usize bytes) noexcept
        {
#if !defined(NDEBUG)
            if (!ptr || bytes == 0)
            {
                return true;
            }

            for (usize i = 0; i < bytes; ++i)
            {
                if (ptr[i] != kRedzonePattern)
                {
                    return false;
                }
            }
#endif
            (void)ptr;
            (void)bytes;
            return true;
        }
    } // namespace detail

    // ------------------------------------------------------------------------
    // GuardAllocator
    // ------------------------------------------------------------------------
    // Purpose : Augment an existing allocator with redzones and guard pages to
    //           trap memory corruption bugs deterministically while routing all
    //           padding math through the canonical alignment utilities.
    // Contract: Parent allocator must outlive this wrapper. All allocations must
    //           be freed with the same size/alignment pair. Thread safety relies
    //           entirely on the parent allocator (this wrapper adds no locking),
    //           and every caller-visible alignment is normalized/power-of-two
    //           checked before use.
    // Notes   : When DNG_MEM_GUARDS == 0, Allocate/Deallocate simply delegate to
    //           the parent to keep production builds lean. No local alignment
    //           replicas exist so semantics stay in lockstep with Core/Alignment.hpp.
    // ------------------------------------------------------------------------
    class GuardAllocator final : public ::dng::core::IAllocator
    {
    public:
        // ---
        // Purpose : Store the parent allocator used for backing storage.
        // Contract: parent must not be nullptr and must remain valid for the
        //           entire lifetime of GuardAllocator.
        // Notes   : We assert in debug builds to catch accidental null wiring.
        // ---
    explicit GuardAllocator(::dng::core::IAllocator* parent) noexcept
            : parent_(parent)
        {
            DNG_CHECK(parent_ != nullptr && "GuardAllocator requires a parent allocator");
        }

        ~GuardAllocator() noexcept override = default;

        // ---
        // Purpose : Allocate a guarded block (or delegate when guards disabled).
        // Contract: Size must be > 0. Alignment is normalized to engine policy.
        // Notes   : Forwards to the tagged overload so future callsites can
        //           provide human-readable labels without breaking ABI.
        // ---
    [[nodiscard]] void* Allocate(::dng::core::usize size, ::dng::core::usize alignment) noexcept override
        {
            return AllocateInternal(size, alignment, "GuardAllocator");
        }

        // ---
        // Purpose : Same as the base Allocate but allows the caller to specify a
        //           diagnostic tag. Stored in the header for leak/corruption logs.
        // Contract: Tag pointer is not copied; caller must ensure its lifetime is
        //           static (string literal recommended).
        // Notes   : Not marked override because IAllocator currently exposes the
        //           two-parameter signature. We keep the overload to ease future
        //           migration toward tagged allocations.
        // ---
    [[nodiscard]] void* Allocate(::dng::core::usize size, ::dng::core::usize alignment, const char* tag) noexcept
        {
            return AllocateInternal(size, alignment, tag);
        }

        // ---
        // Purpose : Release a guarded allocation and return the virtual memory to
        //           the operating system.
        // Contract: ptr must denote a block previously returned by Allocate(),
        //           using the exact same size/alignment. nullptr is ignored.
        // Notes   : Redzones are validated (debug builds) and the entire commit
        //           range is flipped to PAGE_NOACCESS before release to catch
        //           lingering use-after-free bugs immediately.
        // ---
    void Deallocate(void* ptr, ::dng::core::usize size, ::dng::core::usize alignment) noexcept override
        {
            if (!ptr)
            {
                return;
            }

            alignment = NormalizeAlignmentChecked(alignment, "GuardAllocator::Deallocate");

#if !DNG_MEM_GUARDS
            if (parent_)
            {
                parent_->Deallocate(ptr, size, alignment);
            }
            else
            {
                DNG_CHECK(false && "GuardAllocator::Deallocate has no parent to delegate to");
            }
            return;
#else
            using detail::GuardHeader;
            using detail::IsHeaderValid;

            const detail::usize pageSize = detail::GuardPageSize();
            const detail::usize minFrontPadding = detail::kHeaderStorage + detail::kRedzoneBytes;

            auto* userBytes = static_cast<std::uint8_t*>(ptr);
            auto* searchBase = userBytes - minFrontPadding;
            auto* commitBasePtr = static_cast<std::uint8_t*>(::dng::core::AlignDown(static_cast<void*>(searchBase), pageSize));
            auto* header = reinterpret_cast<GuardHeader*>(commitBasePtr);

            if (!IsHeaderValid(header))
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator::Deallocate detected a corrupted header (ptr={}, size={})",
                    ptr,
                    static_cast<unsigned long long>(size));
                return;
            }

            if (header->requestedSize != size)
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator::Deallocate size mismatch (expected {}, got {})",
                    static_cast<unsigned long long>(header->requestedSize),
                    static_cast<unsigned long long>(size));
            }
            if (header->alignment != alignment)
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator::Deallocate alignment mismatch (expected {}, got {})",
                    static_cast<unsigned long long>(header->alignment),
                    static_cast<unsigned long long>(alignment));
            }

            const detail::usize commitSize = header->commitSize;
            auto* frontRedzoneBegin = commitBasePtr + detail::kHeaderStorage;
            const detail::usize frontRedzoneBytes = (header->frontPadding > detail::kHeaderStorage)
                ? (header->frontPadding - detail::kHeaderStorage)
                : 0;
            auto* userPtr = commitBasePtr + header->frontPadding;
            auto* backRedzoneBegin = userPtr + header->requestedSize;
            auto* commitEnd = commitBasePtr + commitSize;
            void* reservedBase = header->reservedBase;
            const detail::usize reservedSize = header->reservedSize;

            DNG_ASSERT(frontRedzoneBytes >= detail::kRedzoneBytes);
            DNG_ASSERT(commitEnd >= backRedzoneBegin + detail::kRedzoneBytes);

#if !defined(NDEBUG)
            if (!detail::CheckPattern(frontRedzoneBegin, frontRedzoneBytes))
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator detected FRONT redzone corruption (tag={})",
                    header->tag ? header->tag : "<unset>");
            }
            if (!detail::CheckPattern(backRedzoneBegin, detail::kRedzoneBytes))
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator detected BACK redzone corruption (tag={})",
                    header->tag ? header->tag : "<unset>");
            }
            detail::FillPattern(userPtr, header->requestedSize);
#endif

            // Flip the committed pages to PAGE_NOACCESS prior to release so any
            // stale pointers fault immediately, then return the reservation.
            header->magic = 0; // poison header before guarding pages; touches occur while page RW.

            for (detail::usize offset = 0; offset < commitSize; offset += pageSize)
            {
                GuardPage(commitBasePtr + offset);
            }

            Release(reservedBase, reservedSize);
#endif
        }

        // ---
        // Purpose : Human-readable identifier useful for diagnostics.
        // Contract: String literal pointer; safe for static storage duration only.
        // Notes   : Mirrors other allocators in the system.
        // ---
        [[nodiscard]] const char* GetName() const noexcept
        {
            return "GuardAllocator";
        }

    private:
        // ---
        // Purpose : Shared implementation for both Allocate overloads.
        // Contract: Same as public Allocate. Returns nullptr on failure while
        //           emitting diagnostics via DNG_LOG_ERROR.
        // Notes   : When guards are compiled out we delegate immediately to the
        //           parent allocator to avoid the overhead of virtual memory.
        // ---
    [[nodiscard]] void* AllocateInternal(::dng::core::usize size, ::dng::core::usize alignment, const char* tag) noexcept
        {
            if (!parent_)
            {
                DNG_CHECK(false && "GuardAllocator::AllocateInternal missing parent allocator");
                return nullptr;
            }

            if (size == 0)
            {
                DNG_CHECK(false && "GuardAllocator::Allocate requires size > 0");
                return nullptr;
            }

            alignment = NormalizeAlignmentChecked(alignment, tag);
            if (alignment > static_cast<std::size_t>(DNG_MAX_REASONABLE_ALIGNMENT))
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator::Allocate alignment {} exceeds DNG_MAX_REASONABLE_ALIGNMENT ({}).",
                    static_cast<unsigned long long>(alignment),
                    static_cast<unsigned long long>(DNG_MAX_REASONABLE_ALIGNMENT));
                return nullptr;
            }

#if !DNG_MEM_GUARDS
            return parent_->Allocate(size, alignment);
#else
            const detail::usize pageSize = detail::GuardPageSize();
            const detail::usize minFrontPadding = detail::kHeaderStorage + detail::kRedzoneBytes;
            const detail::usize alignmentSlack = alignment;
            const std::size_t maxValue = (std::numeric_limits<std::size_t>::max)();

            const std::size_t frontAndBack = minFrontPadding + detail::kRedzoneBytes;
            if (frontAndBack < minFrontPadding)
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator::Allocate overflow while computing overhead (tag={}).",
                    tag ? tag : "<unset>");
                return nullptr;
            }

            const std::size_t totalOverhead = frontAndBack + alignmentSlack;
            if (totalOverhead < frontAndBack)
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator::Allocate overflow while computing alignment slack (tag={}).",
                    tag ? tag : "<unset>");
                return nullptr;
            }

            if (size > maxValue - totalOverhead)
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator::Allocate request too large after overhead (size={}, tag={}).",
                    static_cast<unsigned long long>(size),
                    tag ? tag : "<unset>");
                return nullptr;
            }

            const detail::usize commitPayload = minFrontPadding + size + detail::kRedzoneBytes + alignmentSlack;
            const detail::usize commitSize = ::dng::core::AlignUp<detail::usize>(commitPayload, pageSize);
            const detail::usize totalReserve = commitSize + (pageSize * 2u);

            void* reserved = Reserve(totalReserve);
            if (!reserved)
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator::Allocate failed to reserve {} bytes (tag={}).",
                    static_cast<unsigned long long>(totalReserve),
                    tag ? tag : "<unset>");
                return nullptr;
            }

            auto* base = static_cast<std::uint8_t*>(reserved);
            auto* guardFront = base;
            auto* commitBase = base + pageSize;

            Commit(commitBase, commitSize);

            GuardPage(guardFront);
            GuardPage(commitBase + commitSize);

            auto* header = new (commitBase) detail::GuardHeader{};
            header->reservedBase = reserved;
            header->reservedSize = totalReserve;
            header->commitSize = commitSize;
            header->requestedSize = size;
            header->alignment = alignment;
            header->tag = tag;

            std::uint8_t* redzoneFrontBegin = commitBase + detail::kHeaderStorage;
            const std::uintptr_t userCandidate = reinterpret_cast<std::uintptr_t>(redzoneFrontBegin) + detail::kRedzoneBytes;
            const std::uintptr_t userAddr = ::dng::core::AlignUp<std::uintptr_t>(userCandidate, alignment);
            auto* userPtr = reinterpret_cast<std::uint8_t*>(userAddr);

            if (userPtr < redzoneFrontBegin)
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator internal alignment bug (tag={}).",
                    tag ? tag : "<unset>");
                header->magic = 0;
                Release(reserved, totalReserve);
                return nullptr;
            }

            header->frontPadding = static_cast<detail::usize>(userPtr - commitBase);

            auto* userEnd = userPtr + size;
            auto* backRedzoneBegin = userEnd;
            auto* commitEnd = commitBase + commitSize;
            if (backRedzoneBegin + detail::kRedzoneBytes > commitEnd)
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "GuardAllocator computed insufficient commit size (tag={}, commit={}, request={}).",
                    tag ? tag : "<unset>",
                    static_cast<unsigned long long>(commitSize),
                    static_cast<unsigned long long>(size));
                header->magic = 0;
                Release(reserved, totalReserve);
                return nullptr;
            }

#if !defined(NDEBUG)
            // Fill both redzones with the canonical pattern and poison the payload.
            detail::FillPattern(redzoneFrontBegin, static_cast<detail::usize>(userPtr - redzoneFrontBegin));
            detail::FillPattern(backRedzoneBegin, detail::kRedzoneBytes);
            detail::FillPattern(userPtr, size);
#else
            (void)backRedzoneBegin;
#endif

            return static_cast<void*>(userPtr);
#endif
        }

    ::dng::core::IAllocator* parent_;

        [[nodiscard]] static ::dng::core::usize NormalizeAlignmentChecked(::dng::core::usize alignment, const char* context) noexcept
        {
            if ((alignment != 0) && !::dng::core::IsPowerOfTwo(alignment))
            {
                DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY,
                    "{} received non power-of-two alignment {}",
                    context ? context : "GuardAllocator",
                    static_cast<unsigned long long>(alignment));
                DNG_CHECK(false);
            }
            return ::dng::core::NormalizeAlignment(alignment);
        }
    };

} // namespace memory
} // namespace dng
