#pragma once

#include <cstddef> // std::size_t, std::ptrdiff_t

#include "Core/Platform/PlatformCompiler.hpp"
#include "Core/Platform/PlatformTypes.hpp"

// =============================
// Types.hpp
// =============================
// Legacy facade over the platform-backed type layer.
// Keep domain-facing aliases in namespace dng while delegating the
// low-level source of truth to Core/Platform.
// =============================

namespace dng
{
// ---- Integer types ----
using int8 = ::int8;
using int16 = ::int16;
using int32 = ::int32;
using int64 = ::int64;

using uint8 = ::uint8;
using uint16 = ::uint16;
using uint32 = ::uint32;
using uint64 = ::uint64;

// ---- Short aliases (i*/u* pattern) ----
using i8  = int8;
using i16 = int16;
using i32 = int32;
using i64 = int64;

using u8  = uint8;
using u16 = uint16;
using u32 = uint32;
using u64 = uint64;

// ---- Floating point ----
using float32 = float;
using float64 = double;

// ---- Size and pointer-related ----
using usize = std::size_t;
using isize = std::ptrdiff_t;

// ---- Character types ----
using char8 = ::char8;
using char16 = ::char16;
using char32 = ::char32;

// ---- Boolean alternatives ----
using bool8 = ::bool8; // Optional compact bool

// ---- Aliases for readability (optional) ----
using byte = uint8;

enum class DeterminismMode : u8
{
    Unknown = 0,
    Off,
    Replay,
    Strict
};

enum class ThreadSafetyMode : u8
{
    Unknown = 0,
    ExternalSync,
    ThreadSafe
};
} // namespace dng


// =============================
// Legacy internal macros (prefer DNG_* in new code)
// =============================

#ifndef DE_API
#define DE_API
#endif

#ifndef DE_FORCEINLINE
#define DE_FORCEINLINE DNG_FORCEINLINE
#endif

#ifndef DE_NOINLINE
#define DE_NOINLINE DNG_NOINLINE
#endif

#ifndef DE_ALIGN
#define DE_ALIGN(x) alignas(x)
#endif

#ifndef DE_NULL
#define DE_NULL DNG_NULL
#endif
