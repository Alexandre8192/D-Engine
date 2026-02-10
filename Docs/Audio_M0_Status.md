# Audio M0 Status

Status
- AudioSystem ships with two backends:
  - `NullAudio` (deterministic, replay-friendly, no device I/O)
  - `WinMmAudio` (Windows device output + software mixing)

Implemented contract surface
- Voice controls: `Play`, `Stop`, `Pause`, `Resume`, `Seek`, `SetGain`
- Bus controls: `SetBusGain` for `Master`, `Music`, `Sfx`
- Clip paths:
  - Memory clip load: PCM16 WAV read fully via FileSystem, then copied in clip pool
  - Stream clip load: PCM16 WAV metadata parse + chunked reads through `ReadFileRange`

Determinism and threading
- `NullAudio` reports `DeterminismMode::Replay`.
- `WinMmAudio` reports `DeterminismMode::Off`.
- Both require `ThreadSafetyMode::ExternalSync`.
- No internal locking is performed in AudioSystem/WinMmAudio hot paths.

Memory policy
- `AudioSystem` uses a shared static WAV load scratch buffer (`g_AudioWavLoadScratch`).
- `WinMmAudio` uses shared static pools for:
  - clip PCM sample pool (`s_ClipSamplePool`)
  - stream cache pool (`s_StreamClips`)
- Consequence: only one initialized `WinMmAudio` instance can own these global pools.
- `Mix()` performs no dynamic allocation.

Stream FileSystem lifetime contract
- Before any stream clip load:
  - call `BindStreamFileSystem(state, fsInterface)`
- While streamed clips are loaded:
  - `UnbindStreamFileSystem(state)` returns `NotSupported`
  - bound FileSystem interface must remain valid
- Stream clip load rejects mismatched/non-bound FileSystem interfaces.

Known limits (M0)
- Fixed capacities (compile-time):
  - voices, command queue, clip count, stream clip count, stream cache chunk size
- Supported stream/source format:
  - WAV PCM16 mono/stereo
- No advanced features yet:
  - 3D spatialization, DSP graph, decoder plugins, compressed asset formats
