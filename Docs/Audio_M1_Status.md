# Audio M1 Status

This document captures the audio subsystem at milestone M1. It reflects the contract, the deterministic Null backend, the orchestration system, and the accompanying tests.

## Current Components
- **Core/Contracts**
  - `Source/Core/Contracts/Audio.hpp`
    - Defines opaque handles (`SoundHandle`, `VoiceHandle`) and the `AudioBus` enum (`Master`, `Music`, `Sfx`, `Ui`, `Voice`, `Count`).
    - Declares `AudioCaps` with `has_buses` (also promoted when the v-table exposes `setBusVolume`), determinism flag, and basic output hints.
    - `PlayParams` now carries `bus`, `volume` (linear [0..1]), `pitch` (playback rate ratio), `loop`, and `startTimeSeconds` (>= 0).
    - Core surface: `QueryCaps`, `Play`, `Stop`, `SetBusVolume`, `SetMasterVolume` (alias for the Master bus). Debug builds assert invalid ranges; release backends may clamp. Master volume is not a separate backend concept.
    - Provides `AudioBackend` concept and `MakeAudioInterface` adapter to produce a dynamic `AudioInterface` without ownership.
- **Core/Audio**
  - `Source/Core/Audio/NullAudio.hpp`
    - Deterministic backend that increments voice handles (1, 2, 3, ...), tracks `totalPlays/totalStops/activeVoices` stats, and stores bus volumes in backend state (not stats).
    - Reports `AudioCaps` with `deterministic = true` and `has_buses = true`; clamps bus gains to [0,1].
    - `MakeNullAudioInterface` initializes bus volumes to 1.0 and wraps the backend in `AudioInterface`.
  - `Source/Core/Audio/AudioSystem.hpp`
    - Orchestrator holding an injected `AudioInterface`, cached `AudioCaps`, and engine-side `busVolumes[(u32)AudioBus::Count]`.
    - `InitAudioSystem(AudioSystemState&, AudioInterface)` validates the interface, caches caps, seeds bus volumes to 1.0, and applies them when `setBusVolume` exists. No backend headers are included or owned.
    - Provides `SysPlay`, `SysStop`, `SysSetBusVolume`, and `SysSetMasterVolume` helpers; `ShutdownAudioSystem` clears cached state without ownership transfer.

## Guarantees
- Header-first, no exceptions or RTTI, no allocations at the contract boundary or in the orchestrator.
- Public types are POD/trivially copyable; API uses value types or non-owning pointers only.
- Volume values are linear gain in [0..1]; pitch is a playback rate ratio. Debug builds assert invalid ranges or NaN; release backends may clamp.
- Bus support is explicit via `AudioCaps::has_buses` or the presence of `setBusVolume` in the v-table. Master volume is an alias for the Master bus.
- NullAudio is deterministic and keeps bus volumes as functional state separate from stats.

## Tests
- Header-only compile check: `tests/SelfContain/Audio_header_only.cpp` (`static_assert` on handles, caps, and `PlayParams`).
- Smoke test: `tests/Audio_smoke.cpp`
  - Instantiates `NullAudio`, wraps it with `MakeNullAudioInterface`, injects it via `InitAudioSystem`, and verifies bus volume propagation plus the SysSetMasterVolume alias.
  - Confirms deterministic voice handles and that stats track plays/stops without owning the backend through the system.
- Aggregated runner: `tests/AllSmokes/AllSmokes_main.cpp` invokes `RunAudioSmoke()` alongside other subsystems.

## Milestone Definition: Audio M1
Audio M1 is considered complete when:
- The audio contract compiles standalone and documents bus-aware play parameters with clear range semantics.
- `AudioCaps` advertises bus support explicitly; master volume routes through the Master bus.
- NullAudio remains deterministic, stores bus volumes in backend state, and exposes bus control via `SetBusVolume`.
- AudioSystem holds only the injected `AudioInterface` (no concrete backend headers or ownership), caches caps, and applies bus volumes when available.
- Smoke and self-contain tests pass in Debug/Release with warnings as errors, without hidden allocations or backend coupling.
