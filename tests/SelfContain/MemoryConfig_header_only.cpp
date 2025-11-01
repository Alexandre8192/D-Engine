// Compile-only self containment check for MemoryConfig.hpp
#include "Core/Memory/MemoryConfig.hpp"

namespace {
    constexpr int kMemoryConfigLogVerbosity = DNG_MEM_LOG_VERBOSITY;
    static_assert(kMemoryConfigLogVerbosity >= 0, "MemoryConfig exposes log verbosity macro");
}

static_assert(true, "MemoryConfig header-only TU compiles");
