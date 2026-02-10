#include "Core/Jobs/JobsSystem.hpp"

namespace
{
    struct CounterJobData
    {
        int* value = nullptr;
    };

    void IncrementJob(void* userData) noexcept
    {
        auto* data = static_cast<CounterJobData*>(userData);
        if (data && data->value)
        {
            ++(*data->value);
        }
    }

    struct ParallelForData
    {
        dng::u32* sum = nullptr;
    };

    void ParallelForJob(void* userData, dng::u32 index) noexcept
    {
        auto* data = static_cast<ParallelForData*>(userData);
        if (data && data->sum)
        {
            *data->sum += (index + 1U);
        }
    }
}

int RunJobsSmoke()
{
    using namespace dng::jobs;

    JobsSystemState uninitialized{};
    const JobsCaps uninitCaps = QueryCaps(uninitialized);
    if (uninitCaps.determinismMode != dng::DeterminismMode::Unknown ||
        uninitCaps.threadSafety != dng::ThreadSafetyMode::Unknown ||
        uninitCaps.stableSubmissionOrder)
    {
        return 7;
    }

    NullJobs nullBackendForValidation{};
    JobsInterface brokenInterface = MakeNullJobsInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    JobsSystemState rejected{};
    if (InitJobsSystemWithInterface(rejected, brokenInterface, JobsSystemBackend::External))
    {
        return 8;
    }

    JobsSystemState state{};
    JobsSystemConfig config{};

    if (!InitJobsSystem(state, config))
    {
        return 1;
    }

    const JobsCaps caps = QueryCaps(state.interface);
    if (!caps.deterministic ||
        caps.multithreaded ||
        caps.determinismMode != dng::DeterminismMode::Replay ||
        caps.threadSafety != dng::ThreadSafetyMode::ExternalSync ||
        !caps.stableSubmissionOrder)
    {
        return 6;
    }


    int counter = 0;
    CounterJobData jobData{&counter};
    JobDesc job{};
    job.func     = &IncrementJob;
    job.userData = &jobData;

    JobCounter jobCounter{};
    SubmitJob(state, job, jobCounter);
    if (!jobCounter.IsComplete() || counter != 1)
    {
        return 2;
    }

    JobDesc batch[3];
    for (dng::u32 i = 0; i < 3; ++i)
    {
        batch[i] = job;
    }

    JobCounter batchCounter{};
    SubmitJobs(state, batch, 3, batchCounter);
    if (!batchCounter.IsComplete() || counter != 4)
    {
        return 3;
    }

    dng::u32 parallelSum = 0;
    ParallelForData pfData{&parallelSum};
    ParallelForBody body{};
    body.func     = &ParallelForJob;
    body.userData = &pfData;

    JobCounter pfCounter{};
    ParallelFor(state, 4U, body, pfCounter);
    if (!pfCounter.IsComplete() || parallelSum != 10U)
    {
        return 4;
    }

    WaitForCounter(state, jobCounter);
    WaitForCounter(state, batchCounter);
    WaitForCounter(state, pfCounter);

    ShutdownJobsSystem(state);
    return 0;
}
