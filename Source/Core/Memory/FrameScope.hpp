#pragma once
// ============================================================================
// D-Engine - Core/Memory/FrameScope.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide an RAII helper that rewinds per-thread frame allocators on
//           scope exit, enabling deterministic stack-like lifetimes without
//           manual Reset() calls.
// Contract: Header-only, self-contained, and noexcept. Default construction
//           requires MemorySystem initialization and an enabled thread frame
//           allocator; callers may also supply an explicit FrameAllocator.
//           Destruction rewinds to the captured marker only when owning.
// Notes   : Designed for lightweight usage inside gameplay/render loops where
//           transient allocations must be released predictably. Nested scopes
//           are supported by capturing FrameAllocator markers.
// ============================================================================

#include "Core/Memory/MemorySystem.hpp"
#include "Core/Memory/FrameAllocator.hpp"

namespace dng
{
namespace memory
{
    class FrameScope final
    {
    public:
        // Purpose : Capture the calling thread's frame allocator for scoped rewinds.
        // Contract: MemorySystem::Init() must have succeeded and the thread frame allocator must be enabled.
        // Notes   : Grabs the allocator via MemorySystem and records the current marker.
        FrameScope() noexcept
            : mAllocator(&::dng::memory::MemorySystem::GetThreadFrameAllocator())
            , mMarker(mAllocator->GetMarker())
            , mOwns(true)
        {
        }

        // Purpose : Bind to a caller-supplied frame allocator while capturing its current marker.
        // Contract: Allocator reference must outlive the scope; caller ensures thread affinity.
        // Notes   : Useful for custom frame allocators not managed by MemorySystem.
        explicit FrameScope(::dng::core::FrameAllocator& allocator) noexcept
            : mAllocator(&allocator)
            , mMarker(allocator.GetMarker())
            , mOwns(true)
        {
        }

        FrameScope(const FrameScope&) = delete;
        FrameScope& operator=(const FrameScope&) = delete;

        // Purpose : Transfer scope ownership to another instance without rewinding twice.
        // Contract: Source scope relinquishes responsibility; destination inherits allocator and marker.
        // Notes   : Leaves the moved-from scope in a non-owning, inert state.
        FrameScope(FrameScope&& other) noexcept
            : mAllocator(other.mAllocator)
            , mMarker(other.mMarker)
            , mOwns(other.mOwns)
        {
            other.mAllocator = nullptr;
            other.mOwns = false;
        }

        // Purpose : Move-assign scope ownership, rewinding any currently owned allocator first.
        // Contract: Safe to self-assign; destructor semantics preserved for both scopes.
        // Notes   : Ensures the previous allocator (if any) rewinds before taking ownership of the new one.
        FrameScope& operator=(FrameScope&& other) noexcept
        {
            if (this != &other)
            {
                if (mOwns && mAllocator != nullptr)
                {
                    mAllocator->Rewind(mMarker);
                }

                mAllocator = other.mAllocator;
                mMarker = other.mMarker;
                mOwns = other.mOwns;

                other.mAllocator = nullptr;
                other.mOwns = false;
            }
            return *this;
        }

        // Purpose : Rewind the tracked allocator to its captured marker when the scope ends.
        // Contract: Noexcept; ignores requests if the scope no longer owns the allocator.
        // Notes   : Allows nested scopes to compose without double resets.
        ~FrameScope() noexcept
        {
            if (mOwns && mAllocator != nullptr)
            {
                mAllocator->Rewind(mMarker);
            }
        }

        // Purpose : Access the wrapped frame allocator for allocations within the scope.
        // Contract: Caller must respect frame discipline; returns a valid reference while the scope is alive.
        // Notes   : Asserts in debug builds if the scope was moved-from and no longer tracks an allocator.
        [[nodiscard]] ::dng::core::FrameAllocator& GetAllocator() const noexcept
        {
            DNG_ASSERT(mAllocator != nullptr, "FrameScope::GetAllocator called on released scope");
            return *mAllocator;
        }

        // Purpose : Release ownership so the destructor skips the rewind step.
        // Contract: Safe to call multiple times; subsequent destruction becomes a no-op.
        // Notes   : Useful when callers perform a manual Reset()/Rewind before scope exit.
        void Release() noexcept
        {
            mOwns = false;
        }

        // Purpose : Report whether the scope will rewind on destruction.
        // Contract: No synchronization; reflects current ownership state only.
        // Notes   : Allows guard code to assert ownership expectations when debugging.
        [[nodiscard]] bool Owns() const noexcept
        {
            return mOwns && (mAllocator != nullptr);
        }

    private:
        ::dng::core::FrameAllocator* mAllocator{ nullptr };
        ::dng::core::FrameMarker mMarker{};
        bool mOwns{ false };
    };
} // namespace memory
} // namespace dng
