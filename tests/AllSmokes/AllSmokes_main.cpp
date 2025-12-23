// ============================================================================
// D-Engine - tests/AllSmokes/AllSmokes_main.cpp
// ----------------------------------------------------------------------------
// Purpose : Aggregate executable that runs all smoke helpers.
// Contract: No exceptions/RTTI; deterministic ordering; returns 0 on success.
// Notes   : Assumes Run*Smoke helpers are linked from their respective TUs.
// ============================================================================

int RunRendererSystemSmoke();
int RunBasicForwardRendererSmoke();
int RunTimeSmoke();
int RunFileSystemSmoke();
int RunWindowSmoke();
int RunInputSmoke();
int RunJobsSmoke();

int main()
{
    int failures = 0;

    // Each smoke returns 0 on pass, non-zero on failure.
    failures += (RunRendererSystemSmoke() != 0) ? 1 : 0;
    failures += (RunBasicForwardRendererSmoke() != 0) ? 1 : 0;
    failures += (RunTimeSmoke() != 0) ? 1 : 0;
    failures += (RunFileSystemSmoke() != 0) ? 1 : 0;
    failures += (RunWindowSmoke() != 0) ? 1 : 0;
    failures += (RunInputSmoke() != 0) ? 1 : 0;
    failures += (RunJobsSmoke() != 0) ? 1 : 0;

    return (failures == 0) ? 0 : 1;
}
