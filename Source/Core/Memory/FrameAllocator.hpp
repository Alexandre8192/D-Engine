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

    // ---
    // Purpose : Capture the allocator offset to support scoped rewinds without per-allocation frees.
    // Contract: Valid only with the originating FrameAllocator; thread affinity matches the owner; trivially copyable.
    // Notes   : Stores the absolute byte offset from the allocator's backing buffer start.
    // ---
    struct FrameMarker {
        usize Offset{ 0 };
    };

    // ---
    // Purpose : Describe optional behaviour toggles for FrameAllocator diagnostics and OOM handling.
    // Contract: Plain-old-data; callers may copy by value; never allocates.
    // Notes   : Defaults favour returning nullptr on OOM and leaving memory unpoisoned for performance.
    // ---
    struct FrameAllocatorConfig {
        // If true, Allocate() may return nullptr on OOM (caller must handle).
        // If false, triggers the engine OOM policy defined in OOM.hpp.
        bool bReturnNullOnOOM{ true };

        // Optional debug poison fill on Reset/rewind (cheap-ish; disabled by default in Release).
        bool bDebugPoisonOnReset{ false };
        u8   DebugPoisonByte{ 0xDD };
    };

    // ---
    // Purpose : Provide a bump-pointer allocator for transient frame-scoped workloads with optional markers.
    // Contract: Not thread-safe; caller supplies backing storage; allocation normalizes alignment and trusts matching tuples on Rewind.
    // Notes   : Designed for zero-overhead hot paths; ownership reclamation happens via Reset or marker rewinds only.
    // ---
    class FrameAllocator final : public IAllocator {
    public:
        // ---
        // Purpose : Bind the allocator to caller-supplied storage and optional diagnostics configuration.
        // Contract: `backingMemory` must remain valid for the allocator lifetime; `capacityBytes` specifies the usable span.
        // Notes   : No allocation occurs; asserts guard pointer validity in debug builds.
        // ---
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

        // ---
        // Purpose : Allow destruction through the IAllocator interface without releasing resources.
        // Contract: No-op; backing storage ownership remains with the caller.
        // Notes   : Defaulted to keep destructor constexpr-friendly.
        // ---
        ~FrameAllocator() override = default;

        FrameAllocator(const FrameAllocator&) = delete;
        FrameAllocator& operator=(const FrameAllocator&) = delete;

        // IAllocator interface -----------------------------------------------------------------
        // ---
        // Purpose : Allocate a frame-scoped block using bump-pointer semantics.
        // Contract: `size` > 0; alignment normalized internally; returns nullptr only when configured to be non-fatal.
        // Notes   : OOM path either logs (if returning null) or escalates through DNG_MEM_CHECK_OOM.
        // ---
        [[nodiscard]] void* Allocate(usize size, usize alignment = alignof(std::max_align_t)) noexcept override {
            if (size == 0) {
                return nullptr;
            }

            alignment = NormalizeAlignment(alignment);

            const std::uintptr_t currentAddr = reinterpret_cast<std::uintptr_t>(mPtr);
            const std::uintptr_t alignedAddr = AlignUp<std::uintptr_t>(currentAddr, alignment);
            const usize padding = static_cast<usize>(alignedAddr - currentAddr);
            const usize remaining = static_cast<usize>(mEnd - mPtr);

            if ((padding > remaining) || (size > (remaining - padding))) {
                if (mConfig.bReturnNullOnOOM) {
                    if (Logger::IsEnabled(LogLevel::Warn, "Memory")) {
                        DNG_LOG_WARNING("Memory", "FrameAllocator OOM: requested {} bytes (align {}), used={}, cap={}",
                            static_cast<size_t>(size), static_cast<size_t>(alignment),
                            static_cast<size_t>(GetUsed()), static_cast<size_t>(GetCapacity()));
                    }
                    return nullptr;
                }
                DNG_MEM_CHECK_OOM(size, alignment, "FrameAllocator::Allocate");
                return nullptr;
            }

            u8* aligned = reinterpret_cast<u8*>(alignedAddr);
            mPtr = aligned + size;
            return aligned;
        }

        // ---
        // Purpose : Ignore individual frees because memory is reclaimed via Reset/markers.
        // Contract: Accepts null pointers; requires callers to follow frame lifetime discipline.
        // Notes   : Exists solely to satisfy the IAllocator interface.
        // ---
        void Deallocate(void* ptr, usize /*size*/, usize /*alignment*/) noexcept override {
            // Intentionally a no-op. FrameAllocator frees en masse via Reset/Rewind.
            (void)ptr;
        }

        // ---
        // Purpose : Provide a fallback resize implementation by allocating a new block and copying payload.
        // Contract: Preserves original data up to `min(oldSize,newSize)`; returns nullptr on allocation failure (subject to config); sets wasInPlace to false when provided.
        // Notes   : Old block remains live until Reset/Rewind; caller must handle lifetime.
        // ---
        [[nodiscard]] void* Reallocate(void* oldPtr,
            usize oldSize,
            usize newSize,
            usize alignment = alignof(std::max_align_t),
            bool* wasInPlace = nullptr) noexcept override {
            // Simple strategy: allocate new block, memcpy, leave old as garbage (reclaimed on Reset()).
            if (oldPtr == nullptr) return Allocate(newSize, alignment);
            if (newSize == 0) { /* nothing to do */ return nullptr; }

            void* newPtr = Allocate(newSize, alignment);
            if (!newPtr) return nullptr;

            if (wasInPlace) {
                *wasInPlace = false;
            }

            const usize toCopy = oldSize < newSize ? oldSize : newSize;
            std::memcpy(newPtr, oldPtr, static_cast<size_t>(toCopy));
            return newPtr;
        }

        // Frame-specific API -------------------------------------------------------------------
        // ---
        // Purpose : Release every allocation performed since the last Reset or constructor call.
        // Contract: Safe to call even when no allocations were made; optional poison fill obeys config toggles.
        // Notes   : Restores the bump pointer to the beginning of the backing buffer.
        // ---
        void Reset() noexcept {
            if (mConfig.bDebugPoisonOnReset) {
                // Poison only the used range for visibility in tools
                std::memset(mBegin, mConfig.DebugPoisonByte, static_cast<size_t>(GetUsed()));
            }
            mPtr = mBegin;
        }

        // ---
        // Purpose : Snapshot the current bump offset for later rewinds.
        // Contract: Returned marker is only valid with this allocator instance.
        // Notes   : Cheap value-type that callers may store on the stack.
        // ---
        [[nodiscard]] FrameMarker GetMarker() const noexcept {
            return FrameMarker{ static_cast<usize>(mPtr - mBegin) };
        }

        // ---
        // Purpose : Restore the allocator state to a previously captured marker.
        // Contract: Marker must originate from this allocator; obeys debug poison toggle when rewinding.
        // Notes   : Does not destruct objects; callers should manage explicit destructors if needed.
        // ---
        void Rewind(FrameMarker marker) noexcept {
            u8* target = mBegin + marker.Offset;
            if (mConfig.bDebugPoisonOnReset && target < mPtr) {
                std::memset(target, mConfig.DebugPoisonByte, static_cast<size_t>(mPtr - target));
            }
            mPtr = target;
        }

        // Introspection -----------------------------------------------------------------------
        // ---
        // Purpose : Report total backing storage in bytes.
        // Contract: Pure accessor; thread-unsafe in tandem with Allocate/Reset.
        // Notes   : Useful for instrumentation.
        // ---
        [[nodiscard]] usize GetCapacity() const noexcept { return static_cast<usize>(mEnd - mBegin); }
        // ---
        // Purpose : Report how many bytes have been consumed since the last reset.
        // Contract: Pure accessor; reflects bump pointer state.
        // Notes   : Equivalent to `GetCapacity() - GetFree()`.
        // ---
        [[nodiscard]] usize GetUsed()     const noexcept { return static_cast<usize>(mPtr - mBegin); }
        // ---
        // Purpose : Report how many bytes remain available in the backing buffer.
        // Contract: Pure accessor; no synchronization.
        // Notes   : Returns zero when buffer exhausted.
        // ---
        [[nodiscard]] usize GetFree()     const noexcept { return static_cast<usize>(mEnd - mPtr); }
        // ---
        // Purpose : Check whether a pointer lies within the allocator's backing range.
        // Contract: Accepts null; no alignment guarantees implied.
        // Notes   : Helpful for debug assertions.
        // ---
        [[nodiscard]] bool  Owns(const void* p) const noexcept {
            auto* up = reinterpret_cast<const u8*>(p);
            return up >= mBegin && up < mEnd;
        }

        // Convenience helpers -----------------------------------------------------------------
        // ---
        // Purpose : Allocate a trivially typed array via the frame allocator.
        // Contract: `count` multiplied by `sizeof(T)` must not overflow; returns nullptr on OOM when allowed.
        // Notes   : Construction responsibility remains with the caller.
        // ---
        template<class T>
        [[nodiscard]] T* AllocArray(usize count) noexcept {
            void* mem = Allocate(sizeof(T) * count, alignof(T));
            return static_cast<T*>(mem);
        }

        // ---
        // Purpose : Allocate and construct a single object in frame memory.
        // Contract: Returns nullptr when allocation fails under return-null policy; forwards constructor noexceptness.
        // Notes   : Object lifetime still tied to Reset/Rewind semantics.
        // ---
        template<class T, class... Args>
        [[nodiscard]] T* New(Args&&... args) noexcept {
            void* mem = Allocate(sizeof(T), alignof(T));
            if (!mem) return nullptr;
            return std::construct_at(static_cast<T*>(mem), static_cast<Args&&>(args)...);
        }

        // ---
        // Purpose : Invoke the destructor of an object allocated from this allocator.
        // Contract: Accepts null; memory is not reclaimed individually.
        // Notes   : Destruction happens immediately; storage reclaimed by later Reset/Rewind.
        // ---
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

    // ---
    // Purpose : Manage a FrameAllocator instance per thread to remove external synchronisation needs.
    // Contract: Caller provides backing storage; usage restricted to owning thread; Reset must be invoked explicitly.
    // Notes   : Minimal façade; higher-level pooling can build on top without ABI changes.
    // ---
    class ThreadLocalFrameAllocator final {
    public:
        // ---
        // Purpose : Construct the thread-local wrapper by forwarding configuration to the underlying allocator.
        // Contract: `backing` and `bytes` mirror FrameAllocator constructor requirements; cfg copied by value.
        // Notes   : Keeps allocator value-initialised for deterministic startup.
        // ---
        ThreadLocalFrameAllocator(void* backing, usize bytes, FrameAllocatorConfig cfg = {}) noexcept
            : mAllocator(backing, bytes, cfg) {
        }

        // ---
        // Purpose : Access the underlying FrameAllocator for allocation operations.
        // Contract: Caller must respect single-thread ownership; reference valid for wrapper lifetime.
        // Notes   : Provides mutable access to expose full FrameAllocator API.
        // ---
        [[nodiscard]] FrameAllocator& Get() noexcept { return mAllocator; }
        // ---
        // Purpose : Reset the per-thread allocator at frame boundaries.
        // Contract: Must be called by owning thread; mirrors FrameAllocator::Reset semantics.
        // Notes   : Convenience forwarding helper.
        // ---
        void Reset() noexcept { mAllocator.Reset(); }

    private:
        FrameAllocator mAllocator;
    };

} // namespace dng::core
