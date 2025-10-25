[![CI — build-matrix](https://github.com/Alexandre8192/D-Engine/actions/workflows/ci.yml/badge.svg)](https://github.com/Alexandre8192/D-Engine/actions/workflows/ci.yml)

# Core/Platform/ - Platform Layer Overview

This directory contains all platform-related abstractions for D Engine.
Each file is focused on a specific responsibility, ensuring clarity, portability, and modularity.

## File Responsibilities

### `PlatformDefines.hpp`
- Detects the **target operating system** (Windows, Linux, Apple...)
- Detects the **CPU architecture** (32-bit, 64-bit)
- Defines flags like `DNG_PLATFORM_WINDOWS`, `DNG_PLATFORM_64BITS`, etc.
- Purely based on preprocessor `#define`s — **no includes** or runtime logic

### `PlatformCompiler.hpp`
- Detects the **compiler** (MSVC, Clang, GCC)
- Defines macros for:
  - `DNG_FORCEINLINE`, `DNG_FORCENOINLINE`
  - `DNG_DLLIMPORT`, `DNG_DLLEXPORT`
  - `DNG_ALIGNAS(n)` for memory alignment
  - `DNG_RESTRICT` keyword abstraction
- Makes the codebase **cross-compiler safe**

### `PlatformTypes.hpp`
- Declares all **fixed-width integer types** like `int32`, `uint64`
- Defines pointer-sized types (`SIZE_T`, `PTRINT`, etc.) for memory and low-level operations
- Includes optional aliases like `bool8`, `char16`, etc.

### `PlatformMacros.hpp`
- Declares **general-purpose macros** used across all modules:
  - `DNG_UNUSED(x)`
  - `DNG_CHECK(x)` for runtime assertions
  - `DNG_LIKELY(x)` / `DNG_UNLIKELY(x)` for branch prediction
  - `DNG_STATIC_ASSERT` and tagging helpers like `DNG_TODO()`
- Lightweight and runtime-safe


## Design Philosophy

- **Each file serves one purpose only**
- **No namespace pollution** (everything prefixed with `DNG_`)
- **Safe to include in any header or `.cpp` file**
- **No runtime logic** in the platform layer — only constants, types, and macros
- Files are composable via `CoreMinimal.h`


## Next Steps (for future platform work)
- Add `PlatformWindows.hpp`, `PlatformLinux.hpp` etc. if platform-specific code becomes necessary
- Introduce `DNG_PLATFORM_HAS_FEATURE_X` flags per OS/Compiler
- Create build system integration to define or inject platform flags
