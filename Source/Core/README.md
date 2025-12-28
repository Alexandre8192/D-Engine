# D-Engine Core

Core is the engine foundation:
- contracts (backend-agnostic APIs)
- core utilities (platform, diagnostics, memory, math)
- optional ABI + interop helpers for loadable modules

Rules of the road:
- no exceptions in Core
- no RTTI in Core
- no hidden allocations at contract boundaries
- header-first: headers must be self-contained

Start here:
- Handbook: ../../D-Engine_Handbook.md
- Docs index: ../../Docs/INDEX.md
- Language policy: ../../Docs/LanguagePolicy.md
- ABI policy: ../../Docs/ABI_Interop_Policy.md
- ABI checklist: ../../Docs/ABI_Review_Checklist.md

Core map:
- Contracts: Contracts/
- ABI headers: Abi/
- Interop helpers: Interop/
- Subsystems: Window/, Renderer/, Time/, Jobs/, Input/, FileSystem/
- Foundations: Platform/, Diagnostics/, Memory/, Math/
