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

    const auto isReset = [](const JobsSystemState& state) noexcept
    {
        const JobsCaps caps = QueryCaps(state);
        return !state.isInitialized &&
               state.backend == JobsSystemBackend::Null &&
               caps.determinismMode == dng::DeterminismMode::Unknown &&
               caps.threadSafety == dng::ThreadSafetyMode::Unknown &&
               !caps.stableSubmissionOrder;
    };

    JobsSystemState uninitialized{};
    if (!isReset(uninitialized))
    {
        return 7;
    }

    JobsSystemConfig config{};

    NullJobs nullBackendForValidation{};
    JobsInterface brokenInterface = MakeNullJobsInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    JobsSystemState rejected{};
    if (!InitJobsSystem(rejected, config))
    {
        return 8;
    }
    if (InitJobsSystemWithInterface(rejected, brokenInterface))
    {
        return 9;
    }
    if (!isReset(rejected))
    {
        return 10;
    }

    JobsSystemConfig rejectedConfig{};
    rejectedConfig.backend = JobsSystemBackend::External;
    if (!InitJobsSystem(rejected, config))
    {
        return 11;
    }
    if (InitJobsSystem(rejected, rejectedConfig))
    {
        return 12;
    }
    if (!isReset(rejected))
    {
        return 13;
    }

    JobsSystemState state{};
    if (!InitJobsSystem(state, config))
    {
        return 1;
    }

    const NullJobs::Stats& initialStats = state.nullBackend.GetStats();
    if (initialStats.submitCalls != 0U ||
        initialStats.submitBatchCalls != 0U ||
        initialStats.parallelForCalls != 0U ||
        initialStats.jobsExecuted != 0U)
    {
        return 9;
    }

    const JobsCaps caps = QueryCaps(state);
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

    const NullJobs::Stats& afterSubmitStats = state.nullBackend.GetStats();
    if (afterSubmitStats.submitCalls != 1U ||
        afterSubmitStats.submitBatchCalls != 0U ||
        afterSubmitStats.parallelForCalls != 0U ||
        afterSubmitStats.jobsExecuted != 1U)
    {
        return 10;
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

    const NullJobs::Stats& afterBatchStats = state.nullBackend.GetStats();
    if (afterBatchStats.submitCalls != 1U ||
        afterBatchStats.submitBatchCalls != 1U ||
        afterBatchStats.parallelForCalls != 0U ||
        afterBatchStats.jobsExecuted != 4U)
    {
        return 11;
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

    const NullJobs::Stats& afterParallelForStats = state.nullBackend.GetStats();
    if (afterParallelForStats.submitCalls != 1U ||
        afterParallelForStats.submitBatchCalls != 1U ||
        afterParallelForStats.parallelForCalls != 1U ||
        afterParallelForStats.jobsExecuted != 8U)
    {
        return 12;
    }

    WaitForCounter(state, jobCounter);
    WaitForCounter(state, batchCounter);
    WaitForCounter(state, pfCounter);

    state.nullBackend.ResetStats();
    const NullJobs::Stats& resetStats = state.nullBackend.GetStats();
    if (resetStats.submitCalls != 0U ||
        resetStats.submitBatchCalls != 0U ||
        resetStats.parallelForCalls != 0U ||
        resetStats.jobsExecuted != 0U)
    {
        return 13;
    }

    ShutdownJobsSystem(state);
    return 0;
}
