# D-Engine - Copilot Instructions

Single source of truth:

- `D-Engine_Handbook.md`

When in doubt, do not invent new policies. Update the handbook instead.

Quick non-negotiables (high risk if violated):

- Core: no exceptions, no RTTI.
- Core: no raw `new`/`delete` expressions (placement-new allowed).
- Core: ASCII-only bytes in code files.
- Follow contracts-first structure (contract + null backend + system + tests).
