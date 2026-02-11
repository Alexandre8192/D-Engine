// ============================================================================
// OOM Policy Smoke Test
// ----------------------------------------------------------------------------
// Ensures the global OOM helpers compile in isolation and validate runtime
// policy toggling semantics.
// ============================================================================

#if __has_include("Core/Memory/OOM.hpp")
#    include "Core/Memory/OOM.hpp"
#else
#    include "../../Source/Core/Memory/OOM.hpp"
#endif

int RunOOMPolicySmoke()
{
    const bool originalFatal = ::dng::core::ShouldFatalOnOOM();

    ::dng::core::SetFatalOnOOMPolicy(false);
    if (::dng::core::ShouldFatalOnOOM())
    {
        ::dng::core::SetFatalOnOOMPolicy(originalFatal);
        return 1;
    }
    if (!::dng::core::ShouldSurfaceBadAlloc())
    {
        ::dng::core::SetFatalOnOOMPolicy(originalFatal);
        return 2;
    }

    ::dng::core::SetFatalOnOOMPolicy(true);
    if (!::dng::core::ShouldFatalOnOOM())
    {
        ::dng::core::SetFatalOnOOMPolicy(originalFatal);
        return 3;
    }
    if (::dng::core::ShouldSurfaceBadAlloc())
    {
        ::dng::core::SetFatalOnOOMPolicy(originalFatal);
        return 4;
    }

    ::dng::core::SetFatalOnOOMPolicy(originalFatal);
    return 0;
}
