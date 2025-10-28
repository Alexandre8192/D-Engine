#pragma once
// ============================================================================
// D-Engine - Core/Memory/AllocatorAdapter.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a standard-library compatible allocator that forwards all
//           storage requests to the engine's AllocatorRef abstraction so STL
//           containers can participate in the memory tracking/guarding layer.
// Contract: Header-only, no hidden globals, and safe to include from any TU.
//           The adapter requires MemorySystem::Init() (or an explicit
//           AllocatorRef) before the first allocation. All allocations honour
//           NormalizeAlignment and propagate the exact (size, alignment) pair
//           back to the originating allocator on deallocate.
// Notes   : This adapter intentionally stores AllocatorRef by value, keeping
//           the allocator choice deterministic for each container instance.
//           Adapted containers remain thread-safe exactly to the extent the
//           underlying allocator is thread-safe. Metadata allocations use the
//           engine's own allocators; no STL heap usage occurs here.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <type_traits>

#include "Core/Diagnostics/Check.hpp"
#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/MemorySystem.hpp"
#include "Core/Memory/OOM.hpp"

namespace dng
{
namespace core
{
    class AllocatorRef;

    // -------------------------------------------------------------------------
    // AllocatorAdapter
    // -------------------------------------------------------------------------
    // Purpose : STL allocator bridge that routes allocations through
    //           dng::core::AllocatorRef, defaulting to the engine default
    //           allocator when no explicit reference is supplied.
    // Contract: Follows the C++23 Allocator named requirements. Allocation
    //           failures honour the engine OOM policy and terminate when the
    //           policy is fatal. deallocate() is noexcept. Propagation traits
    //           favour move/swap.
    // Notes   : Containers copy-constructed from adapters inherit the same
    //           allocator reference. ResolveAllocator() performs lazy binding
    //           to avoid touching MemorySystem when the user passes an explicit
    //           AllocatorRef.
    // -------------------------------------------------------------------------
    template <class T>
    class AllocatorAdapter
    {
    public:
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_swap = std::true_type;
        using propagate_on_container_copy_assignment = std::false_type;
        using is_always_equal = std::false_type;

        template <class U> friend class AllocatorAdapter;

        // ---
        // Purpose : Default-construct without an allocator binding.
        // Contract: Caller must ensure MemorySystem::Init() before first
        //           allocation so ResolveAllocator() can bind lazily.
        // Notes   : Keeping the ctor trivial preserves aggregate friendliness.
        // ---
        constexpr AllocatorAdapter() noexcept = default;

        // ---
        // Purpose : Bind adapter to a specific AllocatorRef supplied by caller.
        // Contract: The referenced allocator must outlive the adapter.
        // Notes   : Useful for containers backed by arenas or scoped allocators.
        // ---
    explicit constexpr AllocatorAdapter(AllocatorRef ref) noexcept
            : mAllocator(ref)
        {
        }

        // ---
        // Purpose : Allow rebinding from AllocatorAdapter<U> as per STL rules.
        // Contract: Relies on the source adapter exposing GetAllocatorRef().
        // Notes   : Copying the raw AllocatorRef keeps ownership semantics.
        // ---
        template <class U>
        constexpr AllocatorAdapter(const AllocatorAdapter<U>& other) noexcept
            : mAllocator(other.GetAllocatorRef())
        {
        }

        // ---
        // Purpose : Allocate storage for `count` objects of type T.
        // Contract: Throws std::bad_alloc on failure unless FatalOnOOM is
        //           enabled (in which case the process terminates). A count of 0
        //           still returns a non-null sentinel pointer per the standard.
        // Notes   : Overflow is guarded explicitly to retain deterministic
        //           behaviour even for adversarial counts.
        // ---
        [[nodiscard]] value_type* allocate(size_type count)
        {
            if (count == 0)
            {
                return ZeroSizeSentinel();
            }

            constexpr size_type kElementSize = static_cast<size_type>(sizeof(value_type));
            const std::size_t alignment = NormalizeAlignment(static_cast<std::size_t>(alignof(value_type)));
            if (count > (std::numeric_limits<size_type>::max)() / kElementSize)
            {
                HandleAllocationFailure((std::numeric_limits<std::size_t>::max)(),
                    alignment,
                    "AllocatorAdapter::allocate overflow");
            }

            const size_type totalBytes = count * kElementSize;

            AllocatorRef alloc = ResolveAllocator();
            if (!alloc.IsValid())
            {
                DNG_CHECK(false && "AllocatorAdapter requires MemorySystem::Init() before use");
                HandleAllocationFailure(totalBytes, alignment, "AllocatorAdapter::allocate (unbound)");
            }

            void* memory = alloc.AllocateBytes(static_cast<std::size_t>(totalBytes), static_cast<std::size_t>(alignment));
            if (!memory)
            {
                HandleAllocationFailure(totalBytes, alignment, "AllocatorAdapter::allocate");
            }

            return static_cast<value_type*>(memory);
        }

        // ---
        // Purpose : Return storage obtained via allocate().
        // Contract: `ptr` may be null (no-op). The `count` must be identical to
        //           the value passed to allocate().
        // Notes   : The adapter tolerates late binding, but we assert in debug
        //           builds if the allocator is still null to aid debugging.
        // ---
        void deallocate(value_type* ptr, size_type count) noexcept
        {
            if (!ptr)
            {
                return;
            }

            if (count == 0)
            {
                return;
            }

            const std::size_t alignment = NormalizeAlignment(static_cast<std::size_t>(alignof(value_type)));
            const std::size_t totalBytes = count * static_cast<size_type>(sizeof(value_type));

            AllocatorRef alloc = ResolveAllocator();
            DNG_CHECK(alloc.IsValid() && "AllocatorAdapter::deallocate called without a bound allocator");
            if (!alloc.IsValid())
            {
                return;
            }

            alloc.DeallocateBytes(static_cast<void*>(ptr), static_cast<std::size_t>(totalBytes), static_cast<std::size_t>(alignment));
        }

        // ---
        // Purpose : Preserve allocator selection on container copy construction.
        // Contract: allocator_traits uses this hook when select_on_container_copy_construction
        //           is true. Returning *this keeps per-instance bindings consistent.
        // Notes   : Copy-constructed containers remain attached to the same allocator.
        // ---
        [[nodiscard]] AllocatorAdapter select_on_container_copy_construction() const noexcept
        {
            return *this;
        }

        // ---
        // Purpose : Report the theoretical maximum number of elements this adaptor can manage.
        // Contract: Matches the allocator named requirements; value is conservative and safe.
        // Notes   : Useful for legacy code that queries allocator capacity prior to operations.
        // ---
        [[nodiscard]] constexpr size_type max_size() const noexcept
        {
            const size_type elementSize = static_cast<size_type>(sizeof(value_type));
            return elementSize == 0 ? size_type(0) : (std::numeric_limits<size_type>::max)() / elementSize;
        }

        // ---
        // Purpose : Expose underlying allocator reference for diagnostics.
        // Contract: Returned AllocatorRef is a copy; modifying it does not
        //           mutate the adapter.
        // Notes   : Containers internally rely on this for propagate traits.
        // ---
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
        // ---
        // Purpose : Return a stable, properly aligned sentinel used when
        //           allocate(0) is requested so containers receive a non-null
        //           pointer while the underlying allocator remains untouched.
        // Contract: Sentinel is never passed to DeallocateBytes because
        //           deallocate(..., 0) short-circuits; storage is process-wide.
        // Notes   : Using unsigned char keeps the storage trivial and avoids
        //           depending on std::byte availability in older toolchains.
        // ---
        [[nodiscard]] static value_type* ZeroSizeSentinel() noexcept
        {
            alignas(value_type) static unsigned char s_storage[sizeof(value_type) == 0 ? 1 : sizeof(value_type)]{};
            return reinterpret_cast<value_type*>(s_storage);
        }

        // ---
        // Purpose : Lazily bind to MemorySystem::GetDefaultAllocator() if no
        //           explicit allocator was provided.
        // Contract: Returns an invalid AllocatorRef if MemorySystem is not
        //           initialized yet, allowing callers to diagnose the misuse.
        // Notes   : Mutable to permit binding even for const adapters.
        // ---
        [[nodiscard]] AllocatorRef ResolveAllocator() const noexcept
        {
            if (!mAllocator.IsValid() && ::dng::memory::MemorySystem::IsInitialized())
            {
                mAllocator = ::dng::memory::MemorySystem::GetDefaultAllocator();
            }
            return mAllocator;
        }

        // ---
        // Purpose : Trigger OOM policy diagnostics and terminate execution upon
        //           allocation failure.
        // Contract: Never returns; noexcept; static function. Must be called with
        //           the failed allocation size, alignment, and context string.
        //           Terminates the process unconditionally after logging.
        // Notes   : Uses std::terminate() rather than throw to maintain noexcept
        //           guarantee and avoid exception overhead. The DNG_MEM_CHECK_OOM
        //           macro logs failure details before termination, enabling
        //           post-mortem debugging. This matches D-Engine's zero-exception
        //           policy in Core modules.
        // ---
        [[noreturn]] static void HandleAllocationFailure(std::size_t size,
            std::size_t alignment,
            const char* context) noexcept
        {
            DNG_MEM_CHECK_OOM(size, alignment, context);
            std::terminate();
        }

        mutable AllocatorRef mAllocator{};
    };

    // ---
    // Purpose : Equality compares whether two adapters reference the same
    //           underlying allocator instance.
    // Contract: Works across different value types thanks to the friend.
    // Notes   : Inequality is derived automatically by the standard library.
    // ---
    template <class T, class U>
    [[nodiscard]] constexpr bool operator==(const AllocatorAdapter<T>& lhs, const AllocatorAdapter<U>& rhs) noexcept
    {
        return lhs.GetAllocatorRef().Get() == rhs.GetAllocatorRef().Get();
    }

} // namespace core
} // namespace dng
