# D-Engine

Header-first C++ engine focused on contracts-first APIs and deterministic, auditable behavior.

## Quickstart
- Open D-Engine.sln in Visual Studio 2022 (or run `msbuild D-Engine.sln /p:Configuration=Debug /p:Platform=x64`).
- Build Debug or Release on x64 to run the smoke/build checks.

## Read next
- Handbook: D-Engine_Handbook.md
- Docs index: Docs/INDEX.md
- Language policy: Docs/LanguagePolicy.md
- ABI policy: Docs/ABI_Interop_Policy.md
- ABI checklist: Docs/ABI_Review_Checklist.md

## Subsystem status (M0)
- Docs/Window_M0_Status.md
- Docs/Renderer_M0_Status.md
- Docs/Time_M0_Status.md
- Docs/Jobs_M0_Status.md
- Docs/Input_M0_Status.md
- Docs/FileSystem_M0_Status.md

## Repository map
- Source/Core/: contracts, foundations, null backends, and interop helpers
- Source/Modules/: optional modules and examples
- Docs/: policies, status docs, and vision notes
- tests/: smoke/build-only checks and header self-containment
