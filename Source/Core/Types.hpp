#pragma once

#include <cstddef> // std::size_t, std::ptrdiff_t

#include "Core/Platform/PlatformTypes.hpp"

// =============================
// Types.hpp
// =============================
// Canonical engine-facing type facade over the platform-backed type layer.
// Core/Platform owns the low-level source of truth; engine code should consume
// types from namespace dng instead of relying on platform-layer internals.
// =============================

namespace dng
{
// ---- Integer types ----
using int8 = ::dng::platform::int8;
using int16 = ::dng::platform::int16;
using int32 = ::dng::platform::int32;
using int64 = ::dng::platform::int64;

using uint8 = ::dng::platform::uint8;
using uint16 = ::dng::platform::uint16;
using uint32 = ::dng::platform::uint32;
using uint64 = ::dng::platform::uint64;

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
using uptr = ::dng::platform::UPTRINT;
using iptr = ::dng::platform::PTRINT;

// ---- Character types ----
using char8 = ::dng::platform::char8;
using char16 = ::dng::platform::char16;
using char32 = ::dng::platform::char32;

// ---- Boolean alternatives ----
using bool8 = ::dng::platform::bool8; // Optional compact bool

// ---- Aliases for readability (optional) ----
using byte = uint8;

template <typename T32, typename T64>
using IntPtrT = ::dng::platform::IntPtrT<T32, T64>;

inline constexpr bool Is64Bit = ::dng::platform::DNG_Is64Bit;
inline constexpr bool Is32Bit = ::dng::platform::DNG_Is32Bit;
inline constexpr int PointerBits = ::dng::platform::DNG_PointerBits;

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
