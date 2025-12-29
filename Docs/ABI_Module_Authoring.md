# ABI Module Authoring Guide (v1)

Purpose
- Help external authors (Rust, Zig, C, etc.) implement D-Engine modules against the stable C ABI.
- Keep the boundary predictable: C ABI only, POD-only types, explicit ownership, no unwinding.

What you build
- A dynamically loadable module that exports the v1 entrypoint: `dngModuleGetApi_v1`.
- The module fills a `dng_module_api_v1` table (metadata + subsystem tables).
- Each subsystem table follows the pattern: `{ header, ctx, function pointers }`.

Hard rules (non-negotiable)
- C ABI only. No C++ name mangling on the exported entrypoint.
- No exceptions/panics/unwinding across the boundary. Catch everything inside and return status codes.
- POD-only data. Do not add hidden allocations or virtual dispatch at the ABI surface.
- Status codes only: return `dng_status_v1` values (e.g., `DNG_STATUS_OK`, `DNG_STATUS_INVALID_ARG`).
- Ownership is explicit: whoever allocates frees. Use the host allocator callbacks (`alloc`/`free`) when provided.
- String views: `dng_str_view_v1.data` may be NULL only if `size == 0`; if `size > 0`, `data` must be non-NULL. Bytes are UTF-8 recommended; no implicit terminator.
- Header handshake: set `header.struct_size = sizeof(struct)` and `header.abi_version = DNG_ABI_VERSION_V1` for every ABI struct you expose.
- Function tables: all required function pointers must be non-NULL; `ctx` must be non-NULL when the table contract requires a context.
- Reserved fields: must be zero in v1 (e.g., `dng_window_desc_v1.flags == 0`).

Minimal implementation steps
- Export `dngModuleGetApi_v1` with C linkage and the ABI calling convention (`DNG_ABI_CALL`).
- Validate the incoming `dng_host_api_v1` (version, struct_size, non-NULL alloc/free) before using it.
- Allocate your module context (if needed) via `host->alloc` and store it in `ctx` fields of your tables.
- Fill `dng_module_api_v1` and nested tables with sizes/versions set and function pointers assigned.
- Provide a `shutdown` function if you allocate a context; free with the same allocator and align/size.

Author checklist
- [ ] `dngModuleGetApi_v1` exported with C ABI and `DNG_ABI_CALL`.
- [ ] All ABI headers included from the SDK; no private headers leaked.
- [ ] All struct headers set: `struct_size` and `abi_version` match the exact struct.
- [ ] All required function pointers are non-NULL; `ctx` populated where required.
- [ ] No exceptions/panics escape; status codes returned for all failures.
- [ ] Strings follow `dng_str_view_v1` rules (UTF-8 recommended, NULL only when size==0).
- [ ] Reserved fields set to zero (e.g., window desc flags).
- [ ] Uses host `alloc`/`free` (or provides matching free) for any allocations.
- [ ] Provides `shutdown` when using dynamic context allocation.

Validation steps
- Build the SDK headers into your module.
- Run the D-Engine ABI header compile tests: `tests/Abi/AbiHeaders_c.c` and `tests/Abi/AbiHeaders_cpp.cpp`.
- Run the module smoke test: `tests/Abi/ModuleSmoke.cpp` (loads the sample NullWindow module and exercises the window API).
- Optional: add your own layout/static asserts similar to `tests/Abi/AbiLayout_v1.c` to pin sizes/offsets in your toolchain.

Windows export note
- When building a C/C++ module on Windows, define `DNG_ABI_EXPORTS` so that `DNG_ABI_API` resolves to `__declspec(dllexport)` and exports `dngModuleGetApi_v1` correctly.

Packaging note
- Use `tools/package_abi_sdk.ps1` to produce a distributable SDK bundle containing ABI headers and this guide.
