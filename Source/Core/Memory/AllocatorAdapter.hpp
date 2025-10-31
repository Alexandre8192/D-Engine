#pragma once
// ============================================================================
// D-Engine - Core/Memory/AllocatorAdapter.hpp
// ----------------------------------------------------------------------------
// Purpose : Publish a deterministic STL allocator/view over AllocatorRef so
//           standard containers honour the engine's tracking, alignment, and
//           OOM policies without introducing hidden costs.
// Contract: Header-only, self-contained, no RTTI or exceptions. All allocation
//           paths normalise alignment via NormalizeAlignment, verify tuples
//           with compile-time guards, and fail through DNG_MEM_CHECK_OOM +
//           std::terminate. Requires MemorySystem::Init() or an explicit
//           AllocatorRef prior to first allocation.
// Notes   : The adapter stays POD so it can be copied by value, keeps allocator
//           selection per-container, and only touches std::pmr internally when
//           platform containers demand polymorphic resources.
// ============================================================================

#include <cstddef>
#include <exception>
#include <limits>
#include <type_traits>

#include "Core/Diagnostics/Check.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/MemorySystem.hpp"
#include "Core/Memory/OOM.hpp"

namespace dng
{
namespace core
{
    namespace detail
    {
        template <class T>
        struct AllocatorAdapterStaticChecks
        {
            static constexpr std::size_t kNativeAlignment = static_cast<std::size_t>(alignof(T));
            static constexpr std::size_t kValueSize = static_cast<std::size_t>(sizeof(T));
            static constexpr std::size_t kAlignedSize = AlignUp(kValueSize, kNativeAlignment);

            static_assert(kValueSize > 0, "AllocatorAdapter requires a complete value_type");
            static_assert(IsPowerOfTwo(kNativeAlignment), "value_type alignment must be power-of-two");
            static_assert(kAlignedSize >= kValueSize, "AlignUp must never shrink the allocation footprint");
        };
    } // namespace detail

    // -------------------------------------------------------------------------
    // AllocatorAdapter
    // -------------------------------------------------------------------------
    // Purpose : STL-compatible allocator that forwards to AllocatorRef without
    //           adding behavioural surprises or secondary allocations.
    // Contract: Meets C++23 Allocator named requirements. All observable state
    //           is trivial; allocation failure terminates via engine OOM policy.
    // Notes   : Lazy binds to MemorySystem default allocator only when needed,
    //           preserving deterministic ownership of explicit AllocatorRefs.
    // -------------------------------------------------------------------------
    template <class T>
    class AllocatorAdapter final
    {
        using StaticChecks = detail::AllocatorAdapterStaticChecks<T>;

    public:
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_swap = std::true_type;
        using propagate_on_container_copy_assignment = std::false_type;
        using is_always_equal = std::false_type;

        template <class U> friend class AllocatorAdapter;

        // Purpose : Default to an unbound adapter; binds lazily on first use.
        // Contract: Caller guarantees MemorySystem::Init() before any hot-path
        //           allocation; otherwise allocate() terminates deterministically.
        // Notes   : constexpr default keeps the adapter usable in globals.
        constexpr AllocatorAdapter() noexcept = default;

        // Purpose : Bind the adapter to a specific allocator supplied by caller.
        // Contract: Referenced allocator must outlive this adapter; pointer is
        //           copied by value with no ownership transfer.
        // Notes   : Explicit to avoid implicit conversions from unrelated types.
        explicit constexpr AllocatorAdapter(AllocatorRef ref) noexcept
            : mAllocator(ref)
        {
        }

        // Purpose : Rebind across value_types as mandated by allocator_traits.
        // Contract: Copies the underlying AllocatorRef so containers share the
        //           exact same allocation context.
        // Notes   : constexpr to keep rebinding viable in compile-time contexts.
        template <class U>
        constexpr AllocatorAdapter(const AllocatorAdapter<U>& other) noexcept
            : mAllocator(other.GetAllocatorRef())
        {
        }

        // Purpose : Acquire storage for `count` objects of value_type.
        // Contract: `count` may be zero (returns sentinel); overflow checked via
        //           max_size(); failure routes through HandleAllocationFailure.
        // Notes   : No throws; AlignUp already validated by static checks.
        [[nodiscard]] value_type* allocate(size_type count)
        {
            if (count == 0)
            {
                return ZeroSizeSentinel();
            }

            constexpr size_type kElementSize = StaticChecks::kValueSize;
            const size_type maxCount = max_size();
            if (count > maxCount)
            {
                const std::size_t normalized = NormalizeAlignment(StaticChecks::kNativeAlignment);
                HandleAllocationFailure(static_cast<std::size_t>(count) * kElementSize,
                    normalized,
                    "AllocatorAdapter::allocate overflow");
            }

            const std::size_t totalBytes = static_cast<std::size_t>(count) * kElementSize;
            const std::size_t alignment = NormalizeAlignment(StaticChecks::kNativeAlignment);

            AllocatorRef alloc = ResolveAllocator();
            if (!alloc.IsValid())
            {
                DNG_CHECK(false && "AllocatorAdapter requires MemorySystem::Init() before use");
                HandleAllocationFailure(totalBytes, alignment, "AllocatorAdapter::allocate (unbound)");
            }

            void* memory = alloc.AllocateBytes(totalBytes, alignment);
            if (!memory)
            {
                HandleAllocationFailure(totalBytes, alignment, "AllocatorAdapter::allocate");
            }

            return static_cast<value_type*>(memory);
        }

        // Purpose : Release storage acquired via allocate().
        // Contract: `ptr` may be null (no-op); `count` must match allocation.
        // Notes   : Defensive DNG_CHECK retains diagnostics when allocator stays unbound.
        void deallocate(value_type* ptr, size_type count) noexcept
        {
            if (!ptr || count == 0)
            {
                return;
            }

            const std::size_t alignment = NormalizeAlignment(StaticChecks::kNativeAlignment);
            const std::size_t totalBytes = static_cast<std::size_t>(count) * StaticChecks::kValueSize;

            AllocatorRef alloc = ResolveAllocator();
            DNG_CHECK(alloc.IsValid() && "AllocatorAdapter::deallocate called without a bound allocator");
            if (!alloc.IsValid())
            {
                return;
            }

            alloc.DeallocateBytes(static_cast<void*>(ptr), totalBytes, alignment);
        }

        // Purpose : Preserve allocator instance on copy-construction of containers.
        // Contract: allocator_traits calls this hook per standard requirements.
        // Notes   : constexpr copy keeps the adapter trivially copyable.
        [[nodiscard]] constexpr AllocatorAdapter select_on_container_copy_construction() const noexcept
        {
            return *this;
        }

        // Purpose : Report conservative upper bound on element count.
        // Contract: Uses `sizeof(value_type)`; returns 0 when sizeof==0 (defensive only).
        // Notes   : constexpr so allocator_traits can reason at compile time.
        [[nodiscard]] constexpr size_type max_size() const noexcept
        {
            const size_type elementSize = StaticChecks::kValueSize;
            return elementSize == 0 ? size_type{ 0 } : (std::numeric_limits<size_type>::max)() / elementSize;
        }

        // Purpose : Make underlying AllocatorRef observable for diagnostics/utilities.
        // Contract: Returns by value; modifications of the copy never mutate the adapter.
        // Notes   : [[nodiscard]] to surface accidental ignore sites during audits.
        [[nodiscard]] constexpr AllocatorRef GetAllocatorRef() const noexcept
        {
            return mAllocator;
        }

        template <class U>
        struct rebind
        {
            using other = AllocatorAdapter<U>;
        };

    private:
        // Purpose : Deterministic sentinel for zero-sized allocations.
        // Contract: Never passed to DeallocateBytes; alignment honours value_type.
        // Notes   : Function-static ensures one per TU without extra headers.
        [[nodiscard]] static value_type* ZeroSizeSentinel() noexcept
        {
            alignas(value_type) static unsigned char storage[StaticChecks::kValueSize == 0 ? 1 : StaticChecks::kValueSize]{};
            return reinterpret_cast<value_type*>(storage);
        }

        // Purpose : Bind to default allocator lazily when caller did not provide one.
        // Contract: Returns invalid AllocatorRef until MemorySystem::Init().
        // Notes   : Mutable to allow late binding inside const methods.
        [[nodiscard]] AllocatorRef ResolveAllocator() const noexcept
        {
            if (!mAllocator.IsValid() && ::dng::memory::MemorySystem::IsInitialized())
            {
                mAllocator = ::dng::memory::MemorySystem::GetDefaultAllocator();
            }
            return mAllocator;
        }

        // Purpose : Uniform failure path honouring engine-wide OOM diagnostics.
        // Contract: Never returns; always terminates the process after logging.
        // Notes   : Keeps adapter noexcept and avoids exception machinery.
        [[noreturn]] static void HandleAllocationFailure(std::size_t size,
            std::size_t alignment,
            const char* context) noexcept
        {
            DNG_MEM_CHECK_OOM(size, alignment, context);
            std::terminate();
        }

        mutable AllocatorRef mAllocator{};
    };

    // Purpose : Compare adapters by the allocator instance they reference.
    // Contract: Cross-value_type compare valid; relies on GetAllocatorRef().
    // Notes   : Inequality is provided automatically by the standard library.
    template <class T, class U>
    [[nodiscard]] constexpr bool operator==(const AllocatorAdapter<T>& lhs, const AllocatorAdapter<U>& rhs) noexcept
    {
        return lhs.GetAllocatorRef().Get() == rhs.GetAllocatorRef().Get();
    }

} // namespace core
} // namespace dng
