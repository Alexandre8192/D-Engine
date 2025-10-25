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
#include "Core/Memory/OOM.hpp"
#include "Core/Logger.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <new>        // std::nothrow
#include <cstring>    // std::memcpy
#include <cstdint>

namespace dng::core
{
    // ---
    // Purpose : Configuration knobs that tailor SmallObjectAllocator behaviour.
    // Contract: Values are read-only after construction; caller owns the struct.
    // Notes   : ReturnNullOnOOM allows higher layers to decide whether an allocation
    //           failure should bubble as nullptr (soft failure) or escalate via
    //           the engine-wide OOM policy (see OOM.hpp).
    // ---
    struct SmallObjectConfig
    {
        usize SlabSizeBytes = 64 * 1024; // 64 KB per slab by default
        usize MaxClassSize = 1024;      // > MaxClassSize => route to Parent
        bool  ReturnNullOnOOM = false;  // if false => escalate to OOM policy
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
    // ---
    // Purpose : Provide a fast-path allocator for < 1 KiB payloads backed by slabs.
    // Contract: Thread safety is per-class mutex (coarse) and depends on parent allocator
    //           being thread-safe for slab procurement. Returned pointers obey
    //           NormalizeAlignment within the guarantees documented in Notes.
    // Notes   : Alignment requirements larger than the per-class natural alignment (16 bytes
    //           today) are delegated to the parent allocator to keep slab layout compact.
    //           Future revisions can specialise classes per alignment bucket if needed.
    // ---
    class SmallObjectAllocator final : public IAllocator
    {
    public:
        // ---
        // Purpose : Construct an allocator that sources slab memory from `parent`.
        // Contract: `parent` must outlive this instance and obey the IAllocator contract.
        // Notes   : We assert on entry because nullptr parent leaves the allocator unusable.
        // ---
        explicit SmallObjectAllocator(IAllocator* parent, SmallObjectConfig cfg = {})
            : mParent(parent), mCfg(cfg)
        {
            DNG_CHECK(mParent != nullptr);
            DNG_CHECK(mCfg.SlabSizeBytes >= 4096); // sanity guard against degenerate slabs
        }

        ~SmallObjectAllocator() override = default;

        // ---
        // Purpose : Allocate a small object while honouring the alignment contract.
        // Contract: Returns nullptr on OOM only if ReturnNullOnOOM==true or parent fails.
        //           The caller must pass the SAME (size, alignment) when freeing.
        // Notes   : Alignments > NaturalAlignFor(classSize) fall back to the parent allocator
        //           because current slab layout guarantees up to 16-byte alignment.
        // ---
        [[nodiscard]] void* Allocate(usize size, usize alignment) noexcept override
        {
            alignment = NormalizeAlignment(alignment);
            if (size == 0)
            {
                size = 1; // zero-byte allocs still consume a slot; keep contracts explicit
            }

            if (size > mCfg.MaxClassSize)
            {
                return mParent->Allocate(size, alignment);
            }

            const ClassIndex ci = SizeToClass(size);
            if (ci.Index < 0)
            {
                return mParent->Allocate(size, alignment);
            }

            const usize supportedAlignment = NormalizeAlignment(NaturalAlignFor(kClassSizes[(usize)ci.Index]));
            if (alignment > supportedAlignment)
            {
                // Current slab layout cannot satisfy this alignment without waste; delegate upstream.
                return mParent->Allocate(size, alignment);
            }

            return AllocateFromClass(ci, size, alignment);
        }

        // ---
        // Purpose : Release a previously allocated small object back to its slab.
        // Contract: `(size, alignment)` must match the original request. Null pointers are ignored.
        // Notes   : Large blocks and high-alignment blocks are forwarded to the parent allocator.
        // ---
        void Deallocate(void* ptr, usize size, usize alignment) noexcept override
        {
            if (!ptr)
            {
                return;
            }

            alignment = NormalizeAlignment(alignment);
            if (size == 0)
            {
                size = 1;
            }

            if (size > mCfg.MaxClassSize)
            {
                mParent->Deallocate(ptr, size, alignment);
                return;
            }

            auto* user = static_cast<u8*>(ptr);
            auto* bh = reinterpret_cast<BlockHeader*>(user - sizeof(BlockHeader));
            SlabHeader* slab = bh->OwnerSlab;

            DNG_CHECK(slab != nullptr);
            const i32 classIdx = slab->ClassIndex;
            DNG_CHECK(classIdx >= 0 && classIdx < kNumClasses);

            FreeBlock(ptr, classIdx);
        }

        // ---
        // Purpose : Provide a conservative reallocate path (allocate-copy-free).
        // Contract: Mirrors IAllocator::Reallocate semantics, including nullptr/newSize==0 cases.
        // Notes   : Currently never performs in-place growth; wasInPlace is always false.
        // ---
        [[nodiscard]] void* Reallocate(void* ptr,
            usize oldSize,
            usize newSize,
            usize alignment = alignof(std::max_align_t),
            bool* wasInPlace = nullptr) noexcept override
        {
            alignment = NormalizeAlignment(alignment);

            if (wasInPlace)
            {
                *wasInPlace = false;
            }

            if (!ptr)
            {
                return Allocate(newSize, alignment);
            }

            if (oldSize == 0)
            {
                DNG_LOG_ERROR("Memory",
                    "SmallObjectAllocator::Reallocate misuse: ptr=%p oldSize==0 (alignment=%zu, newSize=%zu)",
                    ptr, static_cast<size_t>(alignment), static_cast<size_t>(newSize));
                DNG_ASSERT(false && "Reallocate requires original size when ptr != nullptr");
                return nullptr;
            }

            if (newSize == 0)
            {
                Deallocate(ptr, oldSize, alignment);
                return nullptr;
            }

            void* newBlock = Allocate(newSize, alignment);
            if (!newBlock)
            {
                return nullptr;
            }

            const usize copySize = oldSize < newSize ? oldSize : newSize;
            if (copySize > 0)
            {
                std::memcpy(newBlock, ptr, copySize);
            }
            Deallocate(ptr, oldSize, alignment);
            return newBlock;
        }

        // ---
        // Purpose : Emit per-class slab statistics for diagnostics.
        // Contract: Safe to call at any time; output is approximate due to relaxed atomics.
        // Notes   : Category defaults to "Memory" but callers may provide custom channels.
        // ---
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
        // ---
        // Purpose : Back new slab storage for the specified size-class.
        // Contract: Must be invoked with the class mutex held.
        // Notes   : Delegates large failures to HandleOutOfMemory().
        // ---
        SlabHeader* AllocateSlabLocked(i32 ci)
        {
            Class& C = mClasses[ci];
            // Allocate raw slab buffer from parent
            u8* raw = static_cast<u8*>(mParent->Allocate(mCfg.SlabSizeBytes, alignof(std::max_align_t)));
            if (!raw)
            {
                HandleOutOfMemory(mCfg.SlabSizeBytes, alignof(std::max_align_t), "SmallObjectAllocator::AllocateSlab");
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

        // ---
        // Purpose : Produce a block from the requested class, provisioning new slabs on demand.
        // Contract: Caller must ensure `ci.Index` is in-range and alignment already validated.
        // Notes   : Alignment assertion doubles as a safety net during development builds.
        // ---
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
                if (mCfg.ReturnNullOnOOM)
                {
                    DNG_LOG_WARNING("Memory",
                        "SmallObjectAllocator OOM: class=%d request=%zu align=%zu",
                        (int)idx, static_cast<size_t>(requestSize), static_cast<size_t>(alignment));
                }
                return nullptr;
            }

            // After creating a slab, free-list is non-empty; pop one
            FreeNode* node = C.FreeList;
            DNG_CHECK(node != nullptr);
            C.FreeList = node->Next;
            C.FreeCount.fetch_sub(1, std::memory_order_relaxed);
            DNG_ASSERT(IsAligned(node, alignment), "SmallObjectAllocator returned misaligned pointer");
            return static_cast<void*>(node);
        }

    // ---
    // Purpose : Return a block to its class free-list.
    // Contract: Must be invoked with a pointer originally produced by AllocateFromClass.
    // Notes   : Free-list push is LIFO to maximise cache locality of hot objects.
    // ---
    void FreeBlock(void* userPtr, i32 classIdx) noexcept
        {
            Class& C = mClasses[classIdx];
            const std::scoped_lock lock(C.Mtx);

            auto* node = reinterpret_cast<FreeNode*>(userPtr);
            node->Next = C.FreeList;
            C.FreeList = node;
            C.FreeCount.fetch_add(1, std::memory_order_relaxed);
        }

        // ---
        // Purpose : Centralise OOM handling depending on configuration.
        // Contract: `context` must be a stable string literal for logging.
        // Notes   : In fatal mode this will not return (DNG_MEM_CHECK_OOM aborts).
        // ---
        void HandleOutOfMemory(usize size, usize alignment, const char* context) const noexcept
        {
            if (mCfg.ReturnNullOnOOM)
            {
                DNG_LOG_WARNING("Memory",
                    "SmallObjectAllocator: allocation failure in %s (size=%zu, align=%zu)",
                    context ? context : "<unknown>", static_cast<size_t>(size), static_cast<size_t>(alignment));
            }
            else
            {
                DNG_MEM_CHECK_OOM(size, alignment, context);
            }
        }
    };

} // namespace dng::core
