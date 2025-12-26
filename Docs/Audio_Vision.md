# D-Engine Audio - Vision and Roadmap (M0 -> M2)

Date: 2025-12-23

## Purpose

Reserve a clean architectural slot for audio in D-Engine that is:
- Backend-agnostic (multiple implementations can plug in).
- Deterministic-friendly (Null backend + repeatable tests).
- Contract-first (clear invariants, no hidden allocations, stable POD boundaries).
- Modular (AudioSystem does not require any concrete backend to compile).

This subsystem is intentionally not trying to be "state of the art audio tech".
Goal: a modern baseline (SFX + music + voice/UI) with user-adjustable volume buses,
while keeping the door open for advanced contributors via optional extensions.

## Non-goals (by design)

The Core Audio Contract does NOT try to:
- Provide a full DSP/mixing engine implemented in Core.
- Guarantee a specific spatialization/HRTF solution.
- Provide streaming/decoder implementations in Core.
- Force thread safety or job scheduling policy (backend-defined).
- Expose vendor types (no FMOD/Wwise objects in Core API).

Those can exist as backends and optional extensions.

---

## Philosophy alignment

### Contracts first
- Every function has a documented contract.
- Public API uses POD/value types or views only.
- Opaque handles (non-owning identifiers).
- Explicit capability discovery (caps).
- Optional feature surfaces are discoverable (extensions), not implied.

### No allocations at contract boundary
- The contract cannot require allocations.
- Backends may allocate internally, but the contract does not force it.
- Hot paths should be allocation-free in common backends.

### Determinism is a first-class concept
- Null backend is deterministic.
- Caps expose a deterministic flag.
- A backend that cannot guarantee determinism must say so.

---

## Core vocabulary

### Handles
- SoundHandle: identifies a sound resource (decoded/loaded/registered by backend or higher-level module).
- VoiceHandle: identifies an active playback instance.
Handles are opaque, non-owning identifiers. Invalid handle is zero.

### Buses (volume channels)
Audio must support at least:
- Master
- Music
- Sfx
- Ui
- Voice

The engine may expose more (Ambience, Cinematic, etc.), but the contract baseline should include the above.
The bus concept is not "mixing tech"; it is a simple routing label + gain.

### Play params
Core play params should be enough for typical games:
- bus routing
- gain/volume (linear)
- pitch (rate ratio)
- looping flag
- optional start offset (seconds or frames, but define it)

---

## Contract shape

### Core (M0/M1) surface

Minimal stable functions:
- QueryCaps(AudioInterface) -> AudioCaps
- Play(AudioInterface, SoundHandle, PlayParams) -> VoiceHandle
- Stop(AudioInterface, VoiceHandle)
- SetMasterVolume(AudioInterface, float linear01)
- SetBusVolume(AudioInterface, AudioBus, float linear01)  (M1 addition)

Notes:
- Ranges must be explicitly defined in the contract.
- In debug, prefer DNG_ASSERT on invalid interface (vtable/userData) while staying safe in release.
- Thread-safety: backend-defined. Callers must synchronize per backend instance.

### Capability discovery (AudioCaps)

Recommended fields (POD):
- bool deterministic
- bool has_buses (or infer via extension availability)
- bool has_spatial
- bool has_streaming
- bool has_effects
- uint32_t max_voices (0 means "unknown/unbounded", but document)
- uint32_t preferred_sample_rate (0 if unknown)
- uint8_t output_channels_min / output_channels_max (or a small bitmask)

Keep it small and stable.

---

## Extensibility mechanism

### Why extensions?
If you keep adding optional features directly into the core vtable,
the contract grows fast and backends get burdened.

Instead, use a query extension mechanism:
- Core remains tiny.
- Advanced contributors can add features without breaking the core API.

### Recommended design
Add to core:
- void* QueryExtension(AudioInterface, AudioExtensionId id)

Where:
- AudioExtensionId is a small enum or 32-bit tag (fourcc style is fine).
- Returned pointer is either null (not supported) or points to a stable POD struct/vtable
  for that extension.

This mirrors patterns used in graphics APIs and keeps your Core contract stable.

---

## Proposed extensions (not required in M0/M1)

### Spatial extension (AudioExtSpatial)
Purpose: 3D audio without forcing any specific tech.

Possible API (POD params):
- SetListener(ListenerParams)
- SetVoiceSpatial(VoiceHandle, SpatialParams)
- SetDopplerScale(float)
Caps: has_spatial

### Streaming extension (AudioExtStreaming)
Purpose: music/long tracks.

Possible API:
- CreateStream(StreamDesc) -> StreamHandle
- PlayStream(StreamHandle, StreamPlayParams) -> VoiceHandle
- QueueStreamData(StreamHandle, AudioBufferView)  (or push/pull callback via user data)
Caps: has_streaming

### Effects extension (AudioExtEffects)
Purpose: bus FX, reverb zones, EQ, etc.

Possible API:
- CreateEffect(EffectDesc) -> EffectHandle
- AttachEffectToBus(AudioBus, EffectHandle)
Caps: has_effects

These are optional. The core engine can ignore them.

---

## AudioSystem role

AudioSystem should be an orchestrator/holder, not a backend owner.

### Must
- Hold an AudioInterface.
- Provide init/shutdown order hooks.
- Provide engine policy state (bus volumes, global mute, etc.).
- Provide a default path for tests (Null backend), but not require it for compilation.

### Must NOT
- Depend on any concrete backend header in its public header.
- Implement a full mixer in Core.

### Recommended pattern
- AudioSystemState contains:
  - AudioInterface iface
  - cached caps
  - bus volume table (engine-side copy)
  - init flag

Null backend can live in:
- Source/Core/Backends/Audio/NullAudio.hpp (or similar)
and tests can include it.

---

## Roadmap milestones

### M0 - Architectural slot (what you already did)
Deliverables:
- Contract header Audio.hpp (core functions only).
- Deterministic Null backend NullAudio.hpp.
- System AudioSystem.hpp (holder + minimal init).
- Header-only compile test.
- Smoke test.

Definition of done:
- No exceptions/RTI.
- All types POD/trivial at boundary.
- Null backend is deterministic and testable.
- System does not allocate on contract boundary.

### M1 - Modern minimum (recommended next)
Add:
- Volume buses:
  - AudioBus enum (Master, Music, Sfx, Ui, Voice).
  - SetBusVolume(AudioBus, float linear01).
  - PlayParams.bus routing.
- Contract docs: ranges and semantics:
  - volume is linear gain in [0..1] (backend may clamp).
  - pitch is playback rate ratio (1.0 normal).
- Debug asserts in wrappers for misuse.

Tests:
- Smoke test verifies that bus volumes are stored/applied (Null backend can simulate).
- Verify that bus volumes do not affect handle determinism.

Definition of done:
- End-user can build a typical "Options > Audio" menu (master/music/sfx/ui/voice).
- Still no streaming required.

### M2 - Extension mechanism (recommended early, even if unused)
Add:
- QueryExtension(AudioExtensionId) to core contract.
- Define extension IDs for Spatial/Streaming/Effects (placeholders OK).
- Provide zero implementations initially (return null).

Tests:
- Smoke test checks QueryExtension returns null on Null backend (unless implemented).

Definition of done:
- Contract supports growth without breaking core API.
- Future contributor can add spatial/streaming/effects without editing the core surface.

---

## Contract documentation checklist (for every public type/function)

For each public entry, document:

### Purpose
What problem does this solve?

### Contract
- Inputs and valid ranges.
- Lifetime rules (handles validity, ownership).
- Thread-safety assumptions.
- Determinism promises (if any).
- Error behavior (invalid handles, invalid interface).

### Notes
- Performance expectations (no allocations at boundary).
- Backend variability (caps, optional features).
- Default behavior (clamping, no-op, etc.).

---

## Compatibility and contributor friendliness

### Stable ABI goals
- Prefer fixed-size POD and enums.
- Keep vtables small.
- Avoid exposing STL containers or engine-private types.

### Contributor guidelines
- A backend should be able to implement the core contract without needing any other D-Engine module.
- Advanced features should live in extensions.
- Backends should document which caps they claim and what they actually guarantee.

---

## Practical recommendations (next PR scope)

Recommended next PR (M1):
1) Add AudioBus enum + bus volume functions.
2) Add bus field to PlayParams.
3) Add range semantics to docs.
4) Refactor AudioSystem so it does not include/own NullAudio in its header.
5) Keep NullAudio as an optional backend and tests helper.

If you do only one structural change: remove the hard dependency of AudioSystem on NullAudio.
That is the biggest modularity win.

End.
