// Compile-only include-order check: memory headers precede Logger.hpp
#include "Core/Memory/ArenaAllocator.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/OOM.hpp"
#include "Core/Memory/StackAllocator.hpp"
#include "Core/Logger.hpp"

namespace {
    void TouchHeaderFirst() noexcept
    {
        DNG_LOG_INFO("Memory", "header-first include order synthesised warning path");
        DNG_LOG_WARNING("Memory", "header-first include order synthesised warning path");
        DNG_LOG_ERROR("Memory", "header-first include order synthesised warning path");
    }
}

static_assert(true, "Header-first include order compiles with active logging macros");
