// ============================================================================
// SmallObjectTLSBins Smoke Test (compile-only)
// ----------------------------------------------------------------------------
// Ensures the SmallObjectTLSBins helper remains self-contained and instantiable.
// ============================================================================

#include <cstddef>
#include <type_traits>

#if __has_include("Core/Memory/SmallObjectTLSBins.hpp")
#    include "Core/Memory/SmallObjectTLSBins.hpp"
#else
#    include "../../Source/Core/Memory/SmallObjectTLSBins.hpp"
#endif

#if __has_include("Core/Memory/SmallObjectAllocator.hpp")
#    include "Core/Memory/SmallObjectAllocator.hpp"
#endif

#if __has_include("Core/Memory/GlobalNewDelete.hpp")
#    include "Core/Memory/GlobalNewDelete.hpp"
#endif

namespace
{
    struct DummyNode
    {
        DummyNode* Next = nullptr;
    };

    struct DummyOwner
    {
        using TLS = dng::core::SmallObjectTLSBins<DummyOwner, DummyNode, 2>;

        void FlushThreadCache(typename TLS::ThreadCache&) noexcept {}
        [[nodiscard]] bool IsAlive() const noexcept { return true; }
    };

    static_assert(std::is_trivially_destructible_v<DummyNode>, "DummyNode must be trivially destructible");
#ifdef DNG_GLOBAL_NEW_SMALL_THRESHOLD
    static_assert(DNG_GLOBAL_NEW_SMALL_THRESHOLD >= 2, "Smoke test expects positive small-threshold");
#endif
#if __has_include("Core/Memory/SmallObjectAllocator.hpp")
    static_assert(alignof(dng::core::SmallObjectAllocator) >= alignof(std::max_align_t),
        "SmallObjectAllocator must remain suitably aligned");
#endif

    void InvokeTLSBinsSmoke() noexcept
    {
        DummyOwner owner{};
        using TLS = DummyOwner::TLS;
        auto& cache = TLS::Cache();
        cache.OwnerInstance = &owner;
        cache.Reset(4);
        cache.OwnerInstance = nullptr;
        (void)TLS::ThreadFingerprint();
        (void)TLS::MakeScope(owner);
        (void)TLS::GetProfile();
    }

#if 0
    // Purpose : Illustrate deterministic cross-thread free wiring without executing at runtime.
    // Contract: Shows how a worker thread could flush its cache on exit before returning blocks upstream.
    void CrossThreadFreeSketch()
    {
        DummyOwner primary{};
        auto scope = DummyOwner::TLS::MakeScope(primary);
        (void)scope;
        // auto worker = std::thread([&primary]() { DummyOwner::TLS::FlushOnThreadExit(primary); });
        // worker.join();
    }
#endif
} // namespace
