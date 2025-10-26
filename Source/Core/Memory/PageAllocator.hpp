#pragma once
// ============================================================================
// D-Engine : PageAllocator.hpp
// ============================================================================
// Purpose : Cross-platform virtual memory abstraction layer.
//           Provides deterministic, low-level control over page reservation,
//           commitment, decommitment, and release. Forms the foundation for
//           higher-level allocators (slab, arena, guard, tracking, etc.).
//
// Contract: 
//   - All functions are thread-safe and stateless.
//   - Sizes must be positive and page-aligned (normalized via NormalizeAlignment).
//   - The caller is responsible for matching Reserve/Release and Commit/Decommit
//     pairs using identical (ptr, size) parameters.
//   - Behavior is undefined if ranges overlap or cross process-owned regions.
//
// Notes   :
//   - Implemented entirely in headers for full transparency.
//   - Uses VirtualAlloc/VirtualFree on Windows and mmap/munmap/mprotect on POSIX.
//   - Guard pages can later be emulated by reserving + committing one page with
//     PROT_NONE / PAGE_NOACCESS.
//   - Includes optional logging and runtime diagnostics through DNG_LOG_* macros.
// ============================================================================

#include "Core/CoreMinimal.hpp"

namespace dng
{
namespace memory
{
	// ------------------------------------------------------------------------
	// Log category for all virtual memory operations
	// ------------------------------------------------------------------------
	#ifndef DNG_PAGE_ALLOCATOR_LOG_CATEGORY
	#define DNG_PAGE_ALLOCATOR_LOG_CATEGORY "Memory.PageAllocator"
	#endif

	// ------------------------------------------------------------------------
	// Platform includes (minimal)
	// ------------------------------------------------------------------------
	#if defined(_WIN32)
		#define NOMINMAX
		#include <windows.h>
	#else
		#include <sys/mman.h>
		#include <unistd.h>
		#include <errno.h>
	#endif

	// ------------------------------------------------------------------------
	// PageSize()
	// ------------------------------------------------------------------------
	// Purpose : Returns the system's native page size in bytes.
	// Contract: Thread-safe, never fails, always >= 4 KB.
	// Notes   : Cached at runtime if necessary (constant on most systems).
	// ------------------------------------------------------------------------
	[[nodiscard]] inline size_t PageSize() noexcept
	{
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
	// Purpose : Reserves a range of virtual address space without committing
	//           physical memory. This only reserves address range.
	// Contract: 
	//   - Size > 0 and will be normalized to a multiple of PageSize().
	//   - Returns nullptr on failure (caller should handle OOM).
	// Notes   : Use Commit() to make it accessible. Release() frees it.
	// ------------------------------------------------------------------------
	[[nodiscard]] inline void* Reserve(size_t size) noexcept
	{
		size = AlignUp(size, PageSize());
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
	// Purpose : Commits physical memory to an already-reserved range, making it
	//           accessible for read/write.
	// Contract:
	//   - ptr must have been returned by Reserve().
	//   - size must be a multiple of PageSize().
	// Notes   : May expand commit granularity internally (OS-dependent).
	// ------------------------------------------------------------------------
	inline void Commit(void* ptr, size_t size) noexcept
	{
		if (!ptr || size == 0)
		{
			DNG_CHECK(false);
			return;
		}
		size = AlignUp(size, PageSize());
		DNG_ASSERT(IsAligned(ptr, PageSize()), "Commit() expects a page-aligned pointer");

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
	// Purpose : Releases physical memory but keeps the virtual address range
	//           reserved. The memory becomes inaccessible until recommitted.
	// Contract:
	//   - ptr must be valid and aligned to a page boundary.
	//   - size must match the region previously committed.
	// Notes   : Use this to shrink working set without losing the address space.
	// ------------------------------------------------------------------------
	inline void Decommit(void* ptr, size_t size) noexcept
	{
		if (!ptr || size == 0)
		{
			DNG_CHECK(false);
			return;
		}
		size = AlignUp(size, PageSize());
		DNG_ASSERT(IsAligned(ptr, PageSize()), "Decommit() expects a page-aligned pointer");

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
	// Purpose : Frees an entire reserved virtual memory region.
	// Contract:
	//   - Must match a prior Reserve() range exactly.
	//   - After this call, ptr becomes invalid.
	// Notes   : Typically called by the allocator owning the region.
	// ------------------------------------------------------------------------
	inline void Release(void* ptr, size_t size) noexcept
	{
		if (!ptr || size == 0)
		{
			DNG_CHECK(false);
			return;
		}
		size = AlignUp(size, PageSize());
		DNG_ASSERT(IsAligned(ptr, PageSize()), "Release() expects a page-aligned pointer");

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
	// Optional future utility : GuardPage()
	// ------------------------------------------------------------------------
	// Purpose : Marks a single page as non-accessible (for debug).
	// Notes   : Used by GuardAllocator to catch out-of-bounds writes.
	// ------------------------------------------------------------------------
	inline void GuardPage(void* ptr) noexcept
	{
		if (!ptr) return;
	#if defined(_WIN32)
		DWORD oldProtect;
		::VirtualProtect(ptr, PageSize(), PAGE_NOACCESS, &oldProtect);
	#else
		::mprotect(ptr, PageSize(), PROT_NONE);
	#endif
	}
} // namespace memory
} // namespace dng
