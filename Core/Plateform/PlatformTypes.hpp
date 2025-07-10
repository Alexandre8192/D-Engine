// =============================
// PlatformTypes.hpp
// =============================
#pragma once

// This file defines platform-safe integer types and pointer-sized types.
// It is meant to ensure consistent sizes across all platforms, including 32-bit and 64-bit targets.
// Should be included before any size-sensitive code.

#include <cstdint>   // for int*_t, uint*_t
#include <cstddef>   // for size_t, ptrdiff_t

// -----------------------------
// Fixed-width integer types
// -----------------------------
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

// -----------------------------
// Pointer-sized integer types
// -----------------------------
using SIZE_T = std::size_t;        // Unsigned type for memory sizes
using SSIZE_T = std::ptrdiff_t;     // Signed type for pointer differences
using UPTRINT = uintptr_t;          // Unsigned pointer-size integer
using PTRINT = intptr_t;           // Signed pointer-size integer

// -----------------------------
// Boolean convenience type
// -----------------------------
using bool8 = uint8; // Compact 8-bit boolean representation for binary formats

// -----------------------------
// Character type aliases (optional for encoding flexibility)
// -----------------------------
using char8 = char;
using char16 = char16_t;
using char32 = char32_t;

// Note: these aliases are NOT intended to override standard types like 'bool' or 'int'.
// They are for internal consistency and clarity in engine code dealing with serialization, memory layout, etc.
