// ============================================================================
// D-Engine - Source/Core/Jobs/NullJobs.hpp
// ----------------------------------------------------------------------------
// Purpose : Deterministic jobs backend that satisfies the jobs contract while
//           executing work immediately on the calling thread.
// Contract: Header-only, no exceptions/RTTI, no allocations. All methods are
//           noexcept and deterministic.
// Notes   : Useful for tests and CI; tracks simple stats for observability.
// ============================================================================

#pragma once

#include "Core/Contracts/Jobs.hpp"

namespace dng::jobs
{
    struct NullJobs
    {
        struct Stats
        {
            dng::u32 submitCalls         = 0;
            dng::u32 submitBatchCalls    = 0;
            dng::u32 parallelForCalls    = 0;
            dng::u32 jobsExecuted        = 0;
        } stats{};

        [[nodiscard]] constexpr JobsCaps GetCaps() const noexcept
        {
            JobsCaps caps{};
            caps.deterministic = true;
            caps.multithreaded = false;
            caps.determinismMode = dng::DeterminismMode::Replay;
            caps.threadSafety = dng::ThreadSafetyMode::ExternalSync;
            caps.stableSubmissionOrder = true;
            return caps;
        }

        void Submit(const JobDesc& job, JobCounter& counter) noexcept
        {
            ++stats.submitCalls;
            counter.value += 1U;
            if (job.func)
            {
                job.func(job.userData);
                ++stats.jobsExecuted;
            }
            counter.value = 0;
        }

        void SubmitBatch(const JobDesc* jobs, dng::u32 jobCount, JobCounter& counter) noexcept
        {
            ++stats.submitBatchCalls;
            counter.value += jobCount;
            for (dng::u32 i = 0; i < jobCount; ++i)
            {
                if (jobs && jobs[i].func)
                {
                    jobs[i].func(jobs[i].userData);
                    ++stats.jobsExecuted;
                }
            }
            counter.value = 0;
        }

        void Wait(JobCounter& counter) noexcept
        {
            (void)counter; // Nothing to do; all work executes inline.
        }

        void ParallelFor(dng::u32 count, const ParallelForBody& body, JobCounter& counter) noexcept
        {
            ++stats.parallelForCalls;
            counter.value += count;
            for (dng::u32 i = 0; i < count; ++i)
            {
                if (body.func)
                {
                    body.func(body.userData, i);
                    ++stats.jobsExecuted;
                }
            }
            counter.value = 0;
        }

        [[nodiscard]] constexpr const Stats& GetStats() const noexcept
        {
            return stats;
        }
    };

    static_assert(JobsBackend<NullJobs>, "NullJobs must satisfy jobs backend concept.");

    [[nodiscard]] inline JobsInterface MakeNullJobsInterface(NullJobs& backend) noexcept
    {
        return MakeJobsInterface(backend);
    }

} // namespace dng::jobs
