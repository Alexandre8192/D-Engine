#pragma once

// D-Engine SmallObject / Slab Allocator (MVP v1)
// - Fixed size classes (16..1024)
// - Per-size-class slab lists (default 64 KB slabs)
// - Singly-linked free-list per class (O(1) alloc/free)
// - Thread-safety: one std::mutex per class (simple v1)
// - Parent-backed: uses a parent IAllocator* to allocate slabs
// - Reallocate: naive copy + free (old block not reused)
// - Debug/Diagnostics: DumpStats()

#include "Core/Types.hpp"
#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Logger.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <new>        // std::nothrow
#include <cstring>    // std::memcpy
#include <cstdint>

namespace dng::core
{
    struct SmallObjectConfig
    {
        usize SlabSizeBytes = 64 * 1024; // 64 KB per slab by default
        usize MaxClassSize = 1024;      // > MaxClassSize => route to Parent
        bool  ReturnNullOnOOM = false;     // if false => OnOutOfMemory()
    };

    /**
     * @brief SmallObjectAllocator maps small allocations to fixed-size buckets (slabs).
     *
     * Layout:
     *   [ SlabHeader | blocks... ]
     * Each block stores a tiny BlockHeader (pointer to owning slab) before user memory,
     * then user payload is aligned as requested (>= natural alignment).
     *
     * Threading:
     *   - Mutex per size-class (coarse but simple). Phase 2 can add per-thread caches.
     */
    class SmallObjectAllocator final : public IAllocator
    {
    public:
        explicit SmallObjectAllocator(IAllocator* parent, SmallObjectConfig cfg = {})
            : mParent(parent), mCfg(cfg)
        {
            DNG_CHECK(mParent != nullptr);
            DNG_CHECK(mCfg.SlabSizeBytes >= 4096); // sanity
            // Build size-to-class map lazily (via helper); nothing else to init.
        }

        ~SmallObjectAllocator() override = default;

        void* Allocate(usize size, usize alignment) noexcept override
        {
            alignment = NormalizeAlignment(alignment);
            if (size == 0)
                size = 1;

            if (size > mCfg.MaxClassSize)
            {
                // Delegate to parent for large allocations
                return mParent->Allocate(size, alignment);
            }

            const ClassIndex ci = SizeToClass(size);
            if (ci.Index < 0) // Should not happen unless MaxClassSize < min class
            {
                return mParent->Allocate(size, alignment);
            }
            return AllocateFromClass(ci, size, alignment);
        }

        void Deallocate(void* ptr, usize size, usize alignment) noexcept override
        {
            if (!ptr)
                return;

            alignment = NormalizeAlignment(alignment);
            if (size == 0)
                size = 1;

            if (size > mCfg.MaxClassSize)
            {
                // Large block was delegated to parent
                mParent->Deallocate(ptr, size, alignment);
                return;
            }

            // Recover BlockHeader to get the owning slab/class
            auto* user = static_cast<u8*>(ptr);
            auto* bh = reinterpret_cast<BlockHeader*>(user - sizeof(BlockHeader));
            SlabHeader* slab = bh->OwnerSlab;
            DNG_CHECK(slab != nullptr);
            const i32 classIdx = slab->ClassIndex;
            DNG_CHECK(classIdx >= 0 && classIdx < kNumClasses);

            // Push back to free-list
            FreeBlock(ptr, classIdx);
        }

        void* Reallocate(void* ptr, usize oldSize, usize alignment, usize newSize) noexcept override
        {
            // MVP: naive reallocate (allocate new -> memcpy -> free old)
            if (!ptr)
                return Allocate(newSize, alignment);

            if (newSize == 0)
            {
                Deallocate(ptr, oldSize, alignment);
                return nullptr;
            }

            void* np = Allocate(newSize, alignment);
            if (!np)
                return nullptr;

            const usize copySize = oldSize < newSize ? oldSize : newSize;
            std::memcpy(np, ptr, copySize);
            Deallocate(ptr, oldSize, alignment);
            return np;
        }

        // Diagnostics / Stats
        void DumpStats(const char* category = "Memory") const
        {
            usize totalSlabs = 0, totalMem = 0, totalFree = 0, totalBlocks = 0;

            for (i32 i = 0; i < kNumClasses; ++i)
            {
                const Class& c = mClasses[i];
                const usize blkSize = EffectiveUserBlockSize(i);
                const usize slabCount = c.SlabCount.load(std::memory_order_relaxed);
                const usize freeCount = c.FreeCount.load(std::memory_order_relaxed);

                // Approx blocks = slabCount * blocksPerSlab(i)
                const usize bps = BlocksPerSlab(i);
                const usize blocks = slabCount * bps;

                totalSlabs += slabCount;
                totalBlocks += blocks;
                totalFree += freeCount;
                totalMem += slabCount * mCfg.SlabSizeBytes;

                DNG_LOG_INFO(category,
                    "[SmallObject] class=%d size=%zu bytes, slabs=%zu, blocks=%zu, free=%zu",
                    (int)i, (size_t)blkSize, (size_t)slabCount, (size_t)blocks, (size_t)freeCount);
            }

            DNG_LOG_INFO(category,
                "[SmallObject] TOTAL slabs=%zu, blocks=%zu, free=%zu, mem=%zu KB",
                (size_t)totalSlabs, (size_t)totalBlocks, (size_t)totalFree, (size_t)(totalMem / 1024));
        }

    private:
        // ---- Fixed size-class table (bytes) ----
        static constexpr std::array<usize, 7> kClassSizes = {
            16, 32, 64, 128, 256, 512, 1024
        };
        static constexpr i32 kNumClasses = (i32)kClassSizes.size();

        struct ClassIndex { i32 Index = -1; };

        static constexpr i32 ClassForSize(usize s) noexcept
        {
            for (i32 i = 0; i < kNumClasses; ++i)
                if (s <= kClassSizes[(usize)i]) return i;
            return -1;
        }

        static constexpr usize NaturalAlignFor(usize s) noexcept
        {
            // Basic heuristic: align to min( max(8, nextPow2<=16), classSize )
            if (s <= 8)   return 8;
            if (s <= 16)  return 16;
            if (s <= 32)  return 16;
            if (s <= 64)  return 16;
            if (s <= 128) return 16;
            return 16; // MVP: keep it simple; alignment is capped by class block layout
        }

        // Internal per-block header to retrieve the owning slab on free().
        struct SlabHeader;
        struct BlockHeader
        {
            SlabHeader* OwnerSlab; // back-pointer to slab
        };

        // A slab contains contiguous blocks of the same class.
        struct SlabHeader
        {
            SlabHeader* Next = nullptr;
            i32         ClassIndex = -1;
            u8* Begin = nullptr; // first byte of user-block region
            u8* End = nullptr; // end of slab (exclusive)
        };

        // Free-list node stored at the beginning of the *user* region.
        struct FreeNode
        {
            FreeNode* Next;
        };

        struct Class
        {
            // Head of slab list
            SlabHeader* Slabs = nullptr;
            // Head of free-list for this class
            FreeNode* FreeList = nullptr;

            // Stats (approx)
            std::atomic<usize> SlabCount{ 0 };
            std::atomic<usize> FreeCount{ 0 };

            // Mutex per class for v1
            mutable std::mutex Mtx;
        };

        IAllocator* mParent;
        SmallObjectConfig mCfg;
        Class          mClasses[kNumClasses]{};

        // --- Helpers ---

        ClassIndex SizeToClass(usize request) const noexcept
        {
            const i32 idx = ClassForSize(request);
            ClassIndex ci;
            ci.Index = idx;
            return ci;
        }

        // Size of the metadata placed before the user memory
        static constexpr usize BlockHeaderSize() noexcept { return sizeof(BlockHeader); }

        // Effective per-block payload size for a class (including header & padding to keep user aligned)
        usize EffectiveUserBlockSize(i32 classIdx) const noexcept
        {
            const usize userMax = kClassSizes[(usize)classIdx];
            const usize natural = NaturalAlignFor(userMax);
            const usize hdr = BlockHeaderSize();

            // We must guarantee that returning (blockStart + sizeof(BlockHeader)) can be aligned to 'natural'
            // Simplify: we align the *start of block* to 'natural', then place header, then user.
            // Reserve max(header + alignment slack). MVP: pad block to next multiple of 'natural'.
            const usize minBlock = hdr + userMax;
            const usize padded = AlignUp<usize>(minBlock, natural);
            return padded;
        }

        usize BlocksPerSlab(i32 classIdx) const noexcept
        {
            const usize bsz = EffectiveUserBlockSize(classIdx);
            // Slab layout: [SlabHeader | padding-to-natural | blocks...]
            const usize headerArea = AlignUp<usize>(sizeof(SlabHeader), 16);
            if (mCfg.SlabSizeBytes <= headerArea + bsz) return 1;
            return (mCfg.SlabSizeBytes - headerArea) / bsz;
        }

        // Allocate a new slab for class 'ci', push all blocks into the free-list.
        SlabHeader* AllocateSlabLocked(i32 ci)
        {
            Class& C = mClasses[ci];
            // Allocate raw slab buffer from parent
            u8* raw = static_cast<u8*>(mParent->Allocate(mCfg.SlabSizeBytes, alignof(std::max_align_t)));
            if (!raw)
            {
                if (mCfg.ReturnNullOnOOM)
                    return nullptr;
                OnOutOfMemory(mCfg.SlabSizeBytes, alignof(std::max_align_t));
                return nullptr;
            }

            // Place slab header at start, aligned
            const usize hdrAlignedSize = AlignUp<usize>(sizeof(SlabHeader), 16);
            auto* slab = reinterpret_cast<SlabHeader*>(raw);
            slab->Next = C.Slabs;
            slab->ClassIndex = ci;
            slab->Begin = raw + hdrAlignedSize;
            slab->End = raw + mCfg.SlabSizeBytes;

            // Initialize blocks for free-list
            const usize blkSize = EffectiveUserBlockSize(ci);
            const usize count = (slab->End - slab->Begin) / blkSize;

            u8* cursor = slab->Begin;
            FreeNode* prev = nullptr;
            for (usize i = 0; i < count; ++i)
            {
                // Block layout:
                // [ .. padding to natural .. | BlockHeader | user ... up to blkSize ]
                // We place BlockHeader at the *beginning* of this block region.
                // User pointer is (bh + 1), which is aligned thanks to EffectiveUserBlockSize().
                auto* bh = reinterpret_cast<BlockHeader*>(cursor);
                bh->OwnerSlab = slab;

                // Free node is stored where user would be (bh + 1)
                auto* fn = reinterpret_cast<FreeNode*>(bh + 1);
                fn->Next = prev;
                prev = fn;

                cursor += blkSize;
            }

            // Link slab and free-list
            C.Slabs = slab;
            // Push all new blocks in front of the free-list
            if (prev)
            {
                // Find last in 'prev' chain (we built LIFO so 'prev' is head already)
                // Append existing free-list behind it
                FreeNode* headNew = prev;
                // Count how many were created:
                usize created = count;

                // Concat: new list headNew + old FreeList
                // We don't need to walk; headNew is already the first
                // Link last -> old FreeList (we built LIFO, so the last created is actually at the end;
                // but since we reversed as we created, 'prev' is head; safe to just set its Next to old head? No.)
                // Simpler: prepend the entire newly-built chain to old list by moving head pointer:
                FreeNode* oldHead = C.FreeList;
                // Find tail to concat (one pass)
                FreeNode* tail = headNew;
                while (tail->Next) tail = tail->Next;
                tail->Next = oldHead;

                C.FreeList = headNew;
                C.FreeCount.fetch_add(created, std::memory_order_relaxed);
            }

            C.SlabCount.fetch_add(1, std::memory_order_relaxed);
            return slab;
        }

        void* AllocateFromClass(ClassIndex ci, usize requestSize, usize alignment) noexcept
        {
            const i32 idx = ci.Index;
            Class& C = mClasses[idx];
            const std::scoped_lock lock(C.Mtx);

            // Fast path: pop from free-list
            if (C.FreeList)
            {
                FreeNode* node = C.FreeList;
                C.FreeList = node->Next;
                C.FreeCount.fetch_sub(1, std::memory_order_relaxed);

                // Return user pointer (node is at user area)
                return static_cast<void*>(node);
            }

            // Need a new slab
            SlabHeader* s = AllocateSlabLocked(idx);
            if (!s)
            {
                if (mCfg.ReturnNullOnOOM) {
                    DNG_LOG_WARNING("Memory",
                        "SmallObject OOM: class=%d request=%zu align=%zu",
                        (int)idx, (size_t)requestSize, (size_t)alignment);
                    return nullptr;
                }
                // OnOutOfMemory called in AllocateSlabLocked
                return nullptr;
            }

            // After creating a slab, free-list is non-empty; pop one
            FreeNode* node = C.FreeList;
            DNG_CHECK(node != nullptr);
            C.FreeList = node->Next;
            C.FreeCount.fetch_sub(1, std::memory_order_relaxed);
            return static_cast<void*>(node);
        }

        void FreeBlock(void* userPtr, i32 classIdx) noexcept
        {
            Class& C = mClasses[classIdx];
            const std::scoped_lock lock(C.Mtx);

            auto* node = reinterpret_cast<FreeNode*>(userPtr);
            node->Next = C.FreeList;
            C.FreeList = node;
            C.FreeCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

} // namespace dng::core
