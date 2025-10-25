## D-Engine — Copilot / AI contributor guidance
**Purpose for AI Contributors (Copilot / GPT):**
You are contributing to D-Engine, a modern, header-first, STL-inspired C++23/26 engine designed exclusively for programmers.
Every line of code must be transparent, educational, and deterministic — the goal is to build a fully auditable codebase that teaches as much as it performs.
**Core Writing Rules**
Comment everything. Every struct, template, and function should include:
- Purpose: what the code does in plain English.
- Contract: expectations (input, output, ownership, thread safety).
- Notes: edge cases, rationale for chosen approach, and potential pitfalls.
- When in doubt, overcomment — verbosity is preferred over silence.
- Use technical and explanatory English, as if you were teaching advanced C++ developers.
- Add rationale for all design decisions — never just “how”, always “why”.
**Coding Conventions**
- Follow dng:: namespace and DNG_* macro naming.
- Use [[nodiscard]], constexpr, and noexcept by default.
- Always use NormalizeAlignment, AlignUp, and IsPowerOfTwo from Alignment.hpp.
- Wrap risky operations in DNG_CHECK or DNG_ASSERT.
- No hidden includes or side effects in headers.
- Never introduce new STL or external dependencies unless absolutely necessary.
**Design Philosophy**
- Header-first, dependency-free: every header must compile in isolation.
- Predictable memory behavior: same (size, alignment) on alloc/free.
- Thread-safety explicitness: always state if a function is thread-safe.
- Compile-time validation: prefer static_assert over runtime checks.
- Transparency over brevity: code must be understandable without prior context.
**Testing and Examples**
- No runtime test framework yet.
- If adding new subsystems, include minimal compile-time smoke tests under tests/ and comment what they validate.
- Prefer compile-time examples that illustrate how each class is meant to be used.
- **Big picture**: Header-first, STL-inspired C++23/26 game engine; almost everything is implemented in headers under `Source/Core/**`. `.cpp` files exist only when separation is unavoidable.
- **Core entry point**: `Source/Core/CoreMinimal.hpp` aggregates the always-safe headers (platform flags, types, logging, timer, alignment, allocator). Include it unless a file needs a thinner slice for compile-time reasons.
- **Platform layer**: `Source/Core/Platform/{PlatformDefines,PlatformCompiler,PlatformTypes,PlatformMacros}.hpp` expose OS/arch/compiler flags plus fixed-width/pointer-sized typedefs. Never add runtime logic or extra includes to these headers; they must stay preprocessor-only.
- **Diagnostics & logging**: `Source/Core/Diagnostics/Check.hpp` defines lightweight `DNG_CHECK/DNG_VERIFY` guards that degrade to no-ops in release builds. `Source/Core/Logger.hpp` implements the logging backend via C++23 `std::print` and also publishes the canonical `DNG_ASSERT`. Respect the logging macros already sprinkled through memory code (many headers provide stubs so they stay include-safe).
- **Memory contracts**: `Source/Core/Memory/Allocator.hpp` describes the allocator interface and the `AllocatorRef` helper. All allocators must normalize alignment via `NormalizeAlignment`, honor the "same (size, alignment) on free" rule, and use `DNG_MEM_CHECK_OOM`/`DNG_ASSERT` for diagnostics. Examples: `DefaultAllocator` (system wrapper with metadata header), `ArenaAllocator` (bump pointer + markers), `StackAllocator` (LIFO markers atop Arena), `TrackingAllocator` (stats/leak tracking controlled by `DNG_MEM_*` toggles).
- **Configuration toggles**: `Source/Core/Memory/MemoryConfig.hpp` centralizes compile-time switches (`DNG_MEM_TRACKING`, `DNG_MEM_FATAL_ON_OOM`, `DNG_MEM_PARANOID_META`, `DNG_MAX_REASONABLE_ALIGNMENT`, etc.) and exposes a runtime `MemoryConfig`. Check these before adding new behavior gates; prefer compile-time constants when possible.
- **Naming & style**: Namespaces live under `dng::`; macros use the `DNG_` prefix. Follow the banner separators (`// ---` and `// ===`), and every public type/function requires a Purpose/Contract/Notes header comment plus inline rationale for non-trivial logic, as reinforced by `.github/instructions/CopilotInstructions.instructions.md`.
- **Modern qualifiers**: Default to `[[nodiscard]]`, `constexpr`, and `noexcept` where appropriate. Use `static_assert` to lock invariants (alignment, struct sizes, enum ranges). Prefer compile-time checks over runtime fallbacks whenever feasible.
- **Include discipline**: Use engine-absolute includes (e.g., `"Core/Memory/Allocator.hpp"`). Keep headers self-contained and side-effect free; if you add helpers, place them in `Source/Core/**` rather than introducing new dependencies.
- **Threading policy**: `Source/Core/Memory/ThreadSafety.hpp` (and the toggles in `MemoryConfig.hpp`) govern mutex wrappers and thread-safe variants. When touching shared allocators, respect the existing policy macros (`DNG_MEM_THREAD_SAFE`, `DNG_MEM_THREAD_POLICY`).
- **Build workflow**: Primary target is MSVC via `D-Engine.sln`. Typical command (PowerShell): `msbuild "$PWD\D-Engine.sln" /p:Configuration=Debug /p:Platform=x64`. Ensure changes compile cleanly under both Debug and Release; Clang/GCC compatibility is expected for header-only code paths.
- **Testing stance**: Currently no automated runtime tests; most validation is via successful compilation and allocator usage smoke tests. If you add new components, supply minimal compile-only usage under `tests/` or document manual verification steps.
- **Alignment helpers**: `Source/Core/Memory/Alignment.hpp` exposes `NormalizeAlignment`, `AlignUp`, and `IsPowerOfTwo`; rely on these utilities instead of re-implementing math.
- **Logging usage**: Prefer the `DNG_LOG_*` macros with short literal categories (e.g., `"Memory"`), and gate expensive formatting behind `Logger::IsEnabled` if you need custom code paths.
- **Allocator patterns**: `AllocatorRef::New/NewArray` guard overflow and honor type alignment; mirror these patterns when adding typed helpers. `StackAllocator` ignores `Deallocate` on purpose—extend via markers instead of raw frees.
- **Fallback shims**: Several headers provide temporary `DNG_LOG_*` stubs so they remain include-safe without `Logger.hpp`; keep or extend those shims rather than introducing hard dependencies.
- **Project files**: If you introduce a rare `.cpp`, remember to register it in `D-Engine.vcxproj` and mirror any required filters; most contributions should stay header-only.
- **External surface**: Avoid pulling additional STL headers unless needed; every include impacts compile-time for downstream consumers who rely on `CoreMinimal.hpp` being lightweight.
- **Platform extensions**: When adjusting detection macros, update only `PlatformDefines.hpp`/`PlatformCompiler.hpp` with preprocessor logic—no runtime code or global state allowed there.
- **When in doubt**: Stay within the established memory/diagnostics patterns and consult the human maintainers for ABI-impacting changes, build matrix adjustments, or new subsystem introductions.

**In short**
Copilot must act like a C++ instructor contributing production-quality code:
Every addition should teach, justify, and explain the engine’s logic, not just implement it.
---
End of file
