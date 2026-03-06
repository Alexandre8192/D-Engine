// ============================================================================
// D-Engine - Source/Core/Platform/PlatformProcess.hpp
// ----------------------------------------------------------------------------
// Purpose : Expose small runtime process/platform queries through the platform
//           layer instead of letting tools include raw OS APIs directly.
// Contract: Header-only, no allocations, no exceptions/RTTI.
// Notes   : Extend conservatively; this is for shared low-level process data,
//           not for general application logic.
// ============================================================================

#pragma once

#include "PlatformDefines.hpp"

#include <cstdint>

#if DNG_PLATFORM_WINDOWS
    #include "WindowsApi.hpp"
#endif

namespace dng::platform
{
    struct ProcessRuntimeInfo
    {
        bool          isAvailable = false;
        std::uint32_t logicalCpuCount = 0;
        std::uint64_t processAffinityMask = 0;
        const char*   priorityClassName = "UNKNOWN";
    };

    [[nodiscard]] inline ProcessRuntimeInfo QueryCurrentProcessRuntimeInfo() noexcept
    {
        ProcessRuntimeInfo info{};

#if DNG_PLATFORM_WINDOWS
        const HANDLE process = ::GetCurrentProcess();
        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask = 0;

        info.isAvailable = true;
        info.processAffinityMask =
            ::GetProcessAffinityMask(process, &processMask, &systemMask)
                ? static_cast<std::uint64_t>(processMask)
                : 0ull;
        info.logicalCpuCount =
            static_cast<std::uint32_t>(::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));

        switch (::GetPriorityClass(process))
        {
            case IDLE_PRIORITY_CLASS:         info.priorityClassName = "IDLE"; break;
            case BELOW_NORMAL_PRIORITY_CLASS: info.priorityClassName = "BELOW_NORMAL"; break;
            case NORMAL_PRIORITY_CLASS:       info.priorityClassName = "NORMAL"; break;
            case ABOVE_NORMAL_PRIORITY_CLASS: info.priorityClassName = "ABOVE_NORMAL"; break;
            case HIGH_PRIORITY_CLASS:         info.priorityClassName = "HIGH"; break;
            case REALTIME_PRIORITY_CLASS:     info.priorityClassName = "REALTIME"; break;
            default:                          info.priorityClassName = "UNKNOWN"; break;
        }
#endif

        return info;
    }
} // namespace dng::platform
