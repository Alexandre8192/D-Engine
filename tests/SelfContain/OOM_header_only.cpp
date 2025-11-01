// Compile-only self containment check for OOM.hpp
#include "Core/Memory/OOM.hpp"

namespace {
    constexpr bool kFatalPolicyDefault = (DNG_MEM_FATAL_ON_OOM == 0) || (DNG_MEM_FATAL_ON_OOM == 1);
    static_assert(kFatalPolicyDefault, "OOM policy macro defaults to binary switch");
}

static_assert(true, "OOM header-only TU compiles");
