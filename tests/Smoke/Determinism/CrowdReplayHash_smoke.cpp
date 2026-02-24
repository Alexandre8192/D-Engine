// ============================================================================
// D-Engine - tests/Smoke/Determinism/CrowdReplayHash_smoke.cpp
// ----------------------------------------------------------------------------
// Purpose : Validate deterministic replay behavior of the CPU crowd simulation
//           and ensure the jobs-shaped step matches the single-thread step.
// Contract: No exceptions/RTTI, no dynamic allocation in the simulation loop,
//           and runtime budget below one second on typical dev hardware.
// Notes   : Uses static caller-owned SoA buffers with N=2000 and 256 ticks.
// ============================================================================

#include "Core/Jobs/NullJobs.hpp"
#include "Core/Simulation/CrowdSim.hpp"
#include "Core/Simulation/CrowdSimJobs.hpp"

namespace
{
    constexpr dng::u32 kAgentCount = 2000u;
    constexpr dng::u32 kTickCount  = 256u;

    dng::u64 RunCrowdPassSingle(const dng::sim::CrowdParams& params) noexcept
    {
        static dng::i32 posX[kAgentCount];
        static dng::i32 posY[kAgentCount];
        static dng::i32 velX[kAgentCount];
        static dng::i32 velY[kAgentCount];
        static dng::u32 rng[kAgentCount];

        dng::sim::CrowdSoAView view{};
        view.posX  = posX;
        view.posY  = posY;
        view.velX  = velX;
        view.velY  = velY;
        view.rng   = rng;
        view.count = kAgentCount;

        dng::sim::InitCrowd(view, params);
        for (dng::u32 tick = 0; tick < kTickCount; ++tick)
        {
            dng::sim::StepCrowd(view, params, tick);
        }

        return dng::sim::HashCrowd(view);
    }

    dng::u64 RunCrowdPassJobs(const dng::sim::CrowdParams& params) noexcept
    {
        static dng::i32 posX[kAgentCount];
        static dng::i32 posY[kAgentCount];
        static dng::i32 velX[kAgentCount];
        static dng::i32 velY[kAgentCount];
        static dng::u32 rng[kAgentCount];
        static dng::i32 scratchAccX[kAgentCount];
        static dng::i32 scratchAccY[kAgentCount];

        dng::sim::CrowdSoAView view{};
        view.posX  = posX;
        view.posY  = posY;
        view.velX  = velX;
        view.velY  = velY;
        view.rng   = rng;
        view.count = kAgentCount;

        dng::sim::InitCrowd(view, params);

        dng::jobs::NullJobs nullJobs{};
        dng::jobs::JobsInterface jobs = dng::jobs::MakeNullJobsInterface(nullJobs);
        for (dng::u32 tick = 0; tick < kTickCount; ++tick)
        {
            dng::sim::StepCrowdJobs(jobs, view, params, tick, scratchAccX, scratchAccY);
        }

        return dng::sim::HashCrowd(view);
    }
} // namespace

int RunCrowdDeterminismSmoke()
{
    dng::sim::CrowdParams params{};
    params.worldMinX = -4096;
    params.worldMaxX = 4096;
    params.worldMinY = -4096;
    params.worldMaxY = 4096;
    params.maxSpeed  = 7;
    params.seed      = 0x5a17b3cdu;

    const dng::u64 firstSingle  = RunCrowdPassSingle(params);
    const dng::u64 secondSingle = RunCrowdPassSingle(params);
    if (firstSingle != secondSingle)
    {
        return 1;
    }

    const dng::u64 firstJobs  = RunCrowdPassJobs(params);
    const dng::u64 secondJobs = RunCrowdPassJobs(params);
    if (firstJobs != secondJobs)
    {
        return 2;
    }

    return (firstSingle == firstJobs) ? 0 : 3;
}
