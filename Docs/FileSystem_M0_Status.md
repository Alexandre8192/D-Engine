# FileSystem M0 Status

> [!WARNING]
> Historical snapshot: this document describes milestone M0 status at the time it was written and may not match current code.
> For current behavior, see `Docs/Implementation_Snapshot.md`, `D-Engine_Handbook.md`, and `tests/README.md`.

This document captures the file system subsystem state at milestone M0. It reflects only the components that currently exist: the file system contract, the Null backend, the FileSystemSystem orchestrator, and the associated tests.

## Current Components
- **Core/Contracts**
  - `Source/Core/Contracts/FileSystem.hpp`
    - Defines `PathView` (non-owning char view) and `FsStatus` result codes.
    - Declares the `FileSystemBackend` concept with `Exists`, `FileSize`, and `ReadFile`, all `noexcept` and allocation-free at the contract edge.
    - Provides the dynamic interface: `FileSystemVTable`, `FileSystemInterface`, and helpers (`Exists`, `FileSize`, `ReadFile`). `MakeFileSystemInterface` adapts any backend satisfying the concept without taking ownership.
- **Core/FileSystem**
  - `Source/Core/FileSystem/NullFileSystem.hpp`
    - Deterministic backend that always returns `FsStatus::NotFound`, leaves buffers untouched, and sets output sizes to zero. No allocations, no logging.
    - `MakeNullFileSystemInterface` wraps an instance into a `FileSystemInterface`.
  - `Source/Core/FileSystem/FileSystemSystem.hpp`
    - Defines `FileSystemSystemBackend` (`Null`, `External`), `FileSystemSystemConfig`, and `FileSystemSystemState` (owns `FileSystemInterface`, inline `NullFileSystem`, `isInitialized`).
    - `InitFileSystemSystem` resets state, instantiates the inline Null backend, and validates injected interfaces in `InitFileSystemSystemWithInterface` (requires `userData` and all vtable functions).
    - Forwards `Exists`, `FileSize`, and `ReadFile` through the active interface while remaining allocation-free.

## Guarantees
- Header-first, no exceptions, no RTTI, no allocations in the contract or system layer.
- All public structs (`PathView`) and enums (`FsStatus`) are POD/trivially copyable; API uses raw views, not owning strings.
- `FileSystemBackend` concept enforces `noexcept` signatures and stable shapes.
- Null backend is deterministic and side-effect free; system validation rejects incomplete interfaces.

## Non-Goals
- No write operations (create/delete) at M0.
- No directory iteration, globbing, or virtual mount layers.
- No platform-specific features (timestamps, permissions, symlinks) and no OS headers in the contract.
- No asynchronous IO or buffering layer.

## Tests
- Header-only compile check: `tests/SelfContain/FileSystem_header_only.cpp` (`static_assert` on `FileSystemBackend`, interface usage, no main).
- Smoke helper: `tests/Smoke/Subsystems/FileSystem_smoke.cpp` (`RunFileSystemSmoke()` initializes the system with the Null backend and checks deterministic `NotFound` responses and zeroed outputs; no main).

## Milestone Definition: FileSystem M0
FileSystem M0 is considered complete when:
- The file system contract compiles standalone and passes self-contain/static concept assertions.
- NullFileSystem satisfies `FileSystemBackend`, remains deterministic, and is wrapped by `FileSystemInterface`.
- FileSystemSystem can instantiate the Null backend or accept an injected external backend via `InitFileSystemSystemWithInterface`, validating required function pointers.
- Header-only and smoke tests build cleanly in Debug/Release with warnings treated as errors.
- No hidden allocations, exceptions, or RTTI exist in this layer; ownership and error signaling are explicit and documented.
