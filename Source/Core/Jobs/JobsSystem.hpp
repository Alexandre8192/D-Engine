// ============================================================================
// D-Engine - Source/Core/Jobs/JobsSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : High-level jobs system that owns a backend instance and exposes
//           unified submission and waiting helpers to the rest of the engine.
// Contract: Header-only, no exceptions/RTTI, no allocations in this layer.
//           Lifetime of the backend is tied to JobsSystemState.
// Notes   : Defaults to the NullJobs backend but accepts external backends via
//           JobsInterface injection.
// ============================================================================

#pragma once

#include "Core/Contracts/Jobs.hpp"
#include "Core/Jobs/NullJobs.hpp"

namespace dng::jobs
{
    enum class JobsSystemBackend : dng::u8
    {
        Null,
        External
    };

    struct JobsSystemConfig
    {
        JobsSystemBackend backend = JobsSystemBackend::Null;
    };

    struct JobsSystemState
    {
        JobsInterface     interface{};
        JobsSystemBackend backend       = JobsSystemBackend::Null;
        NullJobs          nullBackend{};
        bool              isInitialized = false;
    };

    [[nodiscard]] inline bool InitJobsSystemWithInterface(JobsSystemState& state,
                                                          JobsInterface interface,
                                                          JobsSystemBackend backend) noexcept
    {
        if (interface.userData == nullptr ||
            interface.vtable.submit == nullptr ||
            interface.vtable.submitBatch == nullptr ||
            interface.vtable.wait == nullptr)
        {
            return false;
        }

        state.interface     = interface;
        state.backend       = backend;
        state.isInitialized = true;
        return true;
    }

    [[nodiscard]] inline bool InitJobsSystem(JobsSystemState& state,
                                             const JobsSystemConfig& config) noexcept
    {
        switch (config.backend)
        {
            case JobsSystemBackend::Null:
            default:
            {
                JobsInterface iface = MakeNullJobsInterface(state.nullBackend);
                return InitJobsSystemWithInterface(state, iface, JobsSystemBackend::Null);
            }
            case JobsSystemBackend::External:
            {
                // External backends must be injected via InitJobsSystemWithInterface.
                return false;
            }
        }

        return false;
    }

    inline void ShutdownJobsSystem(JobsSystemState& state) noexcept
    {
        state.interface     = JobsInterface{};
        state.backend       = JobsSystemBackend::Null;
        state.nullBackend   = NullJobs{};
        state.isInitialized = false;
    }

    inline void SubmitJob(JobsSystemState& state, const JobDesc& job, JobCounter& counter) noexcept
    {
        if (!state.isInitialized)
        {
            return;
        }

        SubmitJob(state.interface, job, counter);
    }

    inline void SubmitJobs(JobsSystemState& state, const JobDesc* jobs, dng::u32 jobCount, JobCounter& counter) noexcept
    {
        if (!state.isInitialized)
        {
            return;
        }

        SubmitJobs(state.interface, jobs, jobCount, counter);
    }

    inline void ParallelFor(JobsSystemState& state, dng::u32 count, const ParallelForBody& body, JobCounter& counter) noexcept
    {
        if (!state.isInitialized)
        {
            return;
        }

        ParallelFor(state.interface, count, body, counter);
    }

    inline void WaitForCounter(JobsSystemState& state, JobCounter& counter) noexcept
    {
        if (!state.isInitialized)
        {
            return;
        }

        WaitForCounter(state.interface, counter);
    }

} // namespace dng::jobs
