# Rust Null Window Module (ABI v1)

Purpose
- Minimal Rust cdylib that implements the Window ABI v1 for D-Engine.
- Exposes the symbol `dngModuleGetApi_v1` and follows the same invariants as the C++ NullWindow module.

Build
- Requires Rust stable (MSVC toolchain on Windows for ModuleSmoke).
- From this directory:
  - `cargo build --release`
- Output files:
  - Windows: `target/release/rust_null_window_module.dll`
  - Linux: `target/release/librust_null_window_module.so`
  - macOS: `target/release/librust_null_window_module.dylib`
- To use with ModuleSmoke, copy/rename the built library to the expected name (e.g., `NullWindowModule.dll` on Windows) next to where ModuleSmoke loads modules.

Notes
- Uses `catch_unwind` to map any panic to `DNG_STATUS_FAIL` (no unwinding across the ABI).
- Validates title string views: `size > 0` requires non-NULL `data`; `size == 0` accepts NULL data.
- Validates reserved flags: `desc->flags` must be zero in v1.
- Uses host `alloc`/`free` for context and title storage; frees everything in `shutdown`.
