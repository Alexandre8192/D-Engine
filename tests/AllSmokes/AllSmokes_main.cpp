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
int RunTimeSmoke();
int RunFileSystemSmoke();
int RunWindowSmoke();
int RunInputSmoke();
int RunJobsSmoke();
int RunAudioSmoke();

int main()
{
    int failures = 0;

    const auto runSmoke = [&failures](const char* name, int (*fn)()) noexcept -> void {
        const int rc = fn();
        if (rc != 0)
        {
            ++failures;
            std::printf("%s FAILED (%d)\n", name, rc);
        }
        else
        {
            std::printf("%s OK\n", name);
        }
    };

    // Each smoke returns 0 on pass, non-zero on failure.
    runSmoke("RendererSystem", RunRendererSystemSmoke);
    runSmoke("BasicForwardRenderer", RunBasicForwardRendererSmoke);
    runSmoke("Time", RunTimeSmoke);
    runSmoke("FileSystem", RunFileSystemSmoke);
    runSmoke("Window", RunWindowSmoke);
    runSmoke("Input", RunInputSmoke);
    runSmoke("Jobs", RunJobsSmoke);
    runSmoke("Audio", RunAudioSmoke);

    return (failures == 0) ? 0 : 1;
}
