# Header-First, Fast Build Strategy

**Objective**: Maintain **contracts in self-contained headers** (readable, testable) while **confining heavy implementation details** (templates, massive includes) outside the public inclusion cone.

## 1. The "Thin Facade" Pattern

*   **`Public.hpp`**: Declares only the contract (PODs, handles, spans, small inline functions). Avoid exposing private template helpers here.
*   **`detail/*.hpp`**: Contains heavy templates and implementation details. These are **NEVER** included by other modules, only by the module's own `.cpp`.
*   **`detail/*.inl`** (optional): Inline implementation chunks included from the module `.cpp` or a private TU, never from public headers.


## 2. Explicit Instantiation

For templates with a finite set of combinations, use `extern template` to prevent re-instantiation in every translation unit. Never expose the heavy template definition in public headers; keep it in `detail/` or a `.cpp`.


**Public Header (`Public.hpp`):**
```cpp
namespace dng {
    template<class T>
    void ProcessPodArray(Span<const T> in, Span<T> out) noexcept;

    // Prevent instantiation in consumer TUs
    extern template void ProcessPodArray<float>(Span<const float>, Span<float>) noexcept;
}
```

**Implementation (`Module.cpp`):**
```cpp
#include "Public.hpp"
#include "detail/HeavyAlgo.hpp" // Heavy templates live here

namespace dng {
    // Explicit instantiation: compiled once, linked everywhere
    template void ProcessPodArray<float>(Span<const float>, Span<float>) noexcept;
}
```

## 3. Build Hygiene

*   **Self-Contained Headers**: Every public header must compile in isolation; use `tests/SelfContain/*.cpp` to enforce.
*   **IWYU (Include What You Use)**: No transitive includes. Each header includes exactly what it needs.
*   **PCH (Precompiled Headers)**: Use a lightweight PCH for platform types and logging macros.
*   **Tools**: Enable `sccache`/`ccache` and Ninja for rapid iteration.


## 4. Template vs. V-Table Policy

*   **Internal Template (`detail/`)**: Use when aggressive inlining is needed, types are known/finite, or for hot paths.
*   **Small Public V-Table**: Use to **stabilize the header** and **limit recompilation** when the strategy changes.
*   **Concept/CRTP**: Keep in `detail/` or limit to the contract layer where already established; do **not** expose in public if it multiplies instantiations.
*   **ABI surface**: Prefer small v-tables or C ABI tables for stability; keep templates out of ABI headers.


## Checklist for New Modules

**Public API**
- [ ] Headers are **self-contained** (enforced by `tests/SelfContain`).
- [ ] **No** heavy dependencies (e.g., no `<vector>` in public API).
- [ ] Functions are **non-template** where possible.
- [ ] **Extern template** used for finite combinations.
- [ ] **POD/views/handles** only; no implementation in public.
- [ ] No `detail/` includes from public headers.

**Implementation**
- [ ] `detail/*.hpp` or `.inl` used for internal templates/CRTP.
- [ ] Module `.cpp` handles instantiations.
- [ ] Heavy `constexpr` logic moved to dedicated TUs (generated tables).
- [ ] ABI headers keep C layout stable (append-only).

