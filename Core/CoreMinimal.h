// =============================
// CoreMinimal.h
// =============================
#pragma once

// This header is the minimal set of includes that any file in the engine can include safely.
// It is meant to avoid having to include every small file manually.
// Only headers with ZERO dependencies and stable purpose should be included here.
//
// Intent: Cross-platform, lightweight, no side effects, no runtime logic.

#include "Core/Platform/PlatformDefines.hpp"   // Defines platform flags (Windows, Linux, 64-bit, etc.)
#include "Core/Platform/PlatformCompiler.hpp"  // Detects compiler and defines FORCEINLINE etc.
#include "Core/Platform/PlatformTypes.hpp"     // Platform pointer-size-safe types (SIZE_T, PTRINT, etc.)
#include "Core/Platform/PlatformMacros.hpp"    // Likely, Unlikely, check/assert macros
#include "Core/Types.hpp"                      // Core typedefs (int32, float32, etc.)
#include "Core/Logger.hpp"                     // Basic logging system (safe for all modules)
#include "Core/Timer.hpp"                      // Simple stopwatch and timing utilities

// Other includes should be added only if they are minimal and always needed
