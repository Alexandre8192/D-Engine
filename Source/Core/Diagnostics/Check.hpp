#pragma once
//
// D-Engine - Core/Diagnostics/Check.hpp
// Centralized lightweight diagnostics macros (no heavy deps).
//
// Provided:
//   - DNG_CHECK(cond): soft check (no-op in Release). In Debug, optional breakpoint.
//   - DNG_VERIFY(cond): always evaluates cond; in Debug can optionally break.
//
// Not provided here (to avoid clashes with Logger.hpp):
//   - DNG_ASSERT(...) -> lives in Logger.hpp (rich formatting).
//
// Optional toggles (define before including this header):
//   - DNG_CHECK_BREAK   : if defined, DNG_CHECK will break in Debug when cond fails
//   - DNG_VERIFY_BREAK  : if defined, DNG_VERIFY will break in Debug when cond fails
//   - DNG_DEFINE_MINIMAL_ASSERT : defines a minimal DNG_ASSERT fallback if not already defined
//

// ----------------------------------------------------------------------------
// Debug detection (respects user-defined DNG_DEBUG; falls back to !NDEBUG)
// ----------------------------------------------------------------------------
#ifndef DNG_DEBUG
#  ifndef NDEBUG
#    define DNG_DEBUG 1
#  else
#    define DNG_DEBUG 0
#  endif
#endif

// ----------------------------------------------------------------------------
 // Internal cross-compiler debug break helper (Debug only)
// ----------------------------------------------------------------------------
#if DNG_DEBUG
#  if defined(_MSC_VER)
#    define DNG_INTERNAL_DEBUG_BREAK() __debugbreak()
#  elif defined(__clang__) || defined(__GNUC__)
#    define DNG_INTERNAL_DEBUG_BREAK() __builtin_trap()
#  else
#    include <cstdlib>
#    define DNG_INTERNAL_DEBUG_BREAK() std::abort()
#  endif
#else
#  define DNG_INTERNAL_DEBUG_BREAK() ((void)0)
#endif

// ----------------------------------------------------------------------------
// DNG_CHECK: soft check
//  - Release: no-op
//  - Debug:   by default no break (non-intrusive); define DNG_CHECK_BREAK to break
// ----------------------------------------------------------------------------
#ifndef DNG_CHECK
#  if DNG_DEBUG
#    ifdef DNG_CHECK_BREAK
#      define DNG_CHECK(cond) do { if(!(cond)) { DNG_INTERNAL_DEBUG_BREAK(); } } while(0)
#    else
#      define DNG_CHECK(cond) do { if(!(cond)) { /* optional breakpoint in debug */ } } while(0)
#    endif
#  else
#    define DNG_CHECK(cond) ((void)0)
#  endif
#endif

// ----------------------------------------------------------------------------
// DNG_VERIFY: like assert but always evaluates `cond`
//  - Release: evaluates cond for side effects, no break
//  - Debug:   define DNG_VERIFY_BREAK to break on failure
// ----------------------------------------------------------------------------
#ifndef DNG_VERIFY
#  if DNG_DEBUG
#    ifdef DNG_VERIFY_BREAK
#      define DNG_VERIFY(cond) do { if(!(cond)) { DNG_INTERNAL_DEBUG_BREAK(); } } while(0)
#    else
#      define DNG_VERIFY(cond) ((void)(cond))
#    endif
#  else
#    define DNG_VERIFY(cond) ((void)(cond))
#  endif
#endif

// ----------------------------------------------------------------------------
 // Optional minimal assert fallback (kept OFF by default)
//   Define DNG_DEFINE_MINIMAL_ASSERT before including this header to enable.
//   If Logger.hpp already defines DNG_ASSERT, this block is skipped.
// ----------------------------------------------------------------------------
#if defined(DNG_DEFINE_MINIMAL_ASSERT) && !defined(DNG_ASSERT)
#  if DNG_DEBUG
#    define DNG_ASSERT(cond) do { if(!(cond)) { DNG_INTERNAL_DEBUG_BREAK(); } } while(0)
#  else
#    define DNG_ASSERT(cond) ((void)0)
#  endif
#endif
