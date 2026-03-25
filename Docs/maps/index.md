# Architecture Maps

> Navigation map. Normative rules live in the handbook and policy headers.

These pages are intentionally curated.
They show the relationships the project wants to preserve, not every accidental
include or every call edge in the codebase.

## Reading guide

Use these pages to answer a few fast questions:

- What is the role of this subsystem?
- Where is the contract boundary?
- Which system or loader owns lifecycle?
- Which backend is the reference implementation?
- Which tests, smokes, or benches keep the subsystem honest?

## Map set

- `Landscape`: project-wide orientation across Core, Modules, tests, tools, bench, and external examples
- `Containers`: top-level repository and runtime boundaries
- `ABI and Modules`: stable C ABI, generic module catalogue, typed window lookup, and module smokes
- `Memory`: config, lifecycle, allocator families, stress coverage, and bench coverage
- `Renderer M0`: contract, null backend, injected backend path, and smoke coverage
- `Audio`: contract, null backend, WinMM path, clip loading, and playback validation
- `Jobs`: contract, null backend, system facade, and deterministic crowd integration
- `Time`: contract, null backend, system facade, runtime ticking, and replay-oriented validation

## Reading rule

If a map and the handbook disagree, the handbook wins.
Treat the mismatch as a documentation bug.
