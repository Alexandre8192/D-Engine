#pragma once
// ============================================================================
// D-Engine - Core/Memory/PageAllocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Expose a minimal, cross-platform virtual memory facade that safely
//           wraps page reservation, commitment, and release without imposing a
//           higher-level policy. Serves as the substrate for GuardAllocator,
//           arena chains, and other paging allocators.
// Contract: All functions are stateless and thread-safe. Callers must provide
//           sizes > 0 that are multiples of PageSize(); helper functions align
//           upward automatically. Reserve/Release and Commit/Decommit must be
//           paired with identical (ptr, size) parameters and never mixed across
//           overlapping regions.
// Notes   : Windows paths rely on VirtualAlloc/VirtualFree while POSIX paths
//           lean on mmap/munmap/mprotect. Logging is intentionally lightweight
//           to avoid additional dependencies. GuardPage() is a thin helper used
//           primarily by GuardAllocator.
// ============================================================================

#include "Core/CoreMinimal.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Diagnostics/Check.hpp"

// ------------------------------------------------------------------------
// Platform includes (minimal)
// ------------------------------------------------------------------------
#if DNG_PLATFORM_WINDOWS
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <errno.h>
#    include <sys/mman.h>
#    include <unistd.h>
#endif

namespace dng
{
namespace memory
{
    // ------------------------------------------------------------------------
    // Log category for all virtual memory operations
    // ------------------------------------------------------------------------
#ifndef DNG_PAGE_ALLOCATOR_LOG_CATEGORY
#    define DNG_PAGE_ALLOCATOR_LOG_CATEGORY "Memory.PageAllocator"
#endif

    // ------------------------------------------------------------------------
    // PageSize()
    // ------------------------------------------------------------------------
    // Purpose : Return the native OS page size in bytes.
    // Contract: Thread-safe, caches the value on first call, never returns 0.
    // Notes   : Most platforms report a constant page size for the lifetime of
    //           the process, so a simple static cache suffices.
    // ------------------------------------------------------------------------
    [[nodiscard]] inline size_t PageSize() noexcept
    {
    // Design Note: function-local static keeps this cache deterministic without exposing
    // additional global symbols, aligning with header-first constraints while avoiding recomputation.
    static size_t cached = 0;
        if (cached == 0)
        {
#if defined(_WIN32)
            SYSTEM_INFO sysInfo{};
            ::GetSystemInfo(&sysInfo);
            cached = static_cast<size_t>(sysInfo.dwPageSize);
#else
            long page = ::sysconf(_SC_PAGESIZE);
            if (page <= 0)
            {
                page = 4096;
            }
            cached = static_cast<size_t>(page);
#endif
        }
        return cached;
    }

    // ------------------------------------------------------------------------
    // Reserve()
    // ------------------------------------------------------------------------
    // Purpose : Reserve a contiguous virtual address range without committing
    //           physical pages.
    // Contract: Size must be positive; it is aligned upward to PageSize().
    //           Returns nullptr on failure (caller decides OOM policy).
    // Notes   : The reserved space must be released with Release().
    // ------------------------------------------------------------------------
    [[nodiscard]] inline void* Reserve(size_t size) noexcept
    {
        const size_t pageSize = PageSize();
        size = ::dng::core::AlignUp<size_t>(size, pageSize);
        if (size == 0)
        {
            DNG_CHECK(false);
            return nullptr;
        }

#if defined(_WIN32)
        void* ptr = ::VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
        if (!ptr)
        {
            DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY, "Reserve() failed (Windows): {}", ::GetLastError());
        }
        return ptr;
#else
        void* ptr = ::mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED)
        {
            DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY, "Reserve() failed (POSIX): errno={}", errno);
            return nullptr;
        }
        return ptr;
#endif
    }

    // ------------------------------------------------------------------------
    // Commit()
    // ------------------------------------------------------------------------
    // Purpose : Commit a previously reserved range so the CPU can access it.
    // Contract: ptr must originate from Reserve(); size is rounded up to the
    //           nearest page multiple and the pointer must be page-aligned.
    // Notes   : Errors surface via DNG_LOG_ERROR but there is no automatic
    //           fallback; callers may wrap this in their own policy.
    // ------------------------------------------------------------------------
    inline void Commit(void* ptr, size_t size) noexcept
    {
        if (!ptr || size == 0)
        {
            DNG_CHECK(false);
            return;
        }

        const size_t pageSize = PageSize();
        size = ::dng::core::AlignUp<size_t>(size, pageSize);
        DNG_ASSERT(::dng::core::IsAligned<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(ptr), pageSize),
            "Commit() expects a page-aligned pointer");

#if defined(_WIN32)
        void* result = ::VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
        if (!result)
        {
            DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY, "Commit() failed (Windows): {}", ::GetLastError());
        }
#else
        if (::mprotect(ptr, size, PROT_READ | PROT_WRITE) != 0)
        {
            DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY, "Commit() failed (POSIX): errno={}", errno);
        }
#endif
    }

    // ------------------------------------------------------------------------
    // Decommit()
    // ------------------------------------------------------------------------
    // Purpose : Release physical pages while keeping the virtual range
    //           reserved for future reuse.
    // Contract: Pointer must be page-aligned, size matches prior Commit().
    // Notes   : POSIX path uses madvise as a hint before revoking access.
    // ------------------------------------------------------------------------
    inline void Decommit(void* ptr, size_t size) noexcept
    {
        if (!ptr || size == 0)
        {
            DNG_CHECK(false);
            return;
        }

        const size_t pageSize = PageSize();
        size = ::dng::core::AlignUp<size_t>(size, pageSize);
        DNG_ASSERT(::dng::core::IsAligned<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(ptr), pageSize),
            "Decommit() expects a page-aligned pointer");

#if defined(_WIN32)
        if (!::VirtualFree(ptr, size, MEM_DECOMMIT))
        {
            DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY, "Decommit() failed (Windows): {}", ::GetLastError());
        }
#else
        if (::madvise(ptr, size, MADV_DONTNEED) != 0)
        {
            DNG_LOG_WARNING(DNG_PAGE_ALLOCATOR_LOG_CATEGORY, "Decommit() madvise fallback (errno={})", errno);
        }
        if (::mprotect(ptr, size, PROT_NONE) != 0)
        {
            DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY, "Decommit() failed (POSIX): errno={}", errno);
        }
#endif
    }

    // ------------------------------------------------------------------------
    // Release()
    // ------------------------------------------------------------------------
    // Purpose : Release an entire reservation.
    // Contract: ptr must match the base returned by Reserve(); size is rounded
    //           up to PageSize(). After this call the range is invalid.
    // Notes   : Windows path ignores the size parameter per VirtualFree API.
    // ------------------------------------------------------------------------
    inline void Release(void* ptr, size_t size) noexcept
    {
        if (!ptr || size == 0)
        {
            DNG_CHECK(false);
            return;
        }

        const size_t pageSize = PageSize();
        size = ::dng::core::AlignUp<size_t>(size, pageSize);
        DNG_ASSERT(::dng::core::IsAligned<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(ptr), pageSize),
            "Release() expects a page-aligned pointer");

#if defined(_WIN32)
        if (!::VirtualFree(ptr, 0, MEM_RELEASE))
        {
            DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY, "Release() failed (Windows): {}", ::GetLastError());
        }
#else
        if (::munmap(ptr, size) != 0)
        {
            DNG_LOG_ERROR(DNG_PAGE_ALLOCATOR_LOG_CATEGORY, "Release() failed (POSIX): errno={}", errno);
        }
#endif
    }

    // ------------------------------------------------------------------------
    // GuardPage()
    // ------------------------------------------------------------------------
    // Purpose : Flip one page to PAGE_NOACCESS/PROT_NONE so accidental reuse
    //           faults immediately. Primarily consumed by GuardAllocator.
    // Contract: ptr must be page-aligned; no-op for nullptr.
    // Notes   : Best-effort helper; callers can choose to treat failure as
    //           fatal if they require hard guarantees.
    // ------------------------------------------------------------------------
    inline void GuardPage(void* ptr) noexcept
    {
        if (!ptr)
        {
            return;
        }

#if DNG_PLATFORM_WINDOWS
        DWORD oldProtect;
        ::VirtualProtect(ptr, PageSize(), PAGE_NOACCESS, &oldProtect);
#else
        ::mprotect(ptr, PageSize(), PROT_NONE);
#endif
    }
} // namespace memory
} // namespace dng
