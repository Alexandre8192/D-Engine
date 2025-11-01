// Compile-only include-order check: Logger.hpp included before memory headers
#include "Core/Logger.hpp"
#include "Core/Memory/ArenaAllocator.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/OOM.hpp"
#include "Core/Memory/StackAllocator.hpp"

namespace {
    void TouchLoggerFirst() noexcept
    {
        DNG_LOG_INFO("Memory", "logger-first include order synthesised warning path");
        DNG_LOG_WARNING("Memory", "logger-first include order synthesised warning path");
        DNG_LOG_ERROR("Memory", "logger-first include order synthesised warning path");
    }
}

static_assert(true, "Logger-first include order compiles with active logging macros");
