// =============================
// PlatformTypes.hpp
// =============================
#pragma once

// Platform-stable scalar and pointer-sized types, plus sanity checks.
// D-Engine policy: **UTF-8 everywhere**, no TCHAR abstraction.

#include <cstdint>   // std::int*_t, std::uint*_t, std::intptr_t, std::uintptr_t
#include <cstddef>   // std::size_t, std::ptrdiff_t
#include "PlatformDefines.hpp" // for 32/64-bit checks

// -----------------------------
// Fixed-width integers
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
// Pointer-sized / size types
// -----------------------------
using UPTRINT = std::uintptr_t;	// Unsigned integer with pointer size
using PTRINT = std::intptr_t;	// Signed integer with pointer size
using SIZE_T = std::size_t;		// Unsigned size type (matches sizeof)
using SSIZE_T = std::ptrdiff_t;	// Signed size type

// -----------------------------
// Small bool & char aliases (optional)
// -----------------------------
using bool8 = uint8;		// Compact 8-bit boolean for serialization/binary blobs
using char8 = char;			// UTF-8 code units; use std::u8string if you prefer strong typing
using char16 = char16_t;	// UTF-16 code units
using char32 = char32_t;	// UTF-32 code units

// -----------------------------
// Sanity checks
// -----------------------------
static_assert(sizeof(uint8) == 1, "uint8 must be 1 byte");
static_assert(sizeof(uint16) == 2, "uint16 must be 2 bytes");
static_assert(sizeof(uint32) == 4, "uint32 must be 4 bytes");
static_assert(sizeof(uint64) == 8, "uint64 must be 8 bytes");

static_assert(sizeof(int8) == 1, "int8 must be 1 byte");
static_assert(sizeof(int16) == 2, "int16 must be 2 bytes");
static_assert(sizeof(int32) == 4, "int32 must be 4 bytes");
static_assert(sizeof(int64) == 8, "int64 must be 8 bytes");

static_assert(sizeof(UPTRINT) == sizeof(void*), "UPTRINT must match pointer size");
static_assert(sizeof(PTRINT) == sizeof(void*), "PTRINT must match pointer size");
static_assert(sizeof(SIZE_T) == sizeof(void*), "SIZE_T must match pointer size or platform ABI");
static_assert(sizeof(SSIZE_T) == sizeof(void*), "SSIZE_T must match pointer size or platform ABI");

#if DNG_PLATFORM_64BITS
static_assert(sizeof(void*) == 8, "64-bit platform must have 8-byte pointers");
#else
static_assert(sizeof(void*) == 4, "32-bit platform must have 4-byte pointers");
#endif

// Endianness macros come from PlatformDefines.hpp:
// - DNG_PLATFORM_LITTLE_ENDIAN (1/0)
// - DNG_PLATFORM_BIG_ENDIAN    (1/0)

// Notes:
// - Prefer standard attributes directly (e.g., [[nodiscard]]).
// - D-Engine is UTF-8 by default; convert at boundaries as needed.
