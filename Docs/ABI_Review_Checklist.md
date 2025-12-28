# D-Engine ABI Review Checklist (v1)

Purpose
- Provide a strict checklist for reviewing any ABI-related change.
- This is used by humans and Copilot prompts.

Contract
- If any checklist item fails, the change is rejected or must be redesigned.
- ABI v1 is immutable once published.

---

## A. Interface shape

[A1] Is the cross-language boundary C ABI only?
[A2] Does the subsystem expose a function table + context pattern?
[A3] Do all functions return `dng_status_t` and use out-params for outputs?
[A4] Are there explicit thread-safety notes for the API?

## B. Unwinding and safety

[B1] Is there any possibility of exceptions/panics escaping across ABI?
[B2] Are all language-specific exceptions caught and converted to status codes?
[B3] Are callbacks documented to never unwind?

## C. Data and layout

[C1] Are all ABI types POD and composed only of fixed-width primitives?
[C2] Are enums represented with explicit size (or as uint32_t)?
[C3] Are booleans represented as uint8_t?
[C4] Are strings represented as (ptr + len)?
[C5] Are arrays represented as (ptr + count)?
[C6] Are handles integers (not pointers), and is invalid handle documented?

## D. Ownership and allocation

[D1] Is ownership explicit for every pointer?
[D2] If memory is returned, is there a matching free function or host allocator usage?
[D3] Do hot-path functions avoid hidden allocations?
[D4] Are scratch buffers / arenas used where appropriate?

## E. Versioning

[E1] Does the change avoid modifying any released v1 struct/signature?
[E2] If compatibility is broken, does it introduce v2 names instead?
[E3] If using struct extension, does it append fields only at the end and validate `struct_size`?

## F. Determinism

[F1] Are all non-deterministic sources (time, RNG, threading) explicit and documented?
[F2] Are optional deterministic modes described in caps/flags?

## G. Tests

[G1] Is there a header self-contained test for new headers?
[G2] Is there a compile-time layout check (sizeof/offsetof) where relevant?
[G3] Is there a smoke test that loads the module (if dynamic) and calls a minimal sequence?

---

Result
- PASS only if all required items pass.
- If uncertain: redesign to reduce ABI surface and clarify ownership.
