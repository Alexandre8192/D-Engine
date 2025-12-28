# D-Engine

D-Engine is a modern, header-first C++ engine project focused on:
- contracts first (backend-agnostic APIs)
- deterministic behavior (Null backends as reference)
- explicit costs (no hidden allocations at contract boundaries)
- clarity and documentation as a feature

This README is intentionally short. The single source of truth for architecture is the handbook.

## Quick links (read in this order)

1) Handbook (architecture, contracts, ABI, interop):
- D-Engine_Handbook.md

2) Docs index (map of all docs):
- Docs/INDEX.md

3) Core rules (non-negotiables):
- Docs/LanguagePolicy.md

4) ABI interop rules:
- Docs/ABI_Interop_Policy.md
- Docs/ABI_Review_Checklist.md

5) Subsystem status (M0):
- Docs/Window_M0_Status.md
- Docs/Renderer_M0_Status.md
- Docs/Time_M0_Status.md
- Docs/Jobs_M0_Status.md
- Docs/Input_M0_Status.md
- Docs/FileSystem_M0_Status.md

## What is D-Engine (in one paragraph)

D-Engine is a set of well-defined building blocks. Each subsystem starts as a Contract (types + invariants + call surface), then provides a deterministic Null backend plus a System orchestrator. Backends are interchangeable. A separate C ABI layer exists for loadable modules and for future cross-language backends.

## Repository layout

- Source/Core/Contracts/
  - Backend-agnostic subsystem contracts (C++ headers).
- Source/Core/<Subsystem>/
  - System orchestrators and Null backends (reference implementations).
- Source/Core/Abi/
  - Stable C ABI headers (vtable-in-C shape, status codes, POD-only).
- Source/Core/Interop/
  - C++ helpers to load and use ABI modules.
- Source/Modules/
  - Optional modules (including loadable examples).
- tests/
  - Header self-containment checks and smoke/build-only tests.
- Docs/
  - Policies, status docs, vision docs, and scope notes.

## Build (Windows, Visual Studio 2022)

This repo ships a Visual Studio solution:
- Open: D-Engine.sln
- Build with MSVC (the project uses /std:c++latest)

Notes about configurations (current state of the repo):
- x64 configurations are used primarily for "build-only" validation (compiles a large set of translation units, including header-only checks).
- Win32 configurations may build a runnable console target (for example a small demo).

If you want a fast sanity check:
- Build Release|x64
- Confirm the build completes warning-free (or with expected known warnings, if any)

## How to start reading the code

If you want the main mental model:
- Read D-Engine_Handbook.md

If you want to see the pattern on a concrete subsystem:
- Contract: Source/Core/Contracts/Window.hpp
- Null backend: Source/Core/Window/NullWindow.hpp
- System: Source/Core/Window/WindowSystem.hpp

If you want to see the ABI foundation:
- Base ABI types: Source/Core/Abi/DngAbi.h
- Module entrypoint: Source/Core/Abi/DngModuleApi.h
- Subsystem table (example): Source/Core/Abi/DngWindowApi.h

## Contributing (rules)

Before changing Core code, read:
- Docs/LanguagePolicy.md
- Docs/HeaderFirstStrategy.md
- Docs/ABI_Interop_Policy.md (if you touch ABI)

## License

See:
- LICENSE.txt
- EULA.txt

## Status

Current scope and progress:
- Docs/D-Engine_v0.1_Scope.md
- Docs/Progress_Summary_v0.1.md
- CHANGELOG.md
