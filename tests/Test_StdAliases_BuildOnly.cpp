// ============================================================================
// Purpose : Build-only verification that the STL aliases compile and instantiate
//           with AllocatorAdapter without requiring explicit boilerplate.
// Contract: No runtime assertions; compilation ensures alias syntax correctness
//           and linkage with AllocatorAdapter. MemorySystem is initialised to
//           provide a valid allocator source.
// Notes   : Keep usage minimal to avoid introducing a runtime dependency in
//           the build-only test suite.
// ============================================================================

#include "Core/Containers/StdAliases.hpp"
#include "Core/Memory/MemorySystem.hpp"

[[maybe_unused]] static int StdAliases_BuildOnly()
{
    ::dng::memory::MemorySystem::Init();

    ::dng::vector<int> values;
    values.push_back(1);

    ::dng::unordered_map<int, int> mapping;
    mapping.emplace(1, 2);

    ::dng::memory::MemorySystem::Shutdown();
    return static_cast<int>(values.front() != 1);
}
