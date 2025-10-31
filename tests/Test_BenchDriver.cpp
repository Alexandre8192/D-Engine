#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
/*
===============================================================================
D-Engine - tests/Test_BenchDriver.cpp  (BenchDriver v2)
Purpose :
    Exercise Core/Diagnostics/Bench.hpp with representative allocator-centric
    scenarios to validate timing, warm-up, auto-scaling, and monotonic churn
    reporting.
Contract :
    - Single-threaded console driver; each benchmark body is a `noexcept`
      lambda.
    - MemorySystem::Init() and Shutdown() are invoked exactly once.
    - Release|x64 provides stable timings; Debug builds are for smoke only.
Notes :
    - When TrackingAllocator is unavailable, BenchResult prints "<tracking-off>".
    - Iteration hints remain adjustable, but the harness now scales toward
      ~250 ms total per scenario automatically.
===============================================================================
*/

#include "Core/CoreMinimal.hpp"            // Platform glue, logging, types
#include "Core/Diagnostics/Bench.hpp"      // dng::bench::{DNG_BENCH, BenchResult, ToString}
#include "Core/Containers/StdAliases.hpp"  // dng::vector alias (AllocatorAdapter)
#include "Core/Memory/MemorySystem.hpp"    // MemorySystem lifecycle façade
#include "Core/Memory/FrameScope.hpp"      // FrameScope burst benchmark
#include "Core/Memory/ArenaAllocator.hpp"  // dng::core::ArenaAllocator contract
#include "Core/Memory/SmallObjectAllocator.hpp" // dng::core::SmallObjectAllocator (for batch sweep)

#include <cstddef>      // std::max_align_t, std::size_t
#include <cstdint>      // std::uint32_t, std::uint64_t
#include <type_traits>  // std::true_type, std::false_type
#include <vector>       // std::vector for tracking_vector alias
#include <string_view>  // std::string_view
#include <string>       // std::string
#include <ctime>        // std::time_t, std::tm, std::gmtime
#include <cstdio>       // std::FILE, std::fopen, std::fprintf, std::fclose
#include <cstdlib>      // std::getenv, std::strtol
#include <array>        // std::array for processor enumeration
#include <algorithm>    // std::sort
#include <cmath>        // std::sqrt, std::abs
#include <atomic>       // std::atomic for LocalBarrier
#include <thread>       // std::thread::hardware_concurrency
#include <cerrno>       // errno
#include <memory>       // std::unique_ptr for telemetry queries
#include <new>          // std::nothrow

#if defined(_WIN32)
#   ifndef NOMINMAX
#       define NOMINMAX 1
#   endif
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN 1
#   endif
#   include <windows.h>
#   include <winternl.h>
#   include <intrin.h>
#else
#   include <sched.h>
#   include <sys/resource.h>
#   include <unistd.h>
#endif

#ifdef __cpp_lib_print
    #include <print>
#else
    #include <iostream>
#endif

#include <cstring>     // std::strlen
#include <limits>      // std::numeric_limits for saturation helpers

#ifndef THREAD_PRIORITY_NORMAL
#define THREAD_PRIORITY_NORMAL 0
#endif


// --- Build banner to confirm the right binary is running ---------------------
#ifndef NDEBUG
    #define DNG_BENCH_BUILD_MODE "Debug"
#else
    #define DNG_BENCH_BUILD_MODE "Release"
#endif

namespace
{
    // Helper macro for compile-time stringification.
#define DNG_STRINGIZE_IMPL(x) #x
#define DNG_STRINGIZE(x) DNG_STRINGIZE_IMPL(x)

    [[nodiscard]] constexpr std::uint64_t Fnv1a64Update(std::uint64_t hash, const char* data, std::size_t length) noexcept
    {
        constexpr std::uint64_t kPrime = 1099511628211ull;
        for (std::size_t i = 0; i < length; ++i)
        {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(data[i]));
            hash *= kPrime;
        }
        return hash;
    }

    [[nodiscard]] constexpr std::uint64_t Fnv1a64(const char* data, std::size_t length) noexcept
    {
        constexpr std::uint64_t kOffset = 1469598103934665603ull;
        return Fnv1a64Update(kOffset, data, length);
    }

    [[nodiscard]] inline std::uint64_t HashScenarioKey(std::string_view name, unsigned threads) noexcept
    {
        std::uint64_t hash = Fnv1a64(name.data(), name.size());
        const char sep = '#';
        hash = Fnv1a64Update(hash, &sep, 1u);

        char digits[16]{};
        std::size_t digitCount = 0;
        unsigned value = threads;
        do
        {
            digits[digitCount++] = static_cast<char>('0' + (value % 10u));
            value /= 10u;
        } while (value != 0u && digitCount < sizeof(digits));

        for (std::size_t i = 0; i < digitCount / 2; ++i)
        {
            const char tmp = digits[i];
            digits[i] = digits[digitCount - 1u - i];
            digits[digitCount - 1u - i] = tmp;
        }

        return Fnv1a64Update(hash, digits, digitCount);
    }

    inline void WriteJsonEscaped(std::FILE* file, std::string_view value) noexcept
    {
        if (!file)
            return;

        std::fputc('"', file);
        for (char c : value)
        {
            switch (c)
            {
            case '\\': std::fputs("\\\\", file); break;
            case '"':  std::fputs("\\\"", file); break;
            case '\n': std::fputs("\\n", file); break;
            case '\r': std::fputs("\\r", file); break;
            case '\t': std::fputs("\\t", file); break;
            default:
                if (static_cast<unsigned char>(c) < 0x20u)
                {
                    std::fprintf(file, "\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(c)));
                }
                else
                {
                    std::fputc(c, file);
                }
                break;
            }
        }
        std::fputc('"', file);
    }

    inline void WriteJsonEscaped(std::FILE* file, const char* value) noexcept
    {
        if (!value)
        {
            WriteJsonEscaped(file, std::string_view{});
        }
        else
        {
            WriteJsonEscaped(file, std::string_view{value});
        }
    }

    struct ThreadTelemetry
    {
        unsigned Index = 0;
        unsigned Processor = 0;
        unsigned long long AffinityMask = 0;
        int Priority = THREAD_PRIORITY_NORMAL;
    };

    inline void PrintLine(std::string_view s) noexcept
    {
    #ifdef __cpp_lib_print
        std::print("{}\n", s);
    #else
        std::cout << s << '\n';
    #endif
    }

    inline void PrintNote(const char* message) noexcept
    {
        if (!message)
            return;
    #ifdef __cpp_lib_print
        std::print("[Note] {}\n", message);
    #else
        std::cout << "[Note] " << message << '\n';
    #endif
    }

    inline void PrintBenchLine(const ::dng::bench::BenchResult& result) noexcept
    {
    #ifdef __cpp_lib_print
        std::print("{}\n", ::dng::bench::ToString(result));
    #else
        std::cout << ::dng::bench::ToString(result) << '\n';
    #endif
    }

    struct Stats
    {
        double Min   = 0.0;
        double Max   = 0.0;
        double Median = 0.0;
        double Mean   = 0.0;
        double StdDev = 0.0;
        double RsdPct = 0.0;
        double Mad    = 0.0;
    };

    [[nodiscard]] inline Stats ComputeStats(const double* samples, std::size_t count) noexcept
    {
        Stats stats{};
        if (!samples || count == 0u)
            return stats;

        double minValue = samples[0];
        double maxValue = samples[0];
        double sum = 0.0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const double sample = samples[i];
            if (sample < minValue) minValue = sample;
            if (sample > maxValue) maxValue = sample;
            sum += sample;
        }

        stats.Min = minValue;
        stats.Max = maxValue;
        stats.Mean = sum / static_cast<double>(count);

        std::vector<double> sorted(samples, samples + count);
        std::sort(sorted.begin(), sorted.end());
        const std::size_t mid = sorted.size() / 2;
        if ((sorted.size() % 2u) == 1u)
        {
            stats.Median = sorted[mid];
        }
        else
        {
            stats.Median = (sorted[mid - 1] + sorted[mid]) * 0.5;
        }

        std::vector<double> deviations;
        deviations.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            deviations.push_back(std::abs(samples[i] - stats.Median));
        }
        std::sort(deviations.begin(), deviations.end());
        const std::size_t devMid = deviations.size() / 2;
        if (!deviations.empty())
        {
            if ((deviations.size() % 2u) == 1u)
            {
                stats.Mad = deviations[devMid];
            }
            else
            {
                stats.Mad = (deviations[devMid - 1] + deviations[devMid]) * 0.5;
            }
        }

        double variance = 0.0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const double sample = samples[i];
            const double delta = sample - stats.Mean;
            variance += delta * delta;
        }
        variance /= static_cast<double>(count);
        stats.StdDev = std::sqrt(variance);
        stats.RsdPct = (stats.Mean != 0.0) ? ((stats.StdDev / stats.Mean) * 100.0) : 0.0;
        return stats;
    }

    struct MachineTelemetry
    {
        char CpuVendor[32]{};
        char CpuName[128]{};
        unsigned PhysicalCores = 0;
        unsigned LogicalCores = 0;
        unsigned AvailableCores = 0;
        unsigned BaseMHz = 0;
        unsigned BoostMHz = 0;
        char OsVersion[64]{};
    };

    struct BuildTelemetry
    {
        char BuildType[16]{};
        char CompilerVersion[32]{};
        char OptFlags[64]{};
        char Lto[8]{};
        char Arch[16]{};
        bool DirtyTree = false;
    };

    MachineTelemetry gMachineTelemetry{};
    BuildTelemetry gBuildTelemetry{};

#if defined(_WIN32)
    constexpr unsigned kMaxLogicalCpuBits = static_cast<unsigned>(sizeof(DWORD_PTR) * 8u);

    struct ProcessTelemetry
    {
        DWORD_PTR ProcessMask = 0;
        DWORD_PTR SystemMask = 0;
        unsigned CpuCount = 0;
        unsigned CpuIndices[kMaxLogicalCpuBits]{};
        unsigned long long MainThreadMask = 0;
        int MainThreadPriority = THREAD_PRIORITY_NORMAL;
        char PowerPlan[128]{};
        char ProcessPriority[32]{};
        char TimerKind[32]{};
    };

    ProcessTelemetry gProcessTelemetry{};

    [[nodiscard]] inline const char* PriorityClassToString(DWORD priorityClass) noexcept
    {
        switch (priorityClass)
        {
        case IDLE_PRIORITY_CLASS:         return "IDLE";
        case BELOW_NORMAL_PRIORITY_CLASS: return "BELOW_NORMAL";
        case NORMAL_PRIORITY_CLASS:       return "NORMAL";
        case ABOVE_NORMAL_PRIORITY_CLASS: return "ABOVE_NORMAL";
        case HIGH_PRIORITY_CLASS:         return "HIGH";
        case REALTIME_PRIORITY_CLASS:     return "REALTIME";
        default:                          return "UNKNOWN";
        }
    }

    [[nodiscard]] inline const char* ThreadPriorityToString(int priority) noexcept
    {
        switch (priority)
        {
        case THREAD_PRIORITY_IDLE:          return "IDLE";
        case THREAD_PRIORITY_LOWEST:        return "LOWEST";
        case THREAD_PRIORITY_BELOW_NORMAL:  return "BELOW_NORMAL";
        case THREAD_PRIORITY_NORMAL:        return "NORMAL";
        case THREAD_PRIORITY_ABOVE_NORMAL:  return "ABOVE_NORMAL";
        case THREAD_PRIORITY_HIGHEST:       return "HIGHEST";
        case THREAD_PRIORITY_TIME_CRITICAL: return "TIME_CRITICAL";
        default:                            return "UNKNOWN";
        }
    }

    [[nodiscard]] inline unsigned CountSetBits(DWORD_PTR mask) noexcept
    {
        unsigned count = 0;
        while (mask != 0)
        {
            count += static_cast<unsigned>(mask & 1u);
            mask >>= 1;
        }
        return count;
    }

    inline void FormatGuidToBuffer(const GUID& guid, char* buffer, std::size_t bufferSize) noexcept
    {
        if (!buffer || bufferSize == 0)
            return;
        std::snprintf(buffer, bufferSize,
            "%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
            static_cast<unsigned long>(guid.Data1),
            static_cast<unsigned short>(guid.Data2),
            static_cast<unsigned short>(guid.Data3),
            guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
            guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    }

    inline void CapturePowerPlan(ProcessTelemetry& telemetry) noexcept
    {
        constexpr char kUnknown[] = "unknown";
        std::snprintf(telemetry.PowerPlan, sizeof(telemetry.PowerPlan), "%s", kUnknown);

        using PowerGetActiveSchemeFn = DWORD (WINAPI*)(HKEY, GUID**);
        using PowerReadFriendlyNameFn = DWORD (WINAPI*)(HKEY, const GUID*, const GUID*, const GUID*, PUCHAR, DWORD*);

        HMODULE powrprof = ::LoadLibraryW(L"powrprof.dll");
        if (!powrprof)
            return;

        auto powerGetActiveScheme = reinterpret_cast<PowerGetActiveSchemeFn>(::GetProcAddress(powrprof, "PowerGetActiveScheme"));
        auto powerReadFriendlyName = reinterpret_cast<PowerReadFriendlyNameFn>(::GetProcAddress(powrprof, "PowerReadFriendlyName"));

        GUID* activeScheme = nullptr;
        if (powerGetActiveScheme && powerGetActiveScheme(nullptr, &activeScheme) == ERROR_SUCCESS && activeScheme)
        {
            wchar_t friendly[128]{};
            DWORD friendlySize = static_cast<DWORD>(sizeof(friendly));
            if (powerReadFriendlyName && powerReadFriendlyName(nullptr, activeScheme, nullptr, nullptr, reinterpret_cast<PUCHAR>(friendly), &friendlySize) == ERROR_SUCCESS)
            {
                char utf8[128]{};
                const int converted = ::WideCharToMultiByte(CP_UTF8, 0, friendly, -1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
                if (converted > 0)
                {
                    std::snprintf(telemetry.PowerPlan, sizeof(telemetry.PowerPlan), "%s", utf8);
                }
            }
            else
            {
                char guidBuffer[64]{};
                FormatGuidToBuffer(*activeScheme, guidBuffer, sizeof(guidBuffer));
                std::snprintf(telemetry.PowerPlan, sizeof(telemetry.PowerPlan), "%s", guidBuffer);
            }
            ::LocalFree(activeScheme);
        }

        ::FreeLibrary(powrprof);
    }

    inline void CaptureTimerKind(ProcessTelemetry& telemetry) noexcept
    {
        LARGE_INTEGER freq{};
        if (::QueryPerformanceFrequency(&freq) != 0)
        {
            std::snprintf(telemetry.TimerKind, sizeof(telemetry.TimerKind), "QPC");
        }
        else
        {
            std::snprintf(telemetry.TimerKind, sizeof(telemetry.TimerKind), "std::chrono::steady_clock");
        }
    }

    [[nodiscard]] inline unsigned QueryRegistryMHz(const wchar_t* valueName) noexcept
    {
        DWORD data = 0;
        DWORD dataSize = sizeof(DWORD);
        const LSTATUS status = ::RegGetValueW(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            valueName,
            RRF_RT_REG_DWORD,
            nullptr,
            &data,
            &dataSize);
        return (status == ERROR_SUCCESS) ? static_cast<unsigned>(data) : 0u;
    }

    inline void CaptureCpuVendorNameAndFrequencies(MachineTelemetry& telemetry) noexcept
    {
        int cpuInfo[4]{};
        __cpuid(cpuInfo, 0);
        const int maxBasic = cpuInfo[0];

        char vendor[13]{};
        std::memcpy(vendor + 0, &cpuInfo[1], sizeof(int));
        std::memcpy(vendor + 4, &cpuInfo[3], sizeof(int));
        std::memcpy(vendor + 8, &cpuInfo[2], sizeof(int));
        vendor[12] = '\0';
        std::snprintf(telemetry.CpuVendor, sizeof(telemetry.CpuVendor), "%s", vendor[0] != '\0' ? vendor : "unknown");

        __cpuid(cpuInfo, 0x80000000);
        const int maxExtended = cpuInfo[0];

        char nameBuffer[64]{};
        if (maxExtended >= 0x80000004)
        {
            int nameSegments[12]{};
            __cpuid(nameSegments + 0, 0x80000002);
            __cpuid(nameSegments + 4, 0x80000003);
            __cpuid(nameSegments + 8, 0x80000004);
            std::memcpy(nameBuffer, nameSegments, sizeof(nameSegments));
        }

        if (nameBuffer[0] == '\0')
        {
            std::snprintf(telemetry.CpuName, sizeof(telemetry.CpuName), "%s", "unknown");
        }
        else
        {
            std::size_t write = 0;
            bool lastWasSpace = false;
            for (std::size_t i = 0; i < sizeof(nameBuffer) && nameBuffer[i] != '\0'; ++i)
            {
                const char c = nameBuffer[i];
                if (c == ' ' || c == '\t')
                {
                    if (!lastWasSpace && write + 1 < sizeof(telemetry.CpuName))
                    {
                        telemetry.CpuName[write++] = ' ';
                        lastWasSpace = true;
                    }
                }
                else if (c >= 32 && write + 1 < sizeof(telemetry.CpuName))
                {
                    telemetry.CpuName[write++] = c;
                    lastWasSpace = false;
                }
            }

            if (write == 0)
            {
                std::snprintf(telemetry.CpuName, sizeof(telemetry.CpuName), "%s", "unknown");
            }
            else
            {
                telemetry.CpuName[write] = '\0';
            }
        }

        unsigned baseMHz = 0;
        unsigned boostMHz = 0;

        if (maxBasic >= 0x16)
        {
            __cpuid(cpuInfo, 0x16);
            baseMHz = static_cast<unsigned>(cpuInfo[0]);
            boostMHz = static_cast<unsigned>(cpuInfo[1]);
        }

        if (baseMHz == 0)
        {
            baseMHz = QueryRegistryMHz(L"~MHz");
        }

        if (boostMHz == 0)
        {
            boostMHz = QueryRegistryMHz(L"~MHz");
        }

        telemetry.BaseMHz = baseMHz;
        telemetry.BoostMHz = (boostMHz == 0u) ? baseMHz : boostMHz;
    }

    [[nodiscard]] inline unsigned QueryLogicalCoreCount() noexcept
    {
        const DWORD count = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        return (count == 0u) ? 1u : static_cast<unsigned>(count);
    }

    [[nodiscard]] inline unsigned QueryPhysicalCoreCount() noexcept
    {
        DWORD bufferLength = 0;
        if (::GetLogicalProcessorInformation(nullptr, &bufferLength) != 0)
        {
            // Unexpected success without buffer; retry with stack allocation below.
        }

        if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || bufferLength == 0)
        {
            return QueryLogicalCoreCount();
        }

        constexpr DWORD kMaxBytes = 16u * 1024u;
        alignas(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) static std::array<std::uint8_t, kMaxBytes> buffer{};
        DWORD bytesToUse = (bufferLength > kMaxBytes) ? kMaxBytes : bufferLength;

        if (::GetLogicalProcessorInformation(
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION>(buffer.data()),
                &bytesToUse) == 0)
        {
            return QueryLogicalCoreCount();
        }

        const DWORD entryCount = bytesToUse / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        const auto* entries = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(buffer.data());

        unsigned physical = 0;
        for (DWORD i = 0; i < entryCount; ++i)
        {
            if (entries[i].Relationship == RelationProcessorCore)
            {
                ++physical;
            }
        }

        return (physical == 0u) ? QueryLogicalCoreCount() : physical;
    }

    inline void CaptureOsVersion(MachineTelemetry& telemetry) noexcept
    {
        RTL_OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);

        using RtlGetVersionFn = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
        const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));

        if (rtlGetVersion && rtlGetVersion(&version) == 0)
        {
            std::snprintf(telemetry.OsVersion, sizeof(telemetry.OsVersion),
                "Windows %lu.%lu.%lu",
                static_cast<unsigned long>(version.dwMajorVersion),
                static_cast<unsigned long>(version.dwMinorVersion),
                static_cast<unsigned long>(version.dwBuildNumber));
        }
        else
        {
            std::snprintf(telemetry.OsVersion, sizeof(telemetry.OsVersion), "%s", "Windows");
        }
    }

    inline void EnsureMachineTelemetryInitialized() noexcept
    {
        static bool initialized = false;
        if (initialized)
            return;

        initialized = true;

        CaptureCpuVendorNameAndFrequencies(gMachineTelemetry);
        gMachineTelemetry.LogicalCores = QueryLogicalCoreCount();
        gMachineTelemetry.PhysicalCores = QueryPhysicalCoreCount();
        gMachineTelemetry.AvailableCores = gMachineTelemetry.LogicalCores;
        CaptureOsVersion(gMachineTelemetry);
    }

    [[nodiscard]] inline bool QueryGitDirty() noexcept
    {
        constexpr char kCmd[] = "git status --porcelain --untracked-files=no 2>nul";
        FILE* pipe = ::_popen(kCmd, "r");
        if (!pipe)
            return false;

        char buffer[8]{};
        const std::size_t bytes = std::fread(buffer, 1, sizeof(buffer), pipe);
        (void)::_pclose(pipe);
        return bytes > 0;
    }

    inline void PopulateBuildTelemetry() noexcept
    {
        if (gBuildTelemetry.BuildType[0] != '\0')
            return;

        std::snprintf(gBuildTelemetry.BuildType, sizeof(gBuildTelemetry.BuildType), "%s", DNG_BENCH_BUILD_MODE);

#if defined(_MSC_FULL_VER)
        std::snprintf(gBuildTelemetry.CompilerVersion, sizeof(gBuildTelemetry.CompilerVersion),
            "MSVC %d", static_cast<int>(_MSC_FULL_VER));
#else
        std::snprintf(gBuildTelemetry.CompilerVersion, sizeof(gBuildTelemetry.CompilerVersion), "%s", "unknown");
#endif

#if defined(NDEBUG)
        std::snprintf(gBuildTelemetry.OptFlags, sizeof(gBuildTelemetry.OptFlags), "%s", "/O2");
#else
        std::snprintf(gBuildTelemetry.OptFlags, sizeof(gBuildTelemetry.OptFlags), "%s", "/Od");
#endif

#if defined(DNG_ENABLE_LTO) || (defined(__GNUC__) && defined(__LTO__))
        std::snprintf(gBuildTelemetry.Lto, sizeof(gBuildTelemetry.Lto), "%s", "ON");
#else
        std::snprintf(gBuildTelemetry.Lto, sizeof(gBuildTelemetry.Lto), "%s", "OFF");
#endif

#if defined(_M_X64)
        std::snprintf(gBuildTelemetry.Arch, sizeof(gBuildTelemetry.Arch), "%s", "x64");
#elif defined(_M_ARM64)
        std::snprintf(gBuildTelemetry.Arch, sizeof(gBuildTelemetry.Arch), "%s", "arm64");
#elif defined(_M_IX86)
        std::snprintf(gBuildTelemetry.Arch, sizeof(gBuildTelemetry.Arch), "%s", "x86");
#else
        std::snprintf(gBuildTelemetry.Arch, sizeof(gBuildTelemetry.Arch), "%s", "unknown");
#endif

        gBuildTelemetry.DirtyTree = QueryGitDirty();
    }

    [[nodiscard]] inline const char* DescribeThreadPriority(int priority) noexcept
    {
        return ThreadPriorityToString(priority);
    }
#else
    struct ProcessTelemetry
    {
        unsigned long long MainThreadMask = 1ull;
        int MainThreadPriority = 0;
        char PowerPlan[128]{};
        char ProcessPriority[32]{};
        char TimerKind[32]{};
    };

    ProcessTelemetry gProcessTelemetry{};

    inline void CapturePowerPlan(ProcessTelemetry& telemetry) noexcept
    {
        std::snprintf(telemetry.PowerPlan, sizeof(telemetry.PowerPlan), "%s", "unknown");
    }

    inline void CaptureTimerKind(ProcessTelemetry& telemetry) noexcept
    {
        std::snprintf(telemetry.TimerKind, sizeof(telemetry.TimerKind), "%s", "std::chrono::steady_clock");
    }

    inline void EnsureMachineTelemetryInitialized() noexcept
    {
        if (gMachineTelemetry.CpuName[0] != '\0')
            return;

        std::snprintf(gMachineTelemetry.CpuName, sizeof(gMachineTelemetry.CpuName), "%s", "unknown");
        std::snprintf(gMachineTelemetry.CpuVendor, sizeof(gMachineTelemetry.CpuVendor), "%s", "unknown");
        std::snprintf(gMachineTelemetry.OsVersion, sizeof(gMachineTelemetry.OsVersion), "%s", "unknown");

        const unsigned fallback = std::max(1u, std::thread::hardware_concurrency());
        gMachineTelemetry.PhysicalCores = fallback;
        gMachineTelemetry.LogicalCores = fallback;
        gMachineTelemetry.AvailableCores = fallback;
        gMachineTelemetry.BaseMHz = 0u;
        gMachineTelemetry.BoostMHz = 0u;
    }

    inline void PopulateBuildTelemetry() noexcept
    {
        if (gBuildTelemetry.BuildType[0] != '\0')
            return;

        std::snprintf(gBuildTelemetry.BuildType, sizeof(gBuildTelemetry.BuildType), "%s", DNG_BENCH_BUILD_MODE);
        std::snprintf(gBuildTelemetry.CompilerVersion, sizeof(gBuildTelemetry.CompilerVersion), "%s", "unknown");
        std::snprintf(gBuildTelemetry.OptFlags, sizeof(gBuildTelemetry.OptFlags), "%s", "unknown");
        std::snprintf(gBuildTelemetry.Lto, sizeof(gBuildTelemetry.Lto), "%s", "OFF");
        std::snprintf(gBuildTelemetry.Arch, sizeof(gBuildTelemetry.Arch), "%s", "unknown");
        gBuildTelemetry.DirtyTree = false;
    }

    [[nodiscard]] inline const char* DescribeThreadPriority(int) noexcept
    {
        return "NORMAL";
    }
#endif

    inline void StabilizeCpu() noexcept
    {
        EnsureMachineTelemetryInitialized();
#if defined(_WIN32)
        const HANDLE process = ::GetCurrentProcess();
        const HANDLE thread  = ::GetCurrentThread();

        (void)::SetPriorityClass(process, HIGH_PRIORITY_CLASS);
        (void)::SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);
        (void)::SetThreadPriorityBoost(thread, TRUE);

        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask  = 0;
        if (::GetProcessAffinityMask(process, &processMask, &systemMask) == 0 || systemMask == 0)
        {
            processMask = 1;
            systemMask = 1;
        }

        gProcessTelemetry.ProcessMask = processMask;
        gProcessTelemetry.SystemMask = systemMask;
        gProcessTelemetry.CpuCount = 0;

        for (unsigned i = 0; i < kMaxLogicalCpuBits; ++i)
        {
            const DWORD_PTR bit = (static_cast<DWORD_PTR>(1) << i);
            if ((systemMask & bit) != 0)
            {
                gProcessTelemetry.CpuIndices[gProcessTelemetry.CpuCount++] = i;
            }
        }

        if (gProcessTelemetry.CpuCount == 0)
        {
            gProcessTelemetry.CpuIndices[0] = 0;
            gProcessTelemetry.CpuCount = 1;
            gProcessTelemetry.SystemMask = 1;
            gProcessTelemetry.ProcessMask = 1;
        }

        std::snprintf(gProcessTelemetry.ProcessPriority,
            sizeof(gProcessTelemetry.ProcessPriority), "%s",
            PriorityClassToString(::GetPriorityClass(process)));

        const unsigned available = CountSetBits(gProcessTelemetry.ProcessMask);
        if (available != 0u)
        {
            gMachineTelemetry.AvailableCores = available;
        }

        const unsigned primaryCpu = gProcessTelemetry.CpuIndices[0];
        (void)::SetThreadIdealProcessor(thread, primaryCpu);
        const DWORD_PTR desiredMask = (static_cast<DWORD_PTR>(1) << primaryCpu);
        (void)::SetThreadAffinityMask(thread, desiredMask);
        gProcessTelemetry.MainThreadMask = desiredMask;
        gProcessTelemetry.MainThreadPriority = ::GetThreadPriority(thread);

        CapturePowerPlan(gProcessTelemetry);
        CaptureTimerKind(gProcessTelemetry);
#else
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(0, &set);
        (void)::sched_setaffinity(0, sizeof(set), &set);
        (void)::setpriority(PRIO_PROCESS, 0, -5);
        gProcessTelemetry.MainThreadMask = 1ull;
        gProcessTelemetry.MainThreadPriority = 0;
        std::snprintf(gProcessTelemetry.ProcessPriority,
            sizeof(gProcessTelemetry.ProcessPriority), "NORMAL");
        CapturePowerPlan(gProcessTelemetry);
        CaptureTimerKind(gProcessTelemetry);
#endif
    }

    [[nodiscard]] inline ThreadTelemetry StabilizeThread(unsigned logicalOrdinal, bool logDetails) noexcept
    {
        ThreadTelemetry info{};
        info.Index = logicalOrdinal;

#if defined(_WIN32)
        const HANDLE thread = ::GetCurrentThread();
        const unsigned cpuCount = (gProcessTelemetry.CpuCount == 0u) ? 1u : gProcessTelemetry.CpuCount;
        const unsigned cpuIndex = gProcessTelemetry.CpuIndices[logicalOrdinal % cpuCount];
        info.Processor = cpuIndex;

        const DWORD_PTR mask = (static_cast<DWORD_PTR>(1) << cpuIndex);
        (void)::SetThreadIdealProcessor(thread, cpuIndex);
        (void)::SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);
        (void)::SetThreadPriorityBoost(thread, TRUE);
        (void)::SetThreadAffinityMask(thread, mask);

        info.AffinityMask = static_cast<unsigned long long>(mask);
        info.Priority = ::GetThreadPriority(thread);

        if (logDetails)
        {
            char buffer[256]{};
            std::snprintf(buffer, sizeof(buffer),
                "[Thread] idx=%u cpu=%u affinity=0x%llX priority=%s",
                info.Index,
                info.Processor,
                info.AffinityMask,
                ThreadPriorityToString(info.Priority));
            PrintNote(buffer);
        }
#else
        (void)logicalOrdinal;
        (void)logDetails;
        info.Processor = 0;
        info.AffinityMask = 1ull;
        info.Priority = 0;
#endif

        return info;
    }

    inline void PrintCpuDiagnostics() noexcept
    {
        const unsigned rawLogical = std::thread::hardware_concurrency();
        const unsigned logical = (rawLogical == 0u) ? 1u : rawLogical;
        char buffer[256]{};

#if defined(_WIN32)
        const HANDLE process = ::GetCurrentProcess();
        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask = 0;
        const BOOL affinityOk = ::GetProcessAffinityMask(process, &processMask, &systemMask);
        const DWORD priorityClass = ::GetPriorityClass(process);

        const char* priorityLabel = "UNKNOWN";
        switch (priorityClass)
        {
            case IDLE_PRIORITY_CLASS:        priorityLabel = "IDLE"; break;
            case BELOW_NORMAL_PRIORITY_CLASS: priorityLabel = "BELOW_NORMAL"; break;
            case NORMAL_PRIORITY_CLASS:      priorityLabel = "NORMAL"; break;
            case ABOVE_NORMAL_PRIORITY_CLASS: priorityLabel = "ABOVE_NORMAL"; break;
            case HIGH_PRIORITY_CLASS:        priorityLabel = "HIGH"; break;
            case REALTIME_PRIORITY_CLASS:    priorityLabel = "REALTIME"; break;
            default: break;
        }

        if (affinityOk != 0)
        {
            std::snprintf(buffer, sizeof(buffer),
                "[CPU] logical=%u affinity=0x%llX priority=%s",
                logical,
                static_cast<unsigned long long>(processMask),
                priorityLabel);
        }
        else
        {
            const DWORD err = ::GetLastError();
            std::snprintf(buffer, sizeof(buffer),
                "[CPU] logical=%u affinity=<error %lu> priority=%s",
                logical,
                static_cast<unsigned long>(err),
                priorityLabel);
        }
#else
        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        const int affinityResult = ::sched_getaffinity(0, sizeof(cpuSet), &cpuSet);
        const int affinityErr = (affinityResult == 0) ? 0 : errno;

        unsigned long long mask = 0ull;
        if (affinityResult == 0)
        {
            const int maxBits = static_cast<int>(sizeof(mask) * 8);
            const int cpuLimit = std::min(maxBits, CPU_SETSIZE);
            for (int cpu = 0; cpu < cpuLimit; ++cpu)
            {
                if (CPU_ISSET(cpu, &cpuSet))
                    mask |= (1ull << cpu);
            }
        }

        errno = 0;
        const int niceValue = ::getpriority(PRIO_PROCESS, 0);
        const int niceErr = errno;

        if (affinityResult == 0)
        {
            if (niceErr == 0)
            {
                std::snprintf(buffer, sizeof(buffer),
                    "[CPU] logical=%u affinity=0x%llX nice=%d",
                    logical,
                    mask,
                    niceValue);
            }
            else
            {
                std::snprintf(buffer, sizeof(buffer),
                    "[CPU] logical=%u affinity=0x%llX nice=<error %d>",
                    logical,
                    mask,
                    niceErr);
            }
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer),
                "[CPU] logical=%u affinity=<error %d>",
                logical,
                affinityErr);
        }
#endif

        PrintLine(std::string_view{buffer});
    }

    // --- Small-object benchmark helpers ------------------------------------

    constexpr std::size_t kSmallObjectSizes[] = { 8u, 16u, 32u, 64u, 128u, 256u, 512u };
    constexpr std::size_t kSmallObjectSizeCount = sizeof(kSmallObjectSizes) / sizeof(kSmallObjectSizes[0]);
    constexpr std::size_t kSmallObjectBurst = 64u;
    constexpr std::size_t kMaxSmallObjectThreads = 8u;
    constexpr std::size_t kSmallObjectSlotCount = kSmallObjectSizeCount * kSmallObjectBurst;

#if defined(DNG_SMALLOBJ_TLS_BINS)
    constexpr bool kSmallObjectTLSCompiled = (DNG_SMALLOBJ_TLS_BINS != 0);
#else
    constexpr bool kSmallObjectTLSCompiled = false;
#endif

#if defined(DNG_GLOBAL_NEW_SMALL_THRESHOLD)
    constexpr std::size_t kGlobalNewSmallThreshold = static_cast<std::size_t>(DNG_GLOBAL_NEW_SMALL_THRESHOLD);
#else
    constexpr std::size_t kGlobalNewSmallThreshold = 0u;
#endif

#if defined(DNG_SOALLOC_BATCH)
    constexpr std::size_t kSmallObjectBatchCompiled = static_cast<std::size_t>(DNG_SOALLOC_BATCH);
#else
    constexpr std::size_t kSmallObjectBatchCompiled = 0u;
#endif

    struct SmallObjectThreadContext
    {
        void* slots[kSmallObjectSlotCount]{};

        void Reset() noexcept
        {
            for (std::size_t i = 0; i < kSmallObjectSlotCount; ++i)
            {
                slots[i] = nullptr;
            }
        }
    };

    struct ThreadTelemetryAggregate
    {
        ThreadTelemetry Entries[kMaxSmallObjectThreads]{};
        unsigned ThreadCount = 0;
        unsigned long long CombinedMask = 0;
        int Priority = THREAD_PRIORITY_NORMAL;
        bool HasLogged = false;
    };

    class LocalBarrier
    {
    public:
        explicit LocalBarrier(std::size_t count) noexcept
            : mThreshold(count == 0u ? 1u : static_cast<unsigned>(count)),
              mCount(mThreshold),
              mGeneration(0u)
        {
        }

        void ArriveAndWait() noexcept
        {
            const unsigned generation = mGeneration.load(std::memory_order_acquire);
            if (mCount.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
            {
                mCount.store(mThreshold, std::memory_order_release);
                mGeneration.fetch_add(1u, std::memory_order_acq_rel);
            }
            else
            {
                while (mGeneration.load(std::memory_order_acquire) == generation)
                {
                    std::this_thread::yield();
                }
            }
        }

    private:
        const unsigned mThreshold;
        std::atomic<unsigned> mCount;
        std::atomic<unsigned> mGeneration;
    };

    inline void PrintSmallObjectBanner(const char* caseLabel,
        bool runtimeRequested,
        bool effective,
        std::size_t threadCount) noexcept
    {
        char banner[256]{};
        const int written = std::snprintf(banner, sizeof(banner),
            "[SmallObject] %s CT=%c RT=%c EFFECTIVE=%c threshold=%llu batch=%llu threads=%llu",
            caseLabel ? caseLabel : "<unnamed>",
            kSmallObjectTLSCompiled ? '1' : '0',
            runtimeRequested ? '1' : '0',
            effective ? '1' : '0',
            static_cast<unsigned long long>(kGlobalNewSmallThreshold),
            static_cast<unsigned long long>(kSmallObjectBatchCompiled),
            static_cast<unsigned long long>(threadCount));

        if (written > 0)
        {
            const std::size_t length = (written >= static_cast<int>(sizeof(banner)))
                ? (sizeof(banner) - 1u)
                : static_cast<std::size_t>(written);
            PrintLine(std::string_view{ banner, length });
        }
    }

    inline void RunSmallObjectMultithreadIteration(::dng::core::SmallObjectAllocator& allocator,
        std::size_t threadCount,
        bool crossThreadFree,
        SmallObjectThreadContext* contexts,
        std::size_t contextCapacity,
        ThreadTelemetryAggregate* telemetry,
        bool logThreadBindings) noexcept
    {
        const std::size_t requestedThreads = (threadCount == 0u) ? 1u : threadCount;
        const std::size_t useThreads = (requestedThreads > contextCapacity) ? contextCapacity : requestedThreads;

        for (std::size_t t = 0; t < useThreads; ++t)
        {
            contexts[t].Reset();
        }

        LocalBarrier barrier(useThreads);
        const std::size_t alignment = alignof(std::max_align_t);

        auto worker = [&](std::size_t threadIndex) noexcept
        {
            const bool shouldLog = logThreadBindings && telemetry != nullptr;
            ThreadTelemetry info = StabilizeThread(static_cast<unsigned>(threadIndex), shouldLog);
            if (telemetry && threadIndex < kMaxSmallObjectThreads)
            {
                telemetry->Entries[threadIndex] = info;
            }
            SmallObjectThreadContext& ctx = contexts[threadIndex];
            for (std::size_t sizeIdx = 0; sizeIdx < kSmallObjectSizeCount; ++sizeIdx)
            {
                const std::size_t size = kSmallObjectSizes[sizeIdx];
                void** const mySlot = ctx.slots + (sizeIdx * kSmallObjectBurst);

                for (std::size_t i = 0; i < kSmallObjectBurst; ++i)
                {
                    void* ptr = allocator.Allocate(size, alignment);
                    DNG_CHECK(ptr != nullptr);
                    mySlot[i] = ptr;
                }

                barrier.ArriveAndWait();

                if (crossThreadFree && useThreads > 1u)
                {
                    const std::size_t recipientIndex = (threadIndex + 1u) % useThreads;
                    void** const recipientSlot = contexts[recipientIndex].slots + (sizeIdx * kSmallObjectBurst);
                    const std::size_t crossCount = kSmallObjectBurst / 2u;
                    for (std::size_t i = 0; i < crossCount; ++i)
                    {
                        void* target = recipientSlot[i];
                        DNG_CHECK(target != nullptr);
                        allocator.Deallocate(target, size, alignment);
                        recipientSlot[i] = nullptr;
                    }
                }

                barrier.ArriveAndWait();

                for (std::size_t i = 0; i < kSmallObjectBurst; ++i)
                {
                    void* target = mySlot[i];
                    if (target != nullptr)
                    {
                        allocator.Deallocate(target, size, alignment);
                        mySlot[i] = nullptr;
                    }
                }

                barrier.ArriveAndWait();
            }
        };

        std::thread threadPool[kMaxSmallObjectThreads];
        for (std::size_t t = 1; t < useThreads; ++t)
        {
            threadPool[t - 1] = std::thread(worker, t);
        }

        worker(0u);

        for (std::size_t t = 1; t < useThreads; ++t)
        {
            if (threadPool[t - 1].joinable())
            {
                threadPool[t - 1].join();
            }
        }

        if (telemetry)
        {
            telemetry->ThreadCount = static_cast<unsigned>(useThreads);
            telemetry->CombinedMask = 0ull;
            telemetry->Priority = THREAD_PRIORITY_NORMAL;
            if (useThreads > 0)
            {
                telemetry->Priority = telemetry->Entries[0].Priority;
            }
            for (std::size_t t = 0; t < useThreads; ++t)
            {
                telemetry->CombinedMask |= telemetry->Entries[t].AffinityMask;
                if (telemetry->Entries[t].Priority > telemetry->Priority)
                    telemetry->Priority = telemetry->Entries[t].Priority;
            }
            if (logThreadBindings)
            {
                telemetry->HasLogged = true;
            }
        }
    }

    // Build environment-driven suffixes for metric names to support param sweeps.
    // Example: [sampling=8, shards=8] or [batch=128]
    inline std::string TrackingSuffixFromEnv() noexcept
    {
    #if defined(_MSC_VER)
    #   pragma warning(push)
    #   pragma warning(disable:4996)
    #endif
        const char* s = std::getenv("DNG_MEM_TRACKING_SAMPLING_RATE");
        const char* h = std::getenv("DNG_MEM_TRACKING_SHARDS");
    #if defined(_MSC_VER)
    #   pragma warning(pop)
    #endif
        std::string tag;
        if ((s && *s) || (h && *h))
        {
            tag += " [";
            bool first = true;
            if (s && *s) { tag += "sampling="; tag += s; first = false; }
            if (h && *h) { if (!first) tag += ", "; tag += "shards="; tag += h; }
            tag += "]";
        }
        return tag;
    }

    inline std::string SoaBatchSuffixFromEnv() noexcept
    {
    #if defined(_MSC_VER)
    #   pragma warning(push)
    #   pragma warning(disable:4996)
    #endif
        const char* b = std::getenv("DNG_SOALLOC_BATCH");
    #if defined(_MSC_VER)
    #   pragma warning(pop)
    #endif
        std::string tag;
        if (b && *b)
        {
            tag += " [batch=";
            tag += b;
            tag += "]";
        }
        return tag;
    }

    // --- Allocation adapters -------------------------------------------------

    // Adapts Allocate/Deallocate naming differences across allocators.
    template<class Alloc>
    [[nodiscard]] void* AllocateCompat(Alloc* alloc, std::size_t size, std::size_t alignment) noexcept
    {
        DNG_CHECK(alloc != nullptr);

        if constexpr (requires(Alloc* a) { a->AllocateBytes(size, alignment); })
        {
            return alloc->AllocateBytes(size, alignment);
        }
        else
        {
            return alloc->Allocate(size, alignment);
        }
    }

    template<class Alloc>
    inline void DeallocateCompat(Alloc* alloc, void* ptr, std::size_t size, std::size_t alignment) noexcept
    {
        if (!alloc || !ptr)
            return;

        if constexpr (requires(Alloc* a) { a->DeallocateBytes(ptr, size, alignment); })
        {
            alloc->DeallocateBytes(ptr, size, alignment);
        }
        else
        {
            alloc->Deallocate(ptr, size, alignment);
        }
    }

    // std::vector adapter that forwards storage to the global TrackingAllocator.
    template<class T>
    struct TrackingAdapter
    {
        using value_type = T;

        TrackingAdapter() noexcept = default;
        template<class U> TrackingAdapter(const TrackingAdapter<U>&) noexcept {}

        [[nodiscard]] T* allocate(std::size_t count)
        {
            auto ref = ::dng::memory::MemorySystem::GetTrackingAllocator();
            auto* tracker = ref.Get();
            DNG_CHECK(tracker != nullptr);

            void* raw = AllocateCompat(tracker, count * sizeof(T), alignof(T));
            return static_cast<T*>(raw);
        }

        void deallocate(T* ptr, std::size_t count) noexcept
        {
            auto ref = ::dng::memory::MemorySystem::GetTrackingAllocator();
            auto* tracker = ref.Get();
            DeallocateCompat(tracker, ptr, count * sizeof(T), alignof(T));
        }

        template<class U> struct rebind { using other = TrackingAdapter<U>; };
        using propagate_on_container_swap = std::true_type;
        using is_always_equal = std::true_type;

        [[nodiscard]] bool operator==(const TrackingAdapter&) const noexcept { return true; }
        [[nodiscard]] bool operator!=(const TrackingAdapter&) const noexcept { return false; }
    };

    template<class T>
    using tracking_vector = std::vector<T, TrackingAdapter<T>>;

} // namespace

// -----------------------------------------------------------------------------

int main(int argc, char** argv)
{
    using namespace ::dng;

#ifdef __cpp_lib_print
    std::print("[BenchDriver v2] build={} {} {}\n", DNG_BENCH_BUILD_MODE, __DATE__, __TIME__);
#else
    std::cout << "[BenchDriver v2] build=" << DNG_BENCH_BUILD_MODE
              << " " << __DATE__ << " " << __TIME__ << '\n';
#endif

    int warmupCount = 0;
    int repeatMin = 3;
    int repeatMax = repeatMin;
    bool repeatMaxExplicit = false;
    double targetRsd = 0.0;
    bool cpuInfoEnabled = true;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--warmup" && (i + 1) < argc)
        {
            warmupCount = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
            if (warmupCount < 0) warmupCount = 0;
        }
        else if (arg == "--repeat" && (i + 1) < argc)
        {
            repeatMin = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
            if (repeatMin < 1) repeatMin = 1;
            if (!repeatMaxExplicit)
                repeatMax = repeatMin;
        }
        else if (arg == "--target-rsd" && (i + 1) < argc)
        {
            targetRsd = std::strtod(argv[++i], nullptr);
            if (targetRsd < 0.0)
                targetRsd = 0.0;
        }
        else if (arg == "--max-repeat" && (i + 1) < argc)
        {
            repeatMax = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
            if (repeatMax < 1) repeatMax = 1;
            repeatMaxExplicit = true;
        }
        else if (arg == "--cpu-info")
        {
            cpuInfoEnabled = true;
            if ((i + 1) < argc && argv[i + 1] && argv[i + 1][0] != '-')
            {
                const std::string value = argv[++i];
                if (value == "0" || value == "false" || value == "off")
                    cpuInfoEnabled = false;
            }
        }
        else if (arg == "--no-cpu-info")
        {
            cpuInfoEnabled = false;
        }
    }

    if (targetRsd > 0.0 && repeatMin < 3)
        repeatMin = 3;
    if (repeatMin < 1)
        repeatMin = 1;
    if (repeatMax < repeatMin)
        repeatMax = repeatMin;

    EnsureMachineTelemetryInitialized();
    PopulateBuildTelemetry();

    StabilizeCpu();
    ThreadTelemetry mainThreadTelemetry = StabilizeThread(0u, cpuInfoEnabled);
    gProcessTelemetry.MainThreadMask = mainThreadTelemetry.AffinityMask;
    gProcessTelemetry.MainThreadPriority = mainThreadTelemetry.Priority;

    if (cpuInfoEnabled)
    {
        PrintCpuDiagnostics();
    }

    ::dng::core::MemoryConfig memoryConfig{};
    memoryConfig.thread_frame_allocator_bytes = 4u * 1024u * 1024u; // 4 MiB per-thread frame arena
    memory::MemorySystem::Init(memoryConfig);

    const auto defaultRef  = memory::MemorySystem::GetDefaultAllocator();
    const auto trackingRef = memory::MemorySystem::GetTrackingAllocator();

    auto* defaultAlloc = defaultRef.Get();
    auto* tracking     = trackingRef.Get();

    if (!tracking)
    {
        PrintNote("TrackingAllocator unavailable: Bench results will show <tracking-off> for churn metrics.");
    }

    // You can bump iterations to target ~200ms per scenario in Release builds.
    constexpr std::uint64_t kIterations = 1'000'000; // Initial hint; Run() rescales toward ~250 ms.

    struct Agg
    {
        std::vector<double> ns;
        double bytesPerOp = -1.0;
        double allocsPerOp = -1.0;
        bool bytesWarned = false;
        bool allocsWarned = false;
    };

    struct ScenarioMeta
    {
        int repeats = 0;
        int warmups = 0;
        std::uint64_t opsPerIter = kIterations;
        unsigned threads = 1;
        unsigned long long affinityMask = 0ull;
        int priority = THREAD_PRIORITY_NORMAL;
        bool earlyStopUsed = false;
        double rsd = 0.0;
    };

    struct ScenarioAggregateEntry
    {
        Agg stats;
        ScenarioMeta meta;
    };

    struct ScenarioOverride
    {
        const char* pattern;
        double opsMultiplier;
        int warmupAtLeast;
        int warmupExact;
        int maxRepeatAtLeast;
        int maxRepeatExact;
        bool disableEarlyStop;
    };

    constexpr ScenarioOverride kScenarioOverrides[] = {
        { "DefaultAllocator Alloc/Free 64B", 8.0, 2, -1, 12, -1, true },
        { "DefaultAllocator Alloc/Free 64B (align16)", 8.0, 2, -1, 12, -1, true },
        { "TrackingAllocator Alloc/Free 64B", 8.0, -1, 3, -1, 15, true },
        { "SmallObject/MT4-TLS LocalFree T4", 4.0, -1, 3, -1, 15, true },
        { "Vector PushPop (no reserve)", 4.0, -1, -1, -1, -1, false },
        { "FrameScope Burst (FrameScope)", 1.0, 3, -1, 9, -1, false },
        { "FrameScope Burst (Manual)", 1.0, 3, -1, 9, -1, false },
        { "SmallObject/Shard", 4.0, 3, -1, 15, -1, true },
    };

    auto findOverride = [&](const std::string& metricName) noexcept -> const ScenarioOverride*
    {
        for (const ScenarioOverride& override : kScenarioOverrides)
        {
            if (!override.pattern)
                continue;
            const std::size_t patternLen = std::strlen(override.pattern);
            if (metricName.compare(0, patternLen, override.pattern) == 0)
                return &override;
        }
        return nullptr;
    };

    auto saturatingMultiply = [](std::uint64_t base, double multiplier) noexcept -> std::uint64_t
    {
        if (multiplier <= 1.0)
            return base;
        const long double product = static_cast<long double>(base) * static_cast<long double>(multiplier);
        if (product >= static_cast<long double>((std::numeric_limits<std::uint64_t>::max)()))
            return (std::numeric_limits<std::uint64_t>::max)();
        const std::uint64_t scaled = static_cast<std::uint64_t>(product);
        return (scaled == 0ull) ? 1ull : scaled;
    };

    std::vector<std::pair<std::string, ScenarioAggregateEntry>> aggregates; // name -> aggregate; deterministic insertion order

    auto find_or_add = [&](const char* name) -> ScenarioAggregateEntry& {
        for (auto& p : aggregates)
        {
            if (p.first == name)
                return p.second;
        }
        aggregates.emplace_back(name ? std::string{name} : std::string{"<unnamed>"}, ScenarioAggregateEntry{});
        return aggregates.back().second;
    };

    const int minRepeatCount = repeatMin;
    const int maxRepeatCount = repeatMax;
    const double targetRsdPct = targetRsd;
    const bool targetRsdActive = targetRsdPct > 0.0;

    auto executeScenario = [&](const std::string& metricName,
                               unsigned scenarioThreads,
                               auto&& runOnce,
                               ThreadTelemetryAggregate* threadTelemetry) noexcept
    {
        const ScenarioOverride* override = findOverride(metricName);
        std::uint64_t iterations = kIterations;
        if (override && override->opsMultiplier > 1.0)
            iterations = saturatingMultiply(iterations, override->opsMultiplier);

        int scenarioWarmups = warmupCount;
        if (override)
        {
            if (override->warmupAtLeast >= 0)
                scenarioWarmups = std::max(scenarioWarmups, override->warmupAtLeast);
            if (override->warmupExact >= 0)
                scenarioWarmups = override->warmupExact;
        }

        int scenarioMinRepeat = minRepeatCount;
        int scenarioMaxRepeat = maxRepeatCount;
        if (override)
        {
            if (override->maxRepeatAtLeast >= 0)
                scenarioMaxRepeat = std::max(scenarioMaxRepeat, override->maxRepeatAtLeast);
            if (override->maxRepeatExact >= 0)
                scenarioMaxRepeat = override->maxRepeatExact;
        }
        if (scenarioMaxRepeat < scenarioMinRepeat)
            scenarioMaxRepeat = scenarioMinRepeat;

        const bool earlyStopEnabled = !(override && override->disableEarlyStop);

        if (threadTelemetry)
        {
            threadTelemetry->ThreadCount = 0;
            threadTelemetry->CombinedMask = 0ull;
            threadTelemetry->Priority = THREAD_PRIORITY_NORMAL;
            threadTelemetry->HasLogged = false;
        }

        for (int i = 0; i < scenarioWarmups; ++i)
        {
            (void)runOnce(iterations, threadTelemetry, false);
        }

        auto& entry = find_or_add(metricName.c_str());
        Agg& agg = entry.stats;
        ScenarioMeta& meta = entry.meta;

        agg.ns.clear();
        agg.ns.reserve(static_cast<std::size_t>(scenarioMaxRepeat));
        agg.bytesPerOp = -1.0;
        agg.allocsPerOp = -1.0;
        agg.bytesWarned = false;
        agg.allocsWarned = false;

        meta.repeats = 0;
        meta.warmups = scenarioWarmups;
        meta.opsPerIter = iterations;
        meta.threads = scenarioThreads;
        meta.affinityMask = gProcessTelemetry.MainThreadMask;
        meta.priority = gProcessTelemetry.MainThreadPriority;
        meta.earlyStopUsed = false;
        meta.rsd = 0.0;

        int repeats = 0;
        bool hitMax = false;
        bool earlyStopTriggered = false;
        std::uint64_t lastIterations = iterations;

        while (true)
        {
            const bool logThreads = (threadTelemetry != nullptr) && !threadTelemetry->HasLogged && repeats == 0;
            auto result = runOnce(iterations, threadTelemetry, logThreads);
            PrintBenchLine(result);
            ++repeats;

            lastIterations = result.Iterations;
            agg.ns.push_back(result.NsPerOp);

            if (result.BytesPerOp >= 0.0)
            {
                if (agg.bytesPerOp < 0.0)
                {
                    agg.bytesPerOp = result.BytesPerOp;
                }
                else if (!agg.bytesWarned && std::abs(agg.bytesPerOp - result.BytesPerOp) > 1e-9)
                {
                    char warn[256]{};
                    std::snprintf(warn, sizeof(warn),
                        "[WARN] bytesPerOp mismatch across repeats for %s",
                        metricName.c_str());
                    PrintNote(warn);
                    agg.bytesWarned = true;
                }
            }

            if (result.AllocsPerOp >= 0.0)
            {
                if (agg.allocsPerOp < 0.0)
                {
                    agg.allocsPerOp = result.AllocsPerOp;
                }
                else if (!agg.allocsWarned && std::abs(agg.allocsPerOp - result.AllocsPerOp) > 1e-9)
                {
                    char warn[256]{};
                    std::snprintf(warn, sizeof(warn),
                        "[WARN] allocsPerOp mismatch across repeats for %s",
                        metricName.c_str());
                    PrintNote(warn);
                    agg.allocsWarned = true;
                }
            }

            const Stats stats = ComputeStats(agg.ns.data(), agg.ns.size());
            const bool reachedMin = repeats >= scenarioMinRepeat;
            const bool reachedMax = repeats >= scenarioMaxRepeat;
            const bool meetsRsd = targetRsdActive && repeats >= 3 && stats.RsdPct <= targetRsdPct;

            if (reachedMax)
            {
                hitMax = true;
                break;
            }

            if (targetRsdActive)
            {
                if (reachedMin && meetsRsd && earlyStopEnabled)
                {
                    earlyStopTriggered = true;
                    break;
                }
            }
            else if (reachedMin && earlyStopEnabled)
            {
                break;
            }
        }

        const Stats finalStats = ComputeStats(agg.ns.data(), agg.ns.size());
        meta.repeats = repeats;
        meta.opsPerIter = lastIterations;
        meta.rsd = finalStats.RsdPct;
        if (threadTelemetry && threadTelemetry->ThreadCount > 0)
        {
            meta.threads = threadTelemetry->ThreadCount;
            meta.affinityMask = threadTelemetry->CombinedMask;
            meta.priority = threadTelemetry->Priority;
        }
        meta.earlyStopUsed = earlyStopEnabled && earlyStopTriggered;

        char summary[256]{};
        std::snprintf(summary, sizeof(summary),
            "n=%d median=%.3f mean=%.3f RSD=%.3f%% min=%.3f max=%.3f",
            repeats,
            finalStats.Median,
            finalStats.Mean,
            finalStats.RsdPct,
            finalStats.Min,
            finalStats.Max);
        PrintLine(std::string_view{summary});

        if (targetRsdActive && finalStats.RsdPct > targetRsdPct && hitMax)
        {
            char warn[256]{};
            std::snprintf(warn, sizeof(warn),
                "[WARN] target RSD exceeded for %s: %.3f%% > %.3f%% (max-repeat reached)",
                metricName.c_str(),
                finalStats.RsdPct,
                targetRsdPct);
            PrintNote(warn);
        }

        char sampleNote[256]{};
        std::snprintf(sampleNote, sizeof(sampleNote),
            "[Sample] %s opsPerIter=%llu repeats=%d warmups=%d earlyStopUsed=%s",
            metricName.c_str(),
            static_cast<unsigned long long>(meta.opsPerIter),
            meta.repeats,
            meta.warmups,
            meta.earlyStopUsed ? "true" : "false");
        PrintNote(sampleNote);
    };

    // ---- Baseline: NoOp (harness overhead) ---------------------------------
    {
        const std::string metricName = std::string{"NoOp"};
        auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
            return DNG_BENCH(metricName.c_str(), iterations, []() noexcept {});
        };
        executeScenario(metricName, 1u, runOnce, nullptr);
    }

    // ---- Scenario 1: dng::vector push/pop without reserve -------------------
    {
        const std::string metricName = std::string{"Vector PushPop (no reserve)"};
        auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
            vector<int> values;
            std::uint32_t counter = 0;
            return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                values.push_back(static_cast<int>(counter++));
                values.pop_back();
            });
        };
        executeScenario(metricName, 1u, runOnce, nullptr);
    }

    // ---- Scenario 2: dng::vector push/pop with reserve ----------------------
    {
        const std::string metricName = std::string{"Vector PushPop (reserved)"};
        auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
            vector<int> values;
            values.reserve(static_cast<std::size_t>(iterations) + 1u);
            std::uint32_t counter = 0;
            return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                values.push_back(static_cast<int>(counter++));
                values.pop_back();
            });
        };
        executeScenario(metricName, 1u, runOnce, nullptr);
    }

    // ---- Scenario 3: Arena allocate/rewind (64 bytes) -----------------------
    if (defaultAlloc)
    {
        const std::string metricName = std::string{"Arena Allocate/Rewind (64B)"};
        auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
            ::dng::core::ArenaAllocator arena{ defaultAlloc, 8u * 1024u * 1024u };
            return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                const auto marker = arena.GetMarker();
                void* ptr = arena.Allocate(64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
                arena.Rewind(marker);
            });
        };
        executeScenario(metricName, 1u, runOnce, nullptr);
    }
    else
    {
        PrintNote("Default allocator unavailable; skipping arena scenario.");
    }

    // ---- Scenario 3b: Arena bulk allocate + rewind -------------------------
    if (defaultAlloc)
    {
        const std::string metricName = std::string{"Arena Bulk Allocate/Rewind (8x64B)"};
        auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
            ::dng::core::ArenaAllocator arena{ defaultAlloc, 8u * 1024u * 1024u };
            return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                const auto marker = arena.GetMarker();
                for (int i = 0; i < 8; ++i)
                {
                    void* ptr = arena.Allocate(64u, alignof(std::max_align_t));
                    DNG_CHECK(ptr != nullptr);
                }
                arena.Rewind(marker);
            });
        };
        executeScenario(metricName, 1u, runOnce, nullptr);
    }

    // ---- Scenario 4: DefaultAllocator direct alloc/free (64 bytes) ----------
    if (defaultAlloc)
    {
        const std::string metricName = std::string{"DefaultAllocator Alloc/Free 64B"};
        auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
            return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                void* ptr = AllocateCompat(defaultAlloc, 64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
                DeallocateCompat(defaultAlloc, ptr, 64u, alignof(std::max_align_t));
            });
        };
        executeScenario(metricName, 1u, runOnce, nullptr);
    }
    else
    {
        PrintNote("Default allocator unavailable; skipping default alloc/free scenario.");
    }

    // ---- Scenario 4b: DefaultAllocator direct alloc/free (64 bytes, align 16) ----
    if (defaultAlloc)
    {
        const std::string metricName = std::string{"DefaultAllocator Alloc/Free 64B (align16)"};
        auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
            static constexpr std::size_t kAlignment16 = 16u;
            return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                void* ptr = AllocateCompat(defaultAlloc, 64u, kAlignment16);
                DNG_CHECK(ptr != nullptr);
                DeallocateCompat(defaultAlloc, ptr, 64u, kAlignment16);
            });
        };
        executeScenario(metricName, 1u, runOnce, nullptr);
    }

    // ---- Scenario 4c: FrameScope burst vs manual baseline --------------
    {
        const auto& globalConfig = ::dng::core::MemoryConfig::GetGlobal();
        if (globalConfig.thread_frame_allocator_bytes == 0u)
        {
            PrintNote("Thread frame allocator disabled; skipping FrameScope burst scenarios.");
        }
        else
        {
            constexpr std::uint64_t kFrameBurstCount = 100'000ull;
            constexpr std::array<std::size_t, 8> kBurstSizes = { 16u, 24u, 32u, 48u, 24u, 32u, 40u, 56u };
            constexpr std::size_t kBurstAlignment = alignof(std::max_align_t);

            // Each measured iteration performs kFrameBurstCount allocations before rewinding.
            auto runBurst = [&](::dng::core::FrameAllocator& frame) noexcept {
                for (std::uint64_t i = 0; i < kFrameBurstCount; ++i)
                {
                    const std::size_t size = kBurstSizes[static_cast<std::size_t>(i % kBurstSizes.size())];
                    void* ptr = frame.Allocate(static_cast<::dng::core::usize>(size), kBurstAlignment);
                    DNG_CHECK(ptr != nullptr);
                }
            };

            const auto normalizeResult = [](const bench::BenchResult& raw) noexcept {
                bench::BenchResult adjusted = raw;
                adjusted.Iterations = kFrameBurstCount;
                const double scale = static_cast<double>(kFrameBurstCount);
                adjusted.NsPerOp /= scale;
                if (adjusted.BytesPerOp >= 0.0)
                {
                    adjusted.BytesPerOp /= scale;
                }
                if (adjusted.AllocsPerOp >= 0.0)
                {
                    adjusted.AllocsPerOp /= scale;
                }
                return adjusted;
            };

            {
                const std::string metricName = std::string{"FrameScope Burst (FrameScope)"};
                auto runOnce = [&](std::uint64_t, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
                    const bench::BenchResult raw = DNG_BENCH(metricName.c_str(), 1u, [&]() noexcept {
                        ::dng::memory::FrameScope scope{};
                        auto& frame = scope.GetAllocator();
                        runBurst(frame);
                    });
                    return normalizeResult(raw);
                };
                executeScenario(metricName, 1u, runOnce, nullptr);
            }

            {
                const std::string metricName = std::string{"FrameScope Burst (Manual)"};
                auto runOnce = [&](std::uint64_t, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
                    const bench::BenchResult raw = DNG_BENCH(metricName.c_str(), 1u, [&]() noexcept {
                        auto& frame = ::dng::memory::MemorySystem::GetThreadFrameAllocator();
                        const auto marker = frame.GetMarker();
                        runBurst(frame);
                        frame.Rewind(marker);
                    });
                    return normalizeResult(raw);
                };
                executeScenario(metricName, 1u, runOnce, nullptr);
            }
        }
    }

    // ---- Scenario 5: TrackingAllocator direct alloc/free (64 bytes) ---------
    if (tracking)
    {
        const std::string metricName = std::string{"TrackingAllocator Alloc/Free 64B"} + TrackingSuffixFromEnv();
        auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
            return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                void* ptr = AllocateCompat(tracking, 64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
                DeallocateCompat(tracking, ptr, 64u, alignof(std::max_align_t));
            });
        };
        executeScenario(metricName, 1u, runOnce, nullptr);
    }
    else
    {
        PrintNote("Tracking allocator unavailable; skipping TrackingAllocator scenario.");
    }

    // ---- Scenario 6: tracking_vector push/pop (no reserve / reserved) -------
    if (tracking)
    {
        {
            const std::string metricName = std::string{"tracking_vector PushPop (no reserve)"} + TrackingSuffixFromEnv();
            auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
                tracking_vector<int> trackedValues;
                std::uint32_t counter = 0;
                return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                    trackedValues.push_back(static_cast<int>(counter++));
                    trackedValues.pop_back();
                });
            };
            executeScenario(metricName, 1u, runOnce, nullptr);
        }

        {
            const std::string metricName = std::string{"tracking_vector PushPop (reserved)"} + TrackingSuffixFromEnv();
            auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
                tracking_vector<int> trackedValues;
                trackedValues.reserve(static_cast<std::size_t>(iterations) + 1u);
                std::uint32_t counter = 0;
                return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                    trackedValues.push_back(static_cast<int>(counter++));
                    trackedValues.pop_back();
                });
            };
            executeScenario(metricName, 1u, runOnce, nullptr);
            PrintNote("tracking_vector PushPop (reserved) uses TrackingAllocator instrumentation; expect higher per-op cost versus std::vector despite identical workload.");
        }
    }
    else
    {
        PrintNote("Tracking allocator unavailable; skipping tracking_vector scenarios.");
    }

    // ---- Scenario 7: SmallObjectAllocator 64B (env-tagged batch) ----------
    if (defaultAlloc)
    {
        const std::string metricName = std::string{"SmallObject 64B"} + SoaBatchSuffixFromEnv();
        auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate*, bool) noexcept -> bench::BenchResult {
            ::dng::core::SmallObjectAllocator soalloc{ defaultAlloc };
            return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                void* ptr = AllocateCompat(&soalloc, 64u, alignof(std::max_align_t));
                DNG_CHECK(ptr != nullptr);
                DeallocateCompat(&soalloc, ptr, 64u, alignof(std::max_align_t));
            });
        };
        executeScenario(metricName, 1u, runOnce, nullptr);
    }
    else
    {
        PrintNote("Default allocator unavailable; skipping SmallObject scenario.");
    }

    // ---- Scenario 8: SmallObjectAllocator multi-thread stress --------------
    if (defaultAlloc)
    {
        struct SmallScenario
        {
            const char* Label;
            bool RuntimeTLS;
            bool CrossThreadFree;
            std::size_t Threads;
        };

        const SmallScenario scenarios[] = {
            { "ST-Local", false, false, 1u },
            { "ST-TLS",   true,  false, 1u },
            { "MT2-Local", false, false, 2u },
            { "MT2-TLS",   true,  false, 2u },
            { "MT2-TLS-XFree", true, true, 2u },
            { "MT4-TLS",   true,  false, 4u },
        };

        SmallObjectThreadContext contexts[kMaxSmallObjectThreads]{};

        for (const SmallScenario& scenario : scenarios)
        {
            ::dng::core::SmallObjectConfig config{};
            config.EnableTLSBins = scenario.RuntimeTLS;
            ::dng::core::SmallObjectAllocator soalloc{ defaultAlloc, config };

            const bool effectiveTLS = scenario.RuntimeTLS && kSmallObjectTLSCompiled;
            PrintSmallObjectBanner(scenario.Label, scenario.RuntimeTLS, effectiveTLS, scenario.Threads);

            std::string metricName = std::string{"SmallObject/"} + scenario.Label;
            metricName += scenario.CrossThreadFree ? " CrossFree" : " LocalFree";
            metricName += " T";
            metricName += std::to_string(static_cast<unsigned long long>(scenario.Threads));

            ThreadTelemetryAggregate threadTelemetry{};
            auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate* telemetry, bool logThreads) noexcept -> bench::BenchResult {
                DNG_CHECK(telemetry != nullptr);
                bool logged = false;
                return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                    const bool doLog = logThreads && !logged;
                    RunSmallObjectMultithreadIteration(soalloc,
                        scenario.Threads,
                        scenario.CrossThreadFree,
                        contexts,
                        kMaxSmallObjectThreads,
                        telemetry,
                        doLog);
                    if (doLog)
                        logged = true;
                });
            };

            executeScenario(metricName, static_cast<unsigned>(scenario.Threads), runOnce, &threadTelemetry);
        }
    }
    else
    {
        PrintNote("Default allocator unavailable; skipping SmallObject TLS scenarios.");
    }

    // ---- Scenario 9: SmallObject shard sweep (local vs remote free) ------
    if (defaultAlloc)
    {
        struct ShardSweepScenario
        {
            std::size_t Shards;
            bool RemoteFree;
        };

        constexpr std::size_t kShardSweepThreads = 4u;
        static_assert(kShardSweepThreads <= kMaxSmallObjectThreads, "Shard sweep thread count exceeds context capacity");

        const ShardSweepScenario shardCases[] = {
            { 1u, false },
            { 1u, true  },
            { 2u, false },
            { 2u, true  },
            { 4u, false },
            { 4u, true  },
            { 8u, false },
            { 8u, true  },
        };

        SmallObjectThreadContext contexts[kMaxSmallObjectThreads]{};

        for (const ShardSweepScenario& shardCase : shardCases)
        {
            ::dng::core::SmallObjectConfig config{};
            config.EnableTLSBins = false;
            config.ShardCountOverride = shardCase.Shards;

            ::dng::core::SmallObjectAllocator soalloc{ defaultAlloc, config };

            char caseLabel[64]{};
            const int labelWritten = std::snprintf(caseLabel, sizeof(caseLabel), "Shard%llu-%s",
                static_cast<unsigned long long>(shardCase.Shards),
                shardCase.RemoteFree ? "Remote" : "Local");
            if (labelWritten > 0)
            {
                PrintSmallObjectBanner(caseLabel, false, false, kShardSweepThreads);
            }

            std::string metricName = "SmallObject/Shard";
            metricName += std::to_string(static_cast<unsigned long long>(shardCase.Shards));
            metricName += shardCase.RemoteFree ? " RemoteFree" : " LocalFree";
            metricName += " T";
            metricName += std::to_string(static_cast<unsigned long long>(kShardSweepThreads));

            ThreadTelemetryAggregate threadTelemetry{};
            auto runOnce = [&](std::uint64_t iterations, ThreadTelemetryAggregate* telemetry, bool logThreads) noexcept -> bench::BenchResult {
                DNG_CHECK(telemetry != nullptr);
                bool logged = false;
                return DNG_BENCH(metricName.c_str(), iterations, [&]() noexcept {
                    const bool doLog = logThreads && !logged;
                    RunSmallObjectMultithreadIteration(soalloc,
                        kShardSweepThreads,
                        shardCase.RemoteFree,
                        contexts,
                        kMaxSmallObjectThreads,
                        telemetry,
                        doLog);
                    if (doLog)
                    {
                        logged = true;
                    }
                });
            };

            executeScenario(metricName, static_cast<unsigned>(kShardSweepThreads), runOnce, &threadTelemetry);
        }
    }
    else
    {
        PrintNote("Default allocator unavailable; skipping SmallObject shard sweep.");
    }
    // --- Export JSON to artifacts/bench -------------------------------------
    {
        using namespace ::dng::bench;
        // Ensure output directory exists
        if (!EnsureBenchOutputDirExists())
        {
            PrintNote("Failed to create bench output directory; skipping JSON export.");
        }
        else
        {
            const char* outDir = BenchOutputDir();
#if defined(_WIN32)
            const char kSep = '\\';
#else
            const char kSep = '/';
#endif
            // Build ISO-like UTC timestamp
            char timeIso[32]{};
            std::time_t now = std::time(nullptr);
            std::tm tmUtc{};
#if defined(_WIN32)
            gmtime_s(&tmUtc, &now);
#else
            gmtime_r(&now, &tmUtc);
#endif
            // Use filename-safe UTC stamp (no ':' for Windows): YYYYMMDDTHHMMSSZ
            std::snprintf(timeIso, sizeof(timeIso), "%04d%02d%02dT%02d%02d%02dZ",
                tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday,
                tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec);

            // Choose gitSha from environment if available (e.g., CI)
            const char* sha = std::getenv("GITHUB_SHA");
            if (!sha || sha[0] == '\0') sha = std::getenv("GIT_COMMIT");
            if (!sha || sha[0] == '\0') sha = "unknown";

            // Compose filename: bench-runner-{gitsha}-{dateUtc}-windows-x64-msvc.bench.json
            char filePath[1024]{};
            std::snprintf(filePath, sizeof(filePath), "%s%cbench-runner-%s-%s-windows-x64-msvc.bench.json",
                outDir, kSep, sha, timeIso);

            PrintNote(filePath);
            std::FILE* f = std::fopen(filePath, "wb");
            if (!f)
            {
                PrintNote("Failed to open bench JSON file for writing.");
            }
            else
            {
                const char* powerPlan = (gProcessTelemetry.PowerPlan[0] != '\0') ? gProcessTelemetry.PowerPlan : "unknown";
                const char* processPriority = (gProcessTelemetry.ProcessPriority[0] != '\0') ? gProcessTelemetry.ProcessPriority : "UNKNOWN";
                const char* timerKind = (gProcessTelemetry.TimerKind[0] != '\0') ? gProcessTelemetry.TimerKind : "std::chrono::steady_clock";
                const char* mainThreadPriority = DescribeThreadPriority(gProcessTelemetry.MainThreadPriority);
#if defined(_WIN32)
                const unsigned long long processAffinity = static_cast<unsigned long long>(gProcessTelemetry.ProcessMask);
                const unsigned long long systemAffinity = static_cast<unsigned long long>(gProcessTelemetry.SystemMask);
#else
                const unsigned long long processAffinity = gProcessTelemetry.MainThreadMask;
#endif

                std::fprintf(f, "{\n");
                std::fprintf(f, "  \"schema\": { \"name\": \"dng.bench\", \"version\": 1 },\n");
                std::fprintf(f, "  \"suite\": \"bench-runner\",\n");
                std::fprintf(f, "  \"timestampUtc\": ");
                WriteJsonEscaped(f, std::string_view{timeIso});
                std::fprintf(f, ",\n");
                std::fprintf(f, "  \"git\": { \"sha\": ");
                WriteJsonEscaped(f, sha);
                std::fprintf(f, ", \"dirty\": %s },\n", gBuildTelemetry.DirtyTree ? "true" : "false");
                std::fprintf(f, "  \"run\": {\n");
                std::fprintf(f, "    \"warmupDefault\": %d,\n", warmupCount);
                std::fprintf(f, "    \"repeatMin\": %d,\n", repeatMin);
                std::fprintf(f, "    \"repeatMax\": %d,\n", repeatMax);
                std::fprintf(f, "    \"targetRsd\": %.3f\n", targetRsdActive ? targetRsdPct : 0.0);
                std::fprintf(f, "  },\n");
                std::fprintf(f, "  \"machine\": {\n");
                std::fprintf(f, "    \"osVersion\": ");
                WriteJsonEscaped(f, gMachineTelemetry.OsVersion);
                std::fprintf(f, ",\n");
                std::fprintf(f, "    \"cpu\": {\n");
                std::fprintf(f, "      \"vendor\": ");
                WriteJsonEscaped(f, gMachineTelemetry.CpuVendor);
                std::fprintf(f, ",\n");
                std::fprintf(f, "      \"name\": ");
                WriteJsonEscaped(f, gMachineTelemetry.CpuName);
                std::fprintf(f, ",\n");
                std::fprintf(f, "      \"physicalCores\": %u,\n", gMachineTelemetry.PhysicalCores);
                std::fprintf(f, "      \"logicalCores\": %u,\n", gMachineTelemetry.LogicalCores);
                std::fprintf(f, "      \"availableCores\": %u,\n", gMachineTelemetry.AvailableCores);
                std::fprintf(f, "      \"baseMHz\": %u,\n", gMachineTelemetry.BaseMHz);
                std::fprintf(f, "      \"boostMHz\": %u\n", gMachineTelemetry.BoostMHz);
                std::fprintf(f, "    }\n");
                std::fprintf(f, "  },\n");
                std::fprintf(f, "  \"build\": {\n");
                std::fprintf(f, "    \"type\": ");
                WriteJsonEscaped(f, gBuildTelemetry.BuildType);
                std::fprintf(f, ",\n");
                std::fprintf(f, "    \"compiler\": ");
                WriteJsonEscaped(f, gBuildTelemetry.CompilerVersion);
                std::fprintf(f, ",\n");
                std::fprintf(f, "    \"optFlags\": ");
                WriteJsonEscaped(f, gBuildTelemetry.OptFlags);
                std::fprintf(f, ",\n");
                std::fprintf(f, "    \"lto\": ");
                WriteJsonEscaped(f, gBuildTelemetry.Lto);
                std::fprintf(f, ",\n");
                std::fprintf(f, "    \"arch\": ");
                WriteJsonEscaped(f, gBuildTelemetry.Arch);
                std::fprintf(f, ",\n");
                std::fprintf(f, "    \"dirtyTree\": %s\n", gBuildTelemetry.DirtyTree ? "true" : "false");
                std::fprintf(f, "  },\n");
                std::fprintf(f, "  \"process\": {\n");
                std::fprintf(f, "    \"powerPlan\": ");
                WriteJsonEscaped(f, powerPlan);
                std::fprintf(f, ",\n");
                std::fprintf(f, "    \"priorityClass\": ");
                WriteJsonEscaped(f, processPriority);
                std::fprintf(f, ",\n");
                std::fprintf(f, "    \"timerKind\": ");
                WriteJsonEscaped(f, timerKind);
                std::fprintf(f, ",\n");
                std::fprintf(f, "    \"cpuAffinity\": \"0x%llX\"", processAffinity);
#if defined(_WIN32)
                std::fprintf(f, ",\n    \"systemAffinity\": \"0x%llX\"", systemAffinity);
#endif
                std::fprintf(f, ",\n    \"mainThread\": {\n");
                std::fprintf(f, "      \"mask\": \"0x%llX\",\n", static_cast<unsigned long long>(gProcessTelemetry.MainThreadMask));
                std::fprintf(f, "      \"priority\": ");
                WriteJsonEscaped(f, mainThreadPriority);
                std::fprintf(f, "\n    }\n");
                std::fprintf(f, "  },\n");
                std::fprintf(f, "  \"scenarios\": [\n");
                for (std::size_t i = 0; i < aggregates.size(); ++i)
                {
                    const auto& name = aggregates[i].first;
                    const auto& entry = aggregates[i].second;
                    const Agg& aggStats = entry.stats;
                    const ScenarioMeta& meta = entry.meta;
                    const Stats stats = ComputeStats(aggStats.ns.data(), aggStats.ns.size());
                    const std::string_view nameView{name};
                    const std::uint64_t scenarioId = HashScenarioKey(nameView, meta.threads);
                    const bool oversubscribed = (gMachineTelemetry.AvailableCores != 0u) && (meta.threads > gMachineTelemetry.AvailableCores);

                    std::fprintf(f, "    {\n");
                    std::fprintf(f, "      \"scenarioId\": \"0x%016llX\",\n", static_cast<unsigned long long>(scenarioId));
                    std::fprintf(f, "      \"name\": ");
                    WriteJsonEscaped(f, nameView);
                    std::fprintf(f, ",\n");
                    std::fprintf(f, "      \"unit\": \"ns/op\",\n");
                    std::fprintf(f, "      \"stats\": {\n");
                    std::fprintf(f, "        \"median\": %.6f,\n", stats.Median);
                    std::fprintf(f, "        \"mad\": %.6f,\n", stats.Mad);
                    std::fprintf(f, "        \"mean\": %.6f,\n", stats.Mean);
                    std::fprintf(f, "        \"stddev\": %.6f,\n", stats.StdDev);
                    std::fprintf(f, "        \"min\": %.6f,\n", stats.Min);
                    std::fprintf(f, "        \"max\": %.6f,\n", stats.Max);
                    std::fprintf(f, "        \"rsd\": %.6f,\n", stats.RsdPct);
                    std::fprintf(f, "        \"samples\": %u\n", static_cast<unsigned>(aggStats.ns.size()));
                    std::fprintf(f, "      },\n");
                    std::fprintf(f, "      \"meta\": {\n");
                    std::fprintf(f, "        \"warmups\": %d,\n", meta.warmups);
                    std::fprintf(f, "        \"repeats\": %d,\n", meta.repeats);
                    std::fprintf(f, "        \"opsPerIter\": %llu,\n", static_cast<unsigned long long>(meta.opsPerIter));
                    std::fprintf(f, "        \"threads\": %u,\n", meta.threads);
                    std::fprintf(f, "        \"affinityMask\": \"0x%llX\",\n", static_cast<unsigned long long>(meta.affinityMask));
                    std::fprintf(f, "        \"priority\": ");
                    WriteJsonEscaped(f, DescribeThreadPriority(meta.priority));
                    std::fprintf(f, ",\n");
                    std::fprintf(f, "        \"oversubscribed\": %s,\n", oversubscribed ? "true" : "false");
                    std::fprintf(f, "        \"earlyStopUsed\": %s\n", meta.earlyStopUsed ? "true" : "false");
                    std::fprintf(f, "      }");

                    const bool hasAllocStats = (aggStats.bytesPerOp >= 0.0) || (aggStats.allocsPerOp >= 0.0);
                    if (hasAllocStats)
                    {
                        std::fprintf(f, ",\n      \"alloc\": {\n");
                        bool wroteAllocField = false;
                        if (aggStats.bytesPerOp >= 0.0)
                        {
                            std::fprintf(f, "        \"bytesPerOp\": %.6f", aggStats.bytesPerOp);
                            wroteAllocField = true;
                        }
                        if (aggStats.allocsPerOp >= 0.0)
                        {
                            if (wroteAllocField)
                                std::fprintf(f, ",\n");
                            std::fprintf(f, "        \"allocsPerOp\": %.6f", aggStats.allocsPerOp);
                            wroteAllocField = true;
                        }
                        if (wroteAllocField)
                            std::fprintf(f, "\n");
                        std::fprintf(f, "      }\n");
                    }
                    else
                    {
                        std::fprintf(f, "\n");
                    }

                    std::fprintf(f, "    }%s\n", (i + 1 < aggregates.size()) ? "," : "");
                }
                std::fprintf(f, "  ]\n");
                std::fprintf(f, "}\n");
                std::fclose(f);

                PrintNote("Bench JSON exported to artifacts/bench/");
            }
        }
    }

    memory::MemorySystem::Shutdown();
    return 0;
}
