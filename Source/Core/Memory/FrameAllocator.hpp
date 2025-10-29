#pragma once
// ============================================================================
// D-Engine - Core/Memory/FrameAllocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a lightweight bump allocator for transient, per-frame
//           workloads where allocations are bulk-released via Reset() or
//           markers. Designed for zero abstraction overhead in hot paths.
// Contract: Header-only and single-threaded by default; callers must normalize
//           synchronization at a higher layer (e.g., per-thread instances).
//           All Allocate() calls normalize alignment via NormalizeAlignment().
//           Deallocate() is a documented no-op—memory is reclaimed only by
//           Reset() / Rewind(marker). Optional poison fills are guarded by
//           FrameAllocatorConfig and off by default in Release builds.
// Notes   : Integrates with the wider allocator ecosystem by inheriting
//           IAllocator so it can be wrapped (e.g., Tracking/Guard). Reset()
//           forms the natural end-of-frame barrier.
// ============================================================================

#include "Core/Types.hpp"
#include "Core/Diagnostics/Check.hpp"
#include "Core/Logger.hpp"
#include "Core/Memory/Alignment.hpp"    // NormalizeAlignment()
#include "Core/Memory/MemoryConfig.hpp" // DNG_MEM_* knobs
#include "Core/Memory/Allocator.hpp"    // IAllocator (for interface compatibility)
#include "Core/Memory/OOM.hpp"
#include "Core/Platform/PlatformCompiler.hpp"
#include "Core/Platform/PlatformDefines.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace dng::core {

    // ------------------------------------------------------------------------
    // FrameMarker
    // ------------------------------------------------------------------------
    // Purpose : Capture the allocator offset so clients can perform scoped
    //           rewinds without freeing each block individually.
    // Contract: Markers are only valid with the allocator that created them.
    //           Thread affinity follows the owning allocator instance.
    // Notes   : Offset is stored as an absolute byte index from mBegin.
    struct FrameMarker {
        usize Offset{ 0 };
    };

    // ------------------------------------------------------------------------
    // FrameAllocatorConfig
    // ------------------------------------------------------------------------
    // Purpose : Capture optional diagnostics behaviour (return-null vs fatal
    //           on OOM, poison buffers on Reset/Rewind).
    // Contract: Safe to copy/compare trivially; no runtime allocations.
    // Notes   : Defaults favour benign behaviour (null on OOM, no poison).
    struct FrameAllocatorConfig {
        // If true, Allocate() may return nullptr on OOM (caller must handle).
        // If false, triggers the engine OOM policy defined in OOM.hpp.
        bool bReturnNullOnOOM{ true };

        // Optional debug poison fill on Reset/rewind (cheap-ish; disabled by default in Release).
        bool bDebugPoisonOnReset{ false };
        u8   DebugPoisonByte{ 0xDD };
    };

    /**
     * @brief Linear per-frame allocator (bump pointer).
     *
     * Memory ownership model:
     *  - This allocator DOES NOT own the backing buffer; you pass it in.
     *  - Integration with a VirtualMemory layer or a parent IAllocator can come later.
     */
    // ------------------------------------------------------------------------
    // FrameAllocator
    // ------------------------------------------------------------------------
    // Purpose : Linear allocator for transient workloads (frame or scope
    //           lifetime) with optional marker/rewind support.
    // Contract: Not thread-safe. Allocate() normalizes alignment, honours the
    //           bReturnNullOnOOM toggle, and updates the bump pointer in O(1).
    //           Deallocate() intentionally does nothing; Reset()/Rewind() are
    //           the only reclamation mechanisms.
    // Notes   : Backing storage is caller-owned. Future integration with a
    //           virtual-memory layer can be layered on top without changing the
    //           contract.
    class FrameAllocator final : public IAllocator {
    public:
        FrameAllocator(void* backingMemory, usize capacityBytes, FrameAllocatorConfig cfg = {}) noexcept
            : mBegin(reinterpret_cast<u8*>(backingMemory))
            , mPtr(reinterpret_cast<u8*>(backingMemory))
            , mEnd(reinterpret_cast<u8*>(backingMemory) + capacityBytes)
            , mConfig(cfg)
        {
            // Sanity
            DNG_ASSERT(mBegin != nullptr, "FrameAllocator requires a valid backing buffer");
            DNG_ASSERT(mBegin <= mEnd, "Invalid capacity range");
        }

        // Non-owning; trivial dtor
        ~FrameAllocator() override = default;

        FrameAllocator(const FrameAllocator&) = delete;
        FrameAllocator& operator=(const FrameAllocator&) = delete;

        // IAllocator interface -----------------------------------------------------------------
        [[nodiscard]] void* Allocate(usize size, usize alignment = alignof(std::max_align_t)) noexcept override {
            if (size == 0) {
                return nullptr;
            }

            alignment = NormalizeAlignment(alignment);

            // Compute aligned pointer from current bump
            u8* current = mPtr;
            const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(current);
            const std::uintptr_t mis = p % alignment;
            const usize padding = mis ? (alignment - static_cast<usize>(mis)) : 0u;

            u8* aligned = nullptr;
            if (mPtr + padding + size <= mEnd) {
                aligned = mPtr + padding;
                mPtr = aligned + size; // bump
            } else if (mConfig.bReturnNullOnOOM) {
                if (Logger::IsEnabled("Memory")) {
                    DNG_LOG_WARNING("Memory", "FrameAllocator OOM: requested {} bytes (align {}), used={}, cap={}",
                        static_cast<size_t>(size), static_cast<size_t>(alignment),
                        static_cast<size_t>(GetUsed()), static_cast<size_t>(GetCapacity()));
                }
            } else {
                DNG_MEM_CHECK_OOM(size, alignment, "FrameAllocator::Allocate");
            }

            return aligned;
        }

        void Deallocate(void* ptr, usize /*size*/, usize /*alignment*/) noexcept override {
            // Intentionally a no-op. FrameAllocator frees en masse via Reset/Rewind.
            (void)ptr;
        }

        [[nodiscard]] void* Reallocate(void* oldPtr, usize oldSize, usize newSize, usize alignment = alignof(std::max_align_t)) noexcept override {
            // Simple strategy: allocate new block, memcpy, leave old as garbage (reclaimed on Reset()).
            if (oldPtr == nullptr) return Allocate(newSize, alignment);
            if (newSize == 0) { /* nothing to do */ return nullptr; }

            void* newPtr = Allocate(newSize, alignment);
            if (!newPtr) return nullptr;

            const usize toCopy = oldSize < newSize ? oldSize : newSize;
            std::memcpy(newPtr, oldPtr, static_cast<size_t>(toCopy));
            return newPtr;
        }

        // Frame-specific API -------------------------------------------------------------------
    /** Free ALL allocations done since last Reset()/construction. */
        void Reset() noexcept {
            if (mConfig.bDebugPoisonOnReset) {
                // Poison only the used range for visibility in tools
                std::memset(mBegin, mConfig.DebugPoisonByte, static_cast<size_t>(GetUsed()));
            }
            mPtr = mBegin;
        }

    /** Capture current bump offset to allow LIFO rewind. */
    [[nodiscard]] FrameMarker GetMarker() const noexcept {
            return FrameMarker{ static_cast<usize>(mPtr - mBegin) };
        }

    /** Rewind to a previously captured marker. */
    void Rewind(FrameMarker marker) noexcept {
            u8* target = mBegin + marker.Offset;
            if (mConfig.bDebugPoisonOnReset && target < mPtr) {
                std::memset(target, mConfig.DebugPoisonByte, static_cast<size_t>(mPtr - target));
            }
            mPtr = target;
        }

        // Introspection -----------------------------------------------------------------------
        [[nodiscard]] usize GetCapacity() const noexcept { return static_cast<usize>(mEnd - mBegin); }
        [[nodiscard]] usize GetUsed()     const noexcept { return static_cast<usize>(mPtr - mBegin); }
        [[nodiscard]] usize GetFree()     const noexcept { return static_cast<usize>(mEnd - mPtr); }
        [[nodiscard]] bool  Owns(const void* p) const noexcept {
            auto* up = reinterpret_cast<const u8*>(p);
            return up >= mBegin && up < mEnd;
        }

        // Convenience helpers -----------------------------------------------------------------
        template<class T>
        [[nodiscard]] T* AllocArray(usize count) noexcept {
            void* mem = Allocate(sizeof(T) * count, alignof(T));
            return static_cast<T*>(mem);
        }

        template<class T, class... Args>
        [[nodiscard]] T* New(Args&&... args) noexcept {
            void* mem = Allocate(sizeof(T), alignof(T));
            if (!mem) return nullptr;
            return std::construct_at(static_cast<T*>(mem), static_cast<Args&&>(args)...);
        }

        template<class T>
        void Delete(T* obj) noexcept {
            if (!obj) return;
            std::destroy_at(obj);
            // memory is not reclaimed individually; Reset()/Rewind() will free en masse
        }

    private:
        u8* mBegin{ nullptr };
        u8* mPtr{ nullptr };
        u8* mEnd{ nullptr };
        FrameAllocatorConfig mConfig{};
    };

    // -----------------------------------------------------------------------------------------
    // ThreadLocalFrameAllocator
    // -----------------------------------------------------------------------------------------
    // Purpose : Provide a convenience wrapper that owns a FrameAllocator per
    //           thread so callers can avoid synchronisation entirely.
    // Contract: Construction forwards directly to FrameAllocator; Reset() must
    //           be invoked by the owning thread at the end of its frame. No
    //           cross-thread access is permitted.
    // Notes   : Acts as a minimal façade; more elaborate policies (e.g.,
    //           pooling) can layer on top.
    class ThreadLocalFrameAllocator final {
    public:
        ThreadLocalFrameAllocator(void* backing, usize bytes, FrameAllocatorConfig cfg = {}) noexcept
            : mAllocator(backing, bytes, cfg) {
        }

        [[nodiscard]] FrameAllocator& Get() noexcept { return mAllocator; }
        void Reset() noexcept { mAllocator.Reset(); }

    private:
        FrameAllocator mAllocator;
    };

} // namespace dng::core
