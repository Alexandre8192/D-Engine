// ============================================================================
// D-Engine - Source/Core/Contracts/Jobs.hpp
// ----------------------------------------------------------------------------
// Purpose : Jobs contract describing backend-agnostic job submission and
//           synchronization so multiple job systems can plug into Core without
//           leaking implementation details.
// Contract: Header-only, no exceptions/RTTI, engine-absolute includes only.
//           All types are POD or trivially copyable; no allocations occur in
//           this layer. Thread-safety is left to the backend; callers must
//           externally synchronize per backend instance.
// Notes   : M0 focuses on single-threaded determinism; backends may execute
//           inline or asynchronously but must honor the contract signatures.
// ============================================================================

#pragma once

#include "Core/Types.hpp"

#include <concepts>
#include <type_traits>

namespace dng::jobs
{
    using JobValue = dng::u32;

    // ------------------------------------------------------------------------
    // Public POD handles and counters
    // ------------------------------------------------------------------------

    struct JobHandle
    {
        JobValue value = 0;

        constexpr JobHandle() noexcept = default;
        explicit constexpr JobHandle(JobValue raw) noexcept : value(raw) {}

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] static constexpr JobHandle Invalid() noexcept { return JobHandle{}; }
    };

    struct JobCounter
    {
        JobValue value = 0;

        [[nodiscard]] constexpr bool IsComplete() const noexcept { return value == 0; }
        [[nodiscard]] static constexpr JobCounter Zero() noexcept { return JobCounter{}; }
    };

    static_assert(std::is_trivially_copyable_v<JobHandle>);
    static_assert(std::is_trivially_copyable_v<JobCounter>);

    struct JobsCaps
    {
        bool deterministic = false;
        bool multithreaded = false;
        dng::DeterminismMode determinismMode = dng::DeterminismMode::Unknown;
        dng::ThreadSafetyMode threadSafety = dng::ThreadSafetyMode::Unknown;
        bool stableSubmissionOrder = false;
    };

    static_assert(std::is_trivially_copyable_v<JobsCaps>);

    // ------------------------------------------------------------------------
    // Job descriptors
    // ------------------------------------------------------------------------

    struct JobDesc
    {
        using JobFunc = void(*)(void* userData) noexcept;

        JobFunc func     = nullptr;
        void*   userData = nullptr;
    };

    static_assert(std::is_trivially_copyable_v<JobDesc>);

    struct ParallelForBody
    {
        using ForFunc = void(*)(void* userData, JobValue index) noexcept;

        ForFunc func     = nullptr;
        void*   userData = nullptr;
    };

    static_assert(std::is_trivially_copyable_v<ParallelForBody>);

    // ------------------------------------------------------------------------
    // Dynamic face (tiny v-table for late binding)
    // ------------------------------------------------------------------------

    struct JobsVTable
    {
        using GetCapsFunc        = JobsCaps(*)(const void* userData) noexcept;
        using SubmitFunc         = void(*)(void* userData, const JobDesc& job, JobCounter& counter) noexcept;
        using SubmitBatchFunc    = void(*)(void* userData, const JobDesc* jobs, dng::u32 jobCount, JobCounter& counter) noexcept;
        using WaitFunc           = void(*)(void* userData, JobCounter& counter) noexcept;
        using ParallelForFunc    = void(*)(void* userData, dng::u32 count, const ParallelForBody& body, JobCounter& counter) noexcept;

        GetCapsFunc     getCaps      = nullptr;
        SubmitFunc      submit       = nullptr;
        SubmitBatchFunc submitBatch  = nullptr;
        WaitFunc        wait         = nullptr;
        ParallelForFunc parallelFor  = nullptr;
    };

    struct JobsInterface
    {
        JobsVTable vtable{};
        void*      userData = nullptr; // Non-owning backend instance pointer.
    };

    [[nodiscard]] inline JobsCaps QueryCaps(const JobsInterface& jobs) noexcept
    {
        return (jobs.vtable.getCaps && jobs.userData)
            ? jobs.vtable.getCaps(jobs.userData)
            : JobsCaps{};
    }

    inline void SubmitJob(JobsInterface& jobs, const JobDesc& job, JobCounter& counter) noexcept
    {
        if (jobs.vtable.submit && jobs.userData)
        {
            jobs.vtable.submit(jobs.userData, job, counter);
        }
    }

    inline void SubmitJobs(JobsInterface& jobs, const JobDesc* jobArray, dng::u32 jobCount, JobCounter& counter) noexcept
    {
        if (jobs.vtable.submitBatch && jobs.userData)
        {
            jobs.vtable.submitBatch(jobs.userData, jobArray, jobCount, counter);
        }
    }

    inline void WaitForCounter(JobsInterface& jobs, JobCounter& counter) noexcept
    {
        if (jobs.vtable.wait && jobs.userData)
        {
            jobs.vtable.wait(jobs.userData, counter);
        }
    }

    inline void ParallelFor(JobsInterface& jobs, dng::u32 count, const ParallelForBody& body, JobCounter& counter) noexcept
    {
        if (jobs.vtable.parallelFor && jobs.userData)
        {
            jobs.vtable.parallelFor(jobs.userData, count, body, counter);
        }
        else
        {
            // Fallback: execute sequentially when the backend has no ParallelFor entry.
            for (dng::u32 i = 0; i < count; ++i)
            {
                if (body.func)
                {
                    body.func(body.userData, i);
                }
            }
            counter.value = 0;
        }
    }

    // ------------------------------------------------------------------------
    // Static face (concept + adapter to dynamic v-table)
    // ------------------------------------------------------------------------

    template <typename Backend>
    concept JobsBackend = requires(Backend& backend, JobCounter& counter, const JobDesc& job, const JobDesc* jobs, dng::u32 jobCount, const ParallelForBody& body, const Backend& constBackend)
    {
        { constBackend.GetCaps() } noexcept -> std::same_as<JobsCaps>;
        { backend.Submit(job, counter) } noexcept -> std::same_as<void>;
        { backend.SubmitBatch(jobs, jobCount, counter) } noexcept -> std::same_as<void>;
        { backend.Wait(counter) } noexcept -> std::same_as<void>;
        { backend.ParallelFor(jobCount, body, counter) } noexcept -> std::same_as<void>;
    };

    namespace detail
    {
        template <typename Backend>
        struct JobsInterfaceAdapter
        {
            static JobsCaps GetCaps(const void* userData) noexcept
            {
                return static_cast<const Backend*>(userData)->GetCaps();
            }

            static void Submit(void* userData, const JobDesc& job, JobCounter& counter) noexcept
            {
                static_cast<Backend*>(userData)->Submit(job, counter);
            }

            static void SubmitBatch(void* userData, const JobDesc* jobs, dng::u32 jobCount, JobCounter& counter) noexcept
            {
                static_cast<Backend*>(userData)->SubmitBatch(jobs, jobCount, counter);
            }

            static void Wait(void* userData, JobCounter& counter) noexcept
            {
                static_cast<Backend*>(userData)->Wait(counter);
            }

            static void ParallelFor(void* userData, dng::u32 count, const ParallelForBody& body, JobCounter& counter) noexcept
            {
                static_cast<Backend*>(userData)->ParallelFor(count, body, counter);
            }
        };
    } // namespace detail

    template <typename Backend>
    [[nodiscard]] inline JobsInterface MakeJobsInterface(Backend& backend) noexcept
    {
        static_assert(JobsBackend<Backend>, "Backend must satisfy JobsBackend concept.");

        JobsInterface iface{};
        iface.userData            = &backend;
        iface.vtable.getCaps      = &detail::JobsInterfaceAdapter<Backend>::GetCaps;
        iface.vtable.submit       = &detail::JobsInterfaceAdapter<Backend>::Submit;
        iface.vtable.submitBatch  = &detail::JobsInterfaceAdapter<Backend>::SubmitBatch;
        iface.vtable.wait         = &detail::JobsInterfaceAdapter<Backend>::Wait;
        iface.vtable.parallelFor  = &detail::JobsInterfaceAdapter<Backend>::ParallelFor;
        return iface;
    }

} // namespace dng::jobs
