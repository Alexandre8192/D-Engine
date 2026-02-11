// ============================================================================
// D-Engine - tests/AllSmokes/AllSmokes_main.cpp
// ----------------------------------------------------------------------------
// Purpose : Aggregate executable that runs all smoke helpers.
// Contract: No exceptions/RTTI; deterministic ordering; returns 0 on success.
// Notes   : Assumes Run*Smoke helpers are linked from their respective TUs.
// ============================================================================

#include <cstdio>

int RunRendererSystemSmoke();
int RunBasicForwardRendererSmoke();
int RunRendererSystemBasicForwardRendererSmoke();
int RunTimeSmoke();
int RunFileSystemSmoke();
int RunWindowSmoke();
int RunInputSmoke();
int RunJobsSmoke();
int RunAudioSmoke();
int RunAudioPlaybackSmoke();
int RunCoreRuntimeSmoke();
int RunDeterminismReplaySmoke();
int RunArenaAllocatorSmoke();
int RunFrameAllocatorSmoke();
int RunStackAllocatorSmoke();
int RunSmallObjectAllocatorSmoke();
int RunLoggerOnlySmoke();
int RunGuardAllocatorAlignmentSmoke();
int RunAllocatorAdapterSmoke();
int RunFrameScopeSmoke();
int RunMemorySystemSmoke();
int RunNewDeleteSmoke();
int RunOOMPolicySmoke();
int RunPageAllocatorSmoke();
int RunPoolAllocatorSmoke();
int RunSmallObjectTLSBinsSmoke();
int RunTrackingAllocatorSmoke();

namespace
{
    struct SmokeEntry
    {
        const char* name;
        int (*func)();
    };
}

int main()
{
    const SmokeEntry smokes[] = {
        {"RendererSystem", &RunRendererSystemSmoke},
        {"BasicForwardRenderer", &RunBasicForwardRendererSmoke},
        {"RendererSystemBasicForwardRenderer", &RunRendererSystemBasicForwardRendererSmoke},
        {"Time", &RunTimeSmoke},
        {"FileSystem", &RunFileSystemSmoke},
        {"Window", &RunWindowSmoke},
        {"Input", &RunInputSmoke},
        {"Jobs", &RunJobsSmoke},
        {"Audio", &RunAudioSmoke},
        {"AudioPlayback", &RunAudioPlaybackSmoke},
        {"CoreRuntime", &RunCoreRuntimeSmoke},
        {"DeterminismReplay", &RunDeterminismReplaySmoke},
        {"ArenaAllocator", &RunArenaAllocatorSmoke},
        {"FrameAllocator", &RunFrameAllocatorSmoke},
        {"StackAllocator", &RunStackAllocatorSmoke},
        {"SmallObjectAllocator", &RunSmallObjectAllocatorSmoke},
        {"LoggerOnly", &RunLoggerOnlySmoke},
        {"GuardAllocatorAlignment", &RunGuardAllocatorAlignmentSmoke},
        {"AllocatorAdapter", &RunAllocatorAdapterSmoke},
        {"FrameScope", &RunFrameScopeSmoke},
        {"MemorySystem", &RunMemorySystemSmoke},
        {"NewDelete", &RunNewDeleteSmoke},
        {"OOMPolicy", &RunOOMPolicySmoke},
        {"PageAllocator", &RunPageAllocatorSmoke},
        {"PoolAllocator", &RunPoolAllocatorSmoke},
        {"SmallObjectTLSBins", &RunSmallObjectTLSBinsSmoke},
        {"TrackingAllocator", &RunTrackingAllocatorSmoke},
    };

    int failures = 0;

    for (const SmokeEntry& entry : smokes)
    {
        const int code = entry.func ? entry.func() : 1;
        if (code != 0)
        {
            ++failures;
        }

        // Minimal reporting for gate diagnostics.
        std::printf("%s: %s (code=%d)\n", entry.name, (code == 0) ? "OK" : "FAIL", code);
    }

    return (failures == 0) ? 0 : 1;
}
