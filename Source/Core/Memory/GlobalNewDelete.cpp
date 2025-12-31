#include "Core/Memory/GlobalNewDelete.hpp"

// ---
// Purpose : Engine-wide global new/delete overloads wired to D-Engine OOM policy.
// Contract: Throwing operator new forms are fatal on OOM (std::terminate);
//           nothrow forms return nullptr. Delete variants honor (size,
//           alignment) invariants.
// Notes   : Core does not emit std::bad_alloc here; OOM diagnostics run before
//           termination. Nothrow paths remain non-throwing for callers that
//           probe for nullptr.
// ---

#if DNG_ROUTE_GLOBAL_NEW
static_assert(DNG_GLOBAL_NEW_SMALL_THRESHOLD >= 0, "DNG_GLOBAL_NEW_SMALL_THRESHOLD must be >= 0");
static_assert(DNG_GLOBAL_NEW_FALLBACK_MALLOC == 0 || DNG_GLOBAL_NEW_FALLBACK_MALLOC == 1,
              "DNG_GLOBAL_NEW_FALLBACK_MALLOC must be 0 or 1");

#include "Core/CoreMinimal.hpp"
#include "Core/Diagnostics/Check.hpp"
#include "Core/Memory/MemorySystem.hpp"
#include "Core/Memory/OOM.hpp"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <new>
#include <cstdint>

namespace
{
    using ::dng::core::AllocatorRef;
    using ::dng::core::IsPowerOfTwo;
    using ::dng::core::NormalizeAlignment;
    using ::dng::memory::MemorySystem;

    // ---------------------------------------------------------------------
    // Constants describing the small-object policy for global routing.
    // ---------------------------------------------------------------------
    constexpr std::size_t kSmallAlignmentCeiling = 16; // matches SmallObjectAllocator heuristic

    // ---------------------------------------------------------------------
    // Thread-local guard that lets us detect recursive entry.
    // ---------------------------------------------------------------------
    class ThreadReentryGuard
    {
    public:
        // ---
        // Purpose : Mark the calling thread as "inside" a global operator.
        // Contract: The referenced flag must remain valid for the guard's
        //           lifetime (we use TLS storage for that). The guard is not
        //           copyable.
        // Notes   : When recursion is detected (flag already set) we skip the
        //           engine routing and fall back to libc allocations to avoid
        //           infinite recursion (e.g., DefaultAllocator calling new).
        // ---
        explicit ThreadReentryGuard(bool& flag) noexcept
            : mFlag(flag)
            , mIsPrimary(!flag)
        {
            if (mIsPrimary)
            {
                flag = true;
            }
        }

        ThreadReentryGuard(const ThreadReentryGuard&) = delete;
        ThreadReentryGuard& operator=(const ThreadReentryGuard&) = delete;

        ~ThreadReentryGuard()
        {
            if (mIsPrimary)
            {
                mFlag = false;
            }
        }

        [[nodiscard]] bool IsPrimary() const noexcept { return mIsPrimary; }

    private:
        bool& mFlag;
        bool  mIsPrimary;
    };

    thread_local bool gNewReentryFlag = false;
    thread_local bool gDeleteReentryFlag = false;

    // ---------------------------------------------------------------------
    // AllocationRecord
    // ---------------------------------------------------------------------
    // Purpose : Track metadata for each global allocation so we can call the
    //           matching allocator (or fallback free) during delete.
    // Contract: Stored in a simple singly-linked list guarded by a mutex. The
    //           registry favours debuggability over raw performance.
    // Notes   : We allocate records via std::malloc to avoid recursively using
    //           the very operators we are overriding.
    // ---------------------------------------------------------------------
    struct AllocationRecord
    {
        void*        pointer{ nullptr };
        void*        fallbackStorage{ nullptr };
        AllocatorRef allocator{};
        std::size_t  size{ 0 };
        std::size_t  alignment{ 0 };
        bool         usedFallback{ false };
        AllocationRecord* next{ nullptr };
    };

    std::mutex gRegistryMutex;
    AllocationRecord* gRegistryHead = nullptr;
    std::atomic<bool> gFallbackWarned{ false };

    // --- Utility helpers ---------------------------------------------------

    // ---
    // Purpose : Emit a single warning the first time we route allocations
    //           through the fallback path (typically before MemorySystem::Init).
    // Contract: Thread-safe; may be called concurrently from multiple threads.
    // Notes   : Logging is kept lightweight to avoid cascading recursion.
    // ---
    void EmitFallbackWarningOnce() noexcept
    {
        bool expected = false;
        if (gFallbackWarned.compare_exchange_strong(expected, true))
        {
            DNG_LOG_WARNING("Memory", "Global new/delete temporarily routed through std::malloc/std::free until MemorySystem::Init() completes.");
        }
    }

    // ---
    // Purpose : Register a freshly allocated pointer with metadata required for
    //           correct deallocation.
    // Contract: Returns true on success. On failure the caller must undo the
    //           allocation (to prevent leaks).
    // Notes   : The registry is intentionally simple; future work can replace
    //           it with a lock-free hash table if necessary.
    // ---
    [[nodiscard]] bool RegisterAllocation(void* ptr,
        const AllocatorRef& allocator,
        std::size_t size,
        std::size_t alignment,
        bool usedFallback,
        void* fallbackStorage) noexcept
    {
        auto* node = static_cast<AllocationRecord*>(std::malloc(sizeof(AllocationRecord)));
        if (!node)
        {
            return false;
        }

        node->pointer = ptr;
        node->fallbackStorage = fallbackStorage;
        node->allocator = allocator;
        node->size = size;
        node->alignment = alignment;
        node->usedFallback = usedFallback;

        std::lock_guard<std::mutex> lock(gRegistryMutex);
        node->next = gRegistryHead;
        gRegistryHead = node;
        return true;
    }

    // ---
    // Purpose : Remove the metadata entry for `ptr` from the registry.
    // Contract: Returns nullptr when the pointer was not tracked (double free or
    //           allocation performed before routing was active).
    // Notes   : Removal is O(n) but acceptable for the current development
    //           focus on correctness and diagnostics.
    // ---
    [[nodiscard]] AllocationRecord* UnregisterAllocation(void* ptr) noexcept
    {
        std::lock_guard<std::mutex> lock(gRegistryMutex);

        AllocationRecord* previous = nullptr;
        AllocationRecord* current = gRegistryHead;
        while (current)
        {
            if (current->pointer == ptr)
            {
                if (previous)
                {
                    previous->next = current->next;
                }
                else
                {
                    gRegistryHead = current->next;
                }
                return current;
            }

            previous = current;
            current = current->next;
        }

        return nullptr;
    }

    // ---
    // Purpose : Release the bookkeeping node created by RegisterAllocation.
    // Contract: Accepts null (no-op).
    // Notes   : Uses std::free to avoid recursion.
    // ---
    void DestroyAllocationRecord(AllocationRecord* record) noexcept
    {
        if (record)
        {
            std::free(record);
        }
    }

    // ---
    // Purpose : Allocate memory via std::malloc while meeting explicit
    //           alignment requirements.
    // Contract: Returns nullptr on failure and writes the raw storage pointer
    //           (needed for deallocation) to storageOut. Caller must pass the
    //           normalized alignment.
    // Notes   : We over-allocate and store the base pointer right before the
    //           aligned address (classic aligned_malloc technique).
    // ---
    [[nodiscard]] void* AllocateFallback(std::size_t size,
        std::size_t alignment,
        void*& storageOut) noexcept
    {
        const std::size_t normalizedAlignment = NormalizeAlignment(alignment);
        const std::size_t extra = normalizedAlignment + sizeof(void*);
        const std::size_t total = size + extra;

        void* base = std::malloc(total);
        if (!base)
        {
            storageOut = nullptr;
            return nullptr;
        }

        auto* bytes = static_cast<unsigned char*>(base);
        const std::uintptr_t baseAddr = reinterpret_cast<std::uintptr_t>(bytes + sizeof(void*));
        const std::uintptr_t alignedAddr = ::dng::core::AlignUp<std::uintptr_t>(baseAddr, normalizedAlignment);
        auto* alignedPtr = reinterpret_cast<unsigned char*>(alignedAddr);
        auto** metadataSlot = reinterpret_cast<void**>(alignedPtr) - 1;
        *metadataSlot = base;

        storageOut = base;
        return static_cast<void*>(alignedPtr);
    }

    // ---
    // Purpose : Free memory obtained via AllocateFallback.
    // Contract: `storage` is the raw base pointer captured during allocation.
    // Notes   : We do not attempt to read metadata from the user pointer; the
    //           registry already stores the base pointer explicitly.
    // ---
    void FreeFallback(void* /*ptr*/, void* storage) noexcept
    {
        std::free(storage);
    }

    // ---
    // Purpose : Decide whether a request should use the small-object allocator.
    // Contract: Performs purely arithmetic checks; no side effects.
    // Notes   : Alignment larger than the slab's natural capabilities falls back
    //           to the default allocator to avoid SmallObjectAllocator delegating
    //           behind our back.
    // ---
    [[nodiscard]] bool ShouldUseSmall(std::size_t size, std::size_t alignment) noexcept
    {
        if (DNG_GLOBAL_NEW_SMALL_THRESHOLD <= 0)
        {
            return false;
        }

        if (size > static_cast<std::size_t>(DNG_GLOBAL_NEW_SMALL_THRESHOLD))
        {
            return false;
        }

        if (alignment > kSmallAlignmentCeiling)
        {
            return false;
        }

        return true;
    }

    // ---
    // Purpose : Centralize out-of-memory (OOM) handling for global operator new
    //           overloads.
    // Contract: Returns nullptr when nothrow==true and allocation fails. When
    //           nothrow==false, runs diagnostics then terminates via
    //           std::terminate(); control never returns to the caller.
    // Notes   : Global new/delete in Core does not throw; OOM hooks still run
    //           through DNG_MEM_CHECK_OOM for diagnostics.
    // ---
    [[nodiscard]] void* HandleAllocationFailure(std::size_t size,
        std::size_t alignment,
        bool nothrow,
        const char* context)
    {
        DNG_MEM_CHECK_OOM(size, alignment, context);
        if (!nothrow)
        {
            std::terminate();
        }
        return nullptr;
    }

    // ---
    // Purpose : Common allocation path shared by all global operator new
    //           overloads.
    // Contract: Returns nullptr when nothrow==true and allocation fails. When
    //           nothrow==false the engine OOM policy will terminate execution;
    //           control never returns to the caller.
    // Notes   : `context` is used for diagnostics only.
    // ---
    [[nodiscard]] void* AllocateGlobal(std::size_t size,
        std::size_t alignment,
        bool nothrow,
        const char* context)
    {
        ThreadReentryGuard guard(gNewReentryFlag);
        const bool isPrimary = guard.IsPrimary();

        const std::size_t normalizedSize = (size == 0) ? 1 : size;
        const std::size_t normalizedAlignment = NormalizeAlignment(alignment);
        DNG_CHECK(IsPowerOfTwo(normalizedAlignment));

        if (!isPrimary)
        {
            // Respect aligned-new contract even under re-entrancy:
            // use the same aligned fallback path and track it in the registry.
            void* storage = nullptr;
            void* ptr = AllocateFallback(normalizedSize, normalizedAlignment, storage);
            if (!ptr)
            {
                return HandleAllocationFailure(normalizedSize, normalizedAlignment, nothrow, context);
            }
            if (!RegisterAllocation(ptr, AllocatorRef{}, normalizedSize, normalizedAlignment, true, storage))
            {
                FreeFallback(ptr, storage);
                return HandleAllocationFailure(sizeof(AllocationRecord), alignof(AllocationRecord), nothrow, "GlobalNew metadata (reentry)");
            }
            return ptr;
        }

        if (!MemorySystem::IsInitialized())
        {
    #if DNG_GLOBAL_NEW_FALLBACK_MALLOC
            EmitFallbackWarningOnce();
            void* storage = nullptr;
            void* ptr = AllocateFallback(normalizedSize, normalizedAlignment, storage);
            if (!ptr)
            {
                return HandleAllocationFailure(normalizedSize, normalizedAlignment, nothrow, context);
            }

            if (!RegisterAllocation(ptr, AllocatorRef{}, normalizedSize, normalizedAlignment, true, storage))
            {
                FreeFallback(ptr, storage);
                return HandleAllocationFailure(sizeof(AllocationRecord), alignof(AllocationRecord), nothrow, "GlobalNew metadata");
            }

            return ptr;
    #else
            DNG_CHECK(false && "Global operator new invoked before MemorySystem::Init() while fallback disabled.");
            return HandleAllocationFailure(normalizedSize, normalizedAlignment, nothrow, context);
    #endif
        }

        const bool useSmall = ShouldUseSmall(normalizedSize, normalizedAlignment);
        AllocatorRef allocator = useSmall ? MemorySystem::GetSmallObjectAllocator()
                                          : MemorySystem::GetDefaultAllocator();

        if (!allocator.IsValid())
        {
            allocator = MemorySystem::GetDefaultAllocator();
        }

        void* ptr = allocator.AllocateBytes(normalizedSize, normalizedAlignment);
        if (!ptr)
        {
            return HandleAllocationFailure(normalizedSize, normalizedAlignment, nothrow, context);
        }

        if (!RegisterAllocation(ptr, allocator, normalizedSize, normalizedAlignment, false, nullptr))
        {
            allocator.DeallocateBytes(ptr, normalizedSize, normalizedAlignment);
            return HandleAllocationFailure(sizeof(AllocationRecord), alignof(AllocationRecord), nothrow, "GlobalNew metadata");
        }

        return ptr;
    }

    // ---
    // Purpose : Common implementation for all global operator delete overloads.
    // Contract: Accepts null pointers (no-op). `sizeHint` and `alignmentHint`
    //           are optional metadata provided by sized/aligned delete forms.
    // Notes   : Not adhering to the recorded allocator triggers a diagnostic to
    //           aid debugging of mismatched new/delete pairs.
    // ---
    void DeallocateGlobal(void* ptr,
        std::size_t sizeHint,
        std::size_t alignmentHint) noexcept
    {
        if (!ptr)
        {
            return;
        }

        ThreadReentryGuard guard(gDeleteReentryFlag);
        const bool isPrimary = guard.IsPrimary();

        if (!isPrimary)
        {
            std::free(ptr);
            return;
        }

        AllocationRecord* record = UnregisterAllocation(ptr);
        if (!record)
        {
            DNG_CHECK(false && "Global delete observed a pointer that was not tracked. Possible double free or foreign pointer.");
            std::free(ptr);
            return;
        }

        if (sizeHint != 0 && sizeHint != record->size)
        {
            DNG_LOG_WARNING("Memory", "Sized delete mismatch: provided=%zu recorded=%zu", static_cast<unsigned long long>(sizeHint), static_cast<unsigned long long>(record->size));
        }

        if (alignmentHint != 0)
        {
            const std::size_t normalized = NormalizeAlignment(alignmentHint);
            if (normalized != record->alignment)
            {
                DNG_LOG_WARNING("Memory", "Aligned delete mismatch: provided=%zu recorded=%zu", static_cast<unsigned long long>(normalized), static_cast<unsigned long long>(record->alignment));
            }
        }

        if (record->usedFallback)
        {
            FreeFallback(ptr, record->fallbackStorage);
        }
        else
        {
            record->allocator.DeallocateBytes(ptr, record->size, record->alignment);
        }

        DestroyAllocationRecord(record);
    }

} // namespace

// ============================================================================
// Global operator new/delete overloads (all variants required by C++17+)
// ============================================================================

void* operator new(std::size_t size)
{
    return AllocateGlobal(size, alignof(std::max_align_t), false, "operator new");
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return AllocateGlobal(size, alignof(std::max_align_t), true, "operator new nothrow");
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return AllocateGlobal(size, static_cast<std::size_t>(alignment), false, "operator new aligned");
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return AllocateGlobal(size, static_cast<std::size_t>(alignment), true, "operator new aligned nothrow");
}

void* operator new[](std::size_t size)
{
    return AllocateGlobal(size, alignof(std::max_align_t), false, "operator new[]");
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return AllocateGlobal(size, alignof(std::max_align_t), true, "operator new[] nothrow");
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return AllocateGlobal(size, static_cast<std::size_t>(alignment), false, "operator new[] aligned");
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return AllocateGlobal(size, static_cast<std::size_t>(alignment), true, "operator new[] aligned nothrow");
}

void operator delete(void* ptr) noexcept
{
    DeallocateGlobal(ptr, 0, 0);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    DeallocateGlobal(ptr, 0, 0);
}

void operator delete(void* ptr, std::size_t size) noexcept
{
    DeallocateGlobal(ptr, size, 0);
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept
{
    DeallocateGlobal(ptr, 0, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::size_t size, std::align_val_t alignment) noexcept
{
    DeallocateGlobal(ptr, size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    DeallocateGlobal(ptr, 0, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::size_t size, const std::nothrow_t&) noexcept
{
    DeallocateGlobal(ptr, size, 0);
}

void operator delete(void* ptr, std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    DeallocateGlobal(ptr, size, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr) noexcept
{
    DeallocateGlobal(ptr, 0, 0);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    DeallocateGlobal(ptr, 0, 0);
}

void operator delete[](void* ptr, std::size_t size) noexcept
{
    DeallocateGlobal(ptr, size, 0);
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
    DeallocateGlobal(ptr, 0, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t alignment) noexcept
{
    DeallocateGlobal(ptr, size, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    DeallocateGlobal(ptr, 0, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::size_t size, const std::nothrow_t&) noexcept
{
    DeallocateGlobal(ptr, size, 0);
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    DeallocateGlobal(ptr, size, static_cast<std::size_t>(alignment));
}

#endif // DNG_ROUTE_GLOBAL_NEW
