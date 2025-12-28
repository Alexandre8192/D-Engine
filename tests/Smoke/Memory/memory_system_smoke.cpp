// ============================================================================
// Memory System Smoke Test (compile-only)
// ----------------------------------------------------------------------------
// Ensures MemorySystem public headers remain self-contained.
// ============================================================================

#if __has_include("Core/Memory/MemorySystem.hpp")
#    include "Core/Memory/MemorySystem.hpp"
#else
#    include "../../Source/Core/Memory/MemorySystem.hpp"
#endif

#if 0
using namespace dng::memory;

static void MemorySystemSmoke()
{
    MemoryConfig cfg{};
    MemorySystem::Init(cfg);
    MemorySystem::Shutdown();
}
#endif
