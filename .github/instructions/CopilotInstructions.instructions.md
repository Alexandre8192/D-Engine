## D-Engine — Copilot / AI contributor guidance
**Purpose for AI Contributors (Copilot / GPT):**
You are contributing to **D-Engine**, a modern, header-first, STL-inspired C++23/26 engine built **for programmers only** — no editor layer, no scripting layer, no Blueprint-style abstraction.
The mission is to create a **transparent, deterministic, and ridiculously fast** codebase that is both educational and production-ready.  
Every line must *teach* something about modern C++ while remaining *runnable on a toaster*.

---

### Core Writing Rules
Comment **everything**.
Each struct, template, or function must include:
- **Purpose:** What the code does, in plain and direct English.
- **Contract:** Inputs, outputs, ownership rules, thread-safety guarantees.
- **Notes:** Edge cases, rationale, trade-offs, and potential pitfalls.
- When in doubt, **over-comment**.
- Write as if explaining to advanced C++ developers reading the code for learning purposes.
- Always justify *why* a choice was made, not just *how* it works.

---

### Coding Conventions
- Use the `dng::` namespace and `DNG_` macro prefix.
- Apply `[[nodiscard]]`, `constexpr`, and `noexcept` by default.
- Use `NormalizeAlignment`, `AlignUp`, and `IsPowerOfTwo` from `Alignment.hpp` — never re-implement alignment math.
- Guard risky or platform-dependent code with `DNG_CHECK` or `DNG_ASSERT`.
- Headers must be side-effect-free and include only what they need.
- Never introduce new STL or third-party dependencies unless unavoidable and approved.

---

### Design Philosophy
- **Header-first, dependency-free.** Every header must compile standalone.
- **Predictable memory behavior.** The same (size, alignment) must be passed to free as was passed to alloc.
- **Explicit thread-safety.** Every API must declare whether it’s thread-safe.
- **Compile-time validation.** Prefer `static_assert` over runtime guards.
- **Transparency over brevity.** Readability beats terseness. Nothing should feel “magical”.

---

### Performance Philosophy
- **Zero abstraction overhead.** Favor direct data access, contiguous layouts, and predictable branching.
- **Allocator-driven design.** All engine containers route through custom allocators (`AllocatorAdapter`, `ArenaAllocator`, `TrackingAllocator`, etc.).
- **Cache locality > O-notation purity.**
- **No hidden runtime.** No garbage collector, no reflection system, no virtualized dispatch unless explicit.
- **Runs on toasters.** Performance is a first-class design constraint — every subsystem must justify its memory and CPU cost.

---

### Testing and Examples
- No runtime unit-test framework yet.
- Add **compile-time smoke tests** in `tests/` for every new header and comment what each validates.
- Prefer **constexpr examples** or minimal runtime snippets illustrating usage.
- Manual perf/bench harnesses are acceptable if header-only.

---

### Reference Architecture
- **Big Picture:** Header-first, STL-inspired C++23/26 engine; almost all code lives in headers under `Source/Core/**`.  
  `.cpp` files are reserved for unavoidable translation-unit separation (e.g., global `new/delete` routing).
- **Core Entry Point:** `Source/Core/CoreMinimal.hpp` aggregates the always-safe headers  
  (platform flags, types, logging, timer, alignment, allocator).
- **Platform Layer:** `Source/Core/Platform/{PlatformDefines,PlatformCompiler,PlatformTypes,PlatformMacros}.hpp`  
  contain OS/arch/compiler detection and typedefs. Never include runtime code there.
- **Diagnostics:**  
  - `Diagnostics/Check.hpp` → defines `DNG_CHECK` / `DNG_VERIFY` (no-op in release).  
  - `Logger.hpp` → C++23 `std::print`-based backend + `DNG_ASSERT`.  
  - Keep lightweight stub macros so headers stay include-safe without hard deps.
- **Memory System:**  
  - `Allocator.hpp` defines `IAllocator` and `AllocatorRef`.  
  - All allocators normalize alignment and obey the “same (size, alignment) on free” rule.  
  - Use `DNG_MEM_CHECK_OOM` and `DNG_ASSERT` for diagnostics.  
  - Examples: `DefaultAllocator` (system), `ArenaAllocator` (bump/marker),  
    `StackAllocator` (LIFO markers), `TrackingAllocator` (stats/leak tracking).
- **Configuration:** `MemoryConfig.hpp` centralizes toggles like  
  `DNG_MEM_TRACKING`, `DNG_MEM_FATAL_ON_OOM`, `DNG_MEM_PARANOID_META`, `DNG_MAX_REASONABLE_ALIGNMENT`.  
  Prefer compile-time constants over runtime flags.
- **Threading Policy:** `ThreadSafety.hpp` defines the mutex wrappers and compile-time policies (`DNG_MEM_THREAD_SAFE`, `DNG_MEM_THREAD_POLICY`).
- **Logging:** Prefer `DNG_LOG_*("Memory", ...)`-style macros. Guard expensive formatting behind `Logger::IsEnabled()`.
- **Allocator Patterns:** `AllocatorRef::New/NewArray` guard overflows and respect type alignment.  
  `StackAllocator` intentionally ignores `Deallocate()` — free via markers only.

---

### Include & Build Discipline
- Use engine-absolute includes (`"Core/Memory/Allocator.hpp"`).
- Keep headers self-contained and light; avoid transitive includes.
- Primary build target: **MSVC 2022** via `D-Engine.sln`.
msbuild "$PWD\D-Engine.sln" /p:Configuration=Debug /p:Platform=x64
- Must compile cleanly in **Debug** and **Release** on **MSVC**, **Clang**, and **GCC**.
- No platform-specific behavior unless wrapped in `#if DNG_PLATFORM_*`.

---

### Future Subsystems (when ready)
- **FrameAllocator:** transient per-frame memory rings.
- **Job System:** lock-free work-stealing with allocator tagging.
- **Math Library:** SIMD-aware vectors/matrices/quaternions.
- **Bench Harness:** header-only micro-benchmark layer.
- **flat_map / small_vector:** cache-friendly containers built atop `AllocatorAdapter`.

---

### In Short
Copilot must behave like a **C++ instructor building a performance-oriented engine.**
Every contribution must:
1. Teach and justify design choices.  
2. Respect deterministic memory and threading policies.  
3. Favor raw performance over syntactic sugar.  
4. Remain transparent, auditable, and self-contained.

---

**End of file**
