#pragma once

// D-Engine - FrameAllocator (Linear/Scratch per frame)
// ----------------------------------------------------
// A fast, bump-pointer allocator intended for short-lived, per-frame allocations.
// Release strategy: one-shot Reset() typically called at EndFrame().
//
// Design goals
//  - Header-only, zero deps besides CoreMinimal subset.
//  - Strict alignment guarantees via NormalizeAlignment().
//  - No implicit synchronization (single-producer by default).
//  - Configurable OOM policy (return nullptr vs fail fast via OOM.hpp policy).
//  - Mark/rewind support for manual LIFO scopes.
//  - Optional debug fills & guard knobs (cheap in Release).
//
// Non-goals (for now)
//  - Multi-producer contention elimination (use a per-thread instance instead).
//  - Automatic overflow chaining to a backing allocator (kept as TODO).
//  - Page reservation/commit (will be provided by VirtualMemory layer later).
//
// Typical usage
//  FrameAllocator frame(mem, size);
//  // Per frame
//  {
//      auto* tmp = frame.Allocate(bytes, alignof(T));
//      // ... use tmp temporaries ...
//      frame.Reset(); // free all per-frame allocations
//  }
//
//  // Scoped LIFO block
//  {
//      auto m = frame.GetMarker();
//      void* a = frame.Allocate(128);
//      void* b = frame.Allocate(64);
//      frame.Rewind(m); // frees b then a in one shot
//  }

#include "Core/Types.hpp"
#include "Core/Memory/Alignment.hpp"    // NormalizeAlignment()
#include "Core/Memory/MemoryConfig.hpp" // DNG_MEM_* knobs
#include "Core/Memory/Allocator.hpp"    // IAllocator (for interface compatibility)
#include "Core/Platform/PlatformDefines.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

// Temporary logging/assert fallbacks (no-ops until Logger.hpp is wired)
#ifndef DNG_LOG_WARNING
#  define DNG_LOG_WARNING(Category, Fmt, ...) ((void)0)
#endif
#ifndef DNG_LOG_ERROR
#  define DNG_LOG_ERROR(Category, Fmt, ...) ((void)0)
#endif
#ifndef DNG_ASSERT
#  define DNG_ASSERT(Expr, Msg) ((void)0)
#endif

namespace dng::core {

    /** Lightweight marker capturing the current bump offset. */
    struct FrameMarker {
        usize Offset{ 0 };
    };

    /** Configuration flags for FrameAllocator behavior. */
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
            if (size == 0) return nullptr;

            alignment = NormalizeAlignment(alignment);

            // Compute aligned pointer from current bump
            u8* current = mPtr;
            std::uintptr_t p = reinterpret_cast<std::uintptr_t>(current);
            std::uintptr_t mis = p % alignment;
            usize padding = mis ? (alignment - static_cast<usize>(mis)) : 0u;

            // Check space
            if (mPtr + padding + size > mEnd) {
                // OOM path
                if (mConfig.bReturnNullOnOOM) {
                    DNG_LOG_WARNING(Memory, "FrameAllocator OOM: requested %zu bytes (align %zu), used=%zu, cap=%zu",
                        static_cast<size_t>(size), static_cast<size_t>(alignment),
                        static_cast<size_t>(GetUsed()), static_cast<size_t>(GetCapacity()));
                    return nullptr;
                }
                // Fail fast via engine policy (will typically log + abort)
                OnOutOfMemory(size, alignment);
                return nullptr; // unreachable in most policies
            }

            u8* aligned = mPtr + padding;
            mPtr = aligned + size; // bump
            return aligned;
        }

        void Deallocate(void* ptr, usize /*size*/, usize /*alignment*/) noexcept override {
			// Intentionally a no-op. FrameAllocator frees massively on Reset()/Rewind().
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
        // Dispatch to the engine OOM policy (OOM.hpp). The concrete symbol lives elsewhere.
        DNG_NO_INLINE void OnOutOfMemory(usize size, usize alignment) noexcept {
            // The engine policy may log, break, or abort; keeping the call indirect here.
            (void)size; (void)alignment;
            // If your OOM policy is a macro function, call it here instead of this stub.
            DNG_LOG_ERROR(Memory, "FrameAllocator hard OOM (requested=%zu, align=%zu)", (size_t)size, (size_t)alignment);
        }

    private:
        u8* mBegin{ nullptr };
        u8* mPtr{ nullptr };
        u8* mEnd{ nullptr };
        FrameAllocatorConfig mConfig{};
    };

    // -----------------------------------------------------------------------------------------
    // Optional: minimal thread-local wrapper (one allocator per thread).
    // Create once per thread at startup or on first use, Reset() at EndFrame per thread.
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
