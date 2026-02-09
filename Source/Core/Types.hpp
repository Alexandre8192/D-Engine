#pragma once

#include <cstdint>  // int32_t, uint32_t...
#include <cstddef>  // size_t, ptrdiff_t

// =============================
// Types.hpp
// =============================
// This header defines fixed-size and platform-consistent types
// used across the engine. All modules should rely on these aliases
// instead of raw native types like 'int' or 'long'.
// =============================

namespace dng
{
// ---- Integer types ----
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

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
using char8 = char;
using char16 = char16_t;
using char32 = char32_t;

// ---- Boolean alternatives ----
using bool8 = uint8; // Optional compact bool

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
// Internal macros (use only in engine internals)
// =============================

#ifndef DE_API
#define DE_API
#endif

#ifndef DE_FORCEINLINE
#define DE_FORCEINLINE inline __attribute__((always_inline))
#endif

#ifndef DE_ALIGN
#define DE_ALIGN(x) alignas(x)
#endif

#ifndef DE_NULL
#define DE_NULL nullptr
#endif
