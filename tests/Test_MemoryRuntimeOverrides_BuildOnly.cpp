// =========================================================================
// Purpose : Compile-only smoke test for runtime memory overrides precedence.
// Contract: Ensures MemorySystem::Init accepts MemoryConfig overrides for
//           tracking sampling, shard count, and SmallObject batch without
//           requiring runtime execution. The translation unit does not run
//           these calls; success is determined by successful compilation.
// Notes   : Keeps coverage minimal to avoid introducing new runtime deps in
//           the build-only test suite.
// =========================================================================

#include "Core/Memory/MemorySystem.hpp"

[[maybe_unused]] static int MemoryRuntimeOverrides_BuildOnly()
{
    dng::core::MemoryConfig cfg{};
    cfg.tracking_sampling_rate = 4;
    cfg.tracking_shard_count = 8;
    cfg.small_object_batch = 32;
    cfg.collect_stacks = false;

    dng::memory::MemorySystem::Init(cfg);
    dng::memory::MemorySystem::Shutdown();
    return 0;
}
