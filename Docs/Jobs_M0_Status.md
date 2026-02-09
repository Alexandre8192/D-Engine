# Jobs M0 Status

This document captures the jobs subsystem state at milestone M0. It reflects only the components that currently exist: the jobs contract, the Null backend, the JobsSystem orchestrator, and the associated tests.

## Current Components
- **Core/Contracts**
  - [Source/Core/Contracts/Jobs.hpp](Source/Core/Contracts/Jobs.hpp)
    - Defines POD types `JobHandle`, `JobCounter`, `JobsCaps`, `JobDesc`, and `ParallelForBody` with `noexcept` function pointer signatures.
    - Declares the `JobsBackend` concept plus the dynamic interface (`JobsVTable`, `JobsInterface`) and helpers (`SubmitJob`, `SubmitJobs`, `ParallelFor`, `WaitForCounter`, `QueryCaps`).
- **Core/Jobs**
  - [Source/Core/Jobs/NullJobs.hpp](Source/Core/Jobs/NullJobs.hpp)
    - Deterministic backend executing all jobs inline on the calling thread. Tracks simple stats (`submitCalls`, `submitBatchCalls`, `parallelForCalls`, `jobsExecuted`).
    - `Submit`/`SubmitBatch` increment counters, execute functions immediately, and reset the provided `JobCounter` to complete. `ParallelFor` walks indices sequentially.
    - `MakeNullJobsInterface` adapts an instance into a `JobsInterface`.
  - [Source/Core/Jobs/JobsSystem.hpp](Source/Core/Jobs/JobsSystem.hpp)
    - Defines `JobsSystemBackend` (`Null`, `External`), `JobsSystemConfig`, and `JobsSystemState` (owns a `JobsInterface`, inline `NullJobs`, `isInitialized`).
    - `InitJobsSystem` wires the inline Null backend; `InitJobsSystemWithInterface` validates required v-table entries (`submit`, `submitBatch`, `wait`) for external injection. Forwarders expose `SubmitJob`, `SubmitJobs`, `ParallelFor`, and `WaitForCounter` without allocations.

## Guarantees
- Header-first, `noexcept`, no exceptions/RTTI, and no allocations at the contract boundary.
- All public types are POD/trivially copyable and use non-owning pointers for job data.
- Dynamic interface pattern (concept + v-table + wrapper) matches other subsystems, enabling external backends to plug in without ownership transfer.
- Null backend is deterministic, allocation-free, and executes work inline; counters complete immediately after execution.
  Counters in M0 act purely as completion tokens: Null executes inline and leaves counters zeroed after the work finishes (including fallback ParallelFor).

## Non-Goals (M0)
- No real threading, worker pools, priorities, or affinity.
- No work stealing, fiber scheduling, or continuation graphs.
- No batching optimizations beyond simple loops; ParallelFor in Null is sequential.

## Tests
- Header-only self-containment: [tests/SelfContain/Jobs_header_only.cpp](tests/SelfContain/Jobs_header_only.cpp) includes the contract alone and `static_assert`s POD and concept conformance.
- Smoke helper: [tests/Smoke/Subsystems/Jobs_smoke.cpp](tests/Smoke/Subsystems/Jobs_smoke.cpp) initializes the JobsSystem with Null, submits individual and batched jobs that mutate counters, runs `ParallelFor`, verifies counters complete, and returns 0 on success (no `main`).

## Milestone Definition: Jobs M0
Jobs M0 is considered complete when:
- The jobs contract compiles standalone and passes self-contain/static concept assertions.
- NullJobs satisfies `JobsBackend`, executes jobs deterministically inline, and is wrapped by `JobsInterface`.
- JobsSystem can instantiate the Null backend or accept an injected backend via `InitJobsSystemWithInterface`, validating required function pointers.
- Smoke and header-only tests build cleanly in Debug/Release with warnings treated as errors.
- No hidden allocations, exceptions, or RTTI exist in this layer; ownership semantics are explicit and documented.
