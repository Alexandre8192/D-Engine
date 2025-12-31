# Policy Matrix

| Policy | Scope | Requirement (one-liner) | Enforcement | Link |
| --- | --- | --- | --- | --- |
| Language Policy | Core C++ usage | Core uses C++23/26 with no exceptions/RTTI, allocator-aware STL only, fatal OOM for global new (nothrow returns nullptr), std::malloc/free allowed only in GlobalNewDelete, ABI host table `free` name is not CRT free | CI flags + review | [Docs/LanguagePolicy.md](Docs/LanguagePolicy.md) |
| ABI Interop Policy | Cross-language ABI boundary | ABI is C-only tables + POD data; no unwind; explicit ownership and versioned v1+ | Review + ABI smoke tests | [Docs/ABI_Interop_Policy.md](Docs/ABI_Interop_Policy.md) |
| ABI Review Checklist | ABI change gate | Every ABI change must satisfy checklist items A–G (shape, unwind, layout, ownership, versioning, determinism, tests) | Review checklist | [Docs/ABI_Review_Checklist.md](Docs/ABI_Review_Checklist.md) |
| Header-First Strategy | Public headers/build hygiene | Contracts live in self-contained headers; heavy templates stay in detail/ and explicit instantiations in .cpp | Header self-contain tests + review | [Docs/HeaderFirstStrategy.md](Docs/HeaderFirstStrategy.md) |
| Bench Protocol | Benchmark harness | Bench runs pin threads/affinity, apply scenario overrides, and enforce RSD targets with JSON schema v1 | Bench runner + workflow gating | [Docs/BenchProtocol.md](Docs/BenchProtocol.md) |
| Determinism Policy | Simulation determinism | Replay mode uses deterministic time/RNG and stable ordering; no nondeterministic sources in simulation paths | Replay tests + review | [Docs/DeterminismPolicy.md](Docs/DeterminismPolicy.md) |
| Threading Rules | Parallel simulation work | Jobs write to private lanes and merge deterministically; forbid timing/order-dependent patterns in Replay | Replay tests + review | [Docs/ThreadingRules.md](Docs/ThreadingRules.md) |
| Time Policy | Time sources/ticks | Simulation uses fixed-step SimulationClock; RealClock only for rendering/tools; command buffers per tick | Design review + time-loop tests | [Docs/TimePolicy.md](Docs/TimePolicy.md) |
| Handbook Non-negotiables | Repository-wide principles | Header-first contracts, explicit ownership, determinism focus, no hidden allocations, Purpose/Contract/Notes expected | Code review + header smoke tests | [D-Engine_Handbook.md](D-Engine_Handbook.md) |
| Copilot Guardrails | AI contributor rules | Honor performance-first golden rules: no exceptions/RTTI/new in Core, engine allocators only, document Purpose/Contract/Notes, keep Core predictable | Maintainer review | [.github/copilot-instructions.md](.github/copilot-instructions.md) |
