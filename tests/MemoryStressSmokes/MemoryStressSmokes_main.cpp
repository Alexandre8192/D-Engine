// ============================================================================
// D-Engine - tests/MemoryStressSmokes/MemoryStressSmokes_main.cpp
// ----------------------------------------------------------------------------
// Purpose : Aggregate executable for extended/noisy memory stress smokes.
// Contract: No exceptions/RTTI; deterministic ordering; returns 0 on success.
// Notes   : Keeps long-running allocator stress tests separate from AllSmokes.
// ============================================================================

#include <cstdio>

int RunSmallObjectThreadStressSmoke();
int RunSmallObjectFragmentationLongRunSmoke();
int RunMemoryOOMAlignmentExtremesSmoke();

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
        {"SmallObjectThreadStress", &RunSmallObjectThreadStressSmoke},
        {"SmallObjectFragmentationLongRun", &RunSmallObjectFragmentationLongRunSmoke},
        {"MemoryOOMAlignmentExtremes", &RunMemoryOOMAlignmentExtremesSmoke},
    };

    int failures = 0;

    for (const SmokeEntry& entry : smokes)
    {
        const int code = entry.func ? entry.func() : 1;
        if (code != 0)
        {
            ++failures;
        }

        std::printf("%s: %s (code=%d)\n", entry.name, (code == 0) ? "OK" : "FAIL", code);
    }

    return (failures == 0) ? 0 : 1;
}
