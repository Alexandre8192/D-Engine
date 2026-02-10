# Tests

This folder collects the compile-only and smoke helpers that keep the header-first promise honest.

## Layout
- SelfContain/: header-only translation units; no `main`; each includes a single public header to validate self-containment.
- Smoke/Subsystems/: subsystem smoke helpers (Window, Time, Jobs, Input, Audio, FileSystem, RendererSystem, BasicForwardRenderer, CoreRuntime); no `main`; consumed by the smoke aggregator or demos.
- Smoke/Memory/: allocator and memory-policy smokes; no `main`.
- Abi/: ABI conformance/interop compilation helpers.
- Policy/: policy-violation checks (expected-to-fail builds) that document guardrails.
- AllSmokes/: smoke aggregator entrypoint (`AllSmokes_main.cpp`) that calls subsystem smokes plus selected runtime memory smokes (Arena/Frame/Stack/SmallObject).
- Math/, AllSmokes/, and other folders: targeted math/unit coverage and aggregated smoke harness code.

## Running
- Build the solution (e.g., `msbuild D-Engine.sln /p:Configuration=Debug /p:Platform=x64`) to compile header-only and smoke helpers.
- Run the smoke aggregator target built from AllSmokes/AllSmokes_main.cpp to execute subsystem smokes and selected runtime memory smokes in one process.
