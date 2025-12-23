#include "Core/Contracts/Jobs.hpp"

using namespace dng::jobs;

namespace
{
    struct DummyJobs
    {
        [[nodiscard]] constexpr JobsCaps GetCaps() const noexcept
        {
            JobsCaps caps{};
            caps.deterministic = true;
            caps.multithreaded = false;
            return caps;
        }

        void Submit(const JobDesc&, JobCounter&) noexcept {}
        void SubmitBatch(const JobDesc*, dng::u32, JobCounter&) noexcept {}
        void Wait(JobCounter&) noexcept {}
        void ParallelFor(dng::u32, const ParallelForBody&, JobCounter&) noexcept {}
    };
}

static_assert(std::is_trivially_copyable_v<JobHandle>);
static_assert(std::is_trivially_copyable_v<JobCounter>);
static_assert(std::is_trivially_copyable_v<JobDesc>);
static_assert(std::is_trivially_copyable_v<ParallelForBody>);
static_assert(JobsBackend<DummyJobs>);
