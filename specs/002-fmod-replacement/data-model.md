# Data Model: Full FMOD Replacement

Written for the recommended decisions (D1 = D, D2 = a). The variants for other choices are at the
end.

## Sound (existing, `.lsound`): new field

| Field | Type | Default | Rules |
|---|---|---|---|
| `Loading` | enum `SoundLoading { Preload, OnDemand, Stream }` | `Preload` | YAML key `Loading`; a missing key reads as `Preload`, so older files load unchanged |

- **Preload:** the clips load on the asset thread when a scene that references the Sound loads,
  and stay while any loaded scene or live instance references them.
- **OnDemand:** the clips load on first play (a Debug build logs the hitch); they are freed like
  Preload.
- **Stream:** the clips are paged from the pack while playing (Phase 2); nothing is held beyond
  miniaudio's page buffers.
- A clip node's existing `Stream` flag still works. `Loading = Stream` sets it for every clip in
  the Sound. A node flag of `true` under a Preload Sound streams that one clip.

## Clip cache entry (runtime, native backend, new)

| Field | Meaning |
|---|---|
| `Clip` | `AssetHandle` of the `AudioFile` |
| `SceneRefs` | number of loaded scenes whose pack index lists it |
| `LiveRefs` | number of `Leaf` voices currently using it |
| `Resident` | registered with miniaudio (encoded bytes or VFS name) |
| `Bytes` | encoded plus decoded bytes it holds, reported to `AudioPerformance` |

**Lifecycle:** Unloaded → (scene load with Preload, or first play) → Loading (asset thread) →
Resident → (SceneRefs = 0 and LiveRefs = 0) → Released (unregistered, `EncodedData` freed) →
Unloaded. A release never runs while `LiveRefs > 0`. It is checked again when the last voice ends.

## Pack audio entry (export, format 31)

| Field | Meaning |
|---|---|
| `Handle` | the `AudioFile` asset handle |
| `Offset`, `Size` | where the encoded file sits in `AssetPack.lap` |
| `Extension` | the original extension, which picks the decoder (`.wav`, `.ogg`, `.mp3`, `.flac`) |
| `Stream` | whether any Sound streams it; stream entries are never read into memory whole |

Older packs (formats 17–30) carry the bytes after the path, as today. The reader keeps that path.

## Legacy FMOD data (read-only after Phase 6)

| Data | Kept as | Removed by |
|---|---|---|
| `AudioEventRef.Guid` / `Path` / `BankName` | read, never written once cleaned | "Remove FMOD data" (D3 b) |
| `ProjectAudioSettings` Studio fields | read and ignored, with one info log | same action |
| `AudioBankManifest` in runtime project formats 17–30 | read, banks ignored with one warning | — (old exports only) |
| `.fspro` / `.bank` registry entries | listed as unknown, with a warning | same action |

## Variants

- **D1 = B (Sound Banks):** a new asset `SoundBank` (`.lbank`) with `Sounds: [AssetHandle]` and
  `Resident: bool`. The clip cache's `SceneRefs` becomes `BankRefs`. The pack gets one file per bank.
- **D1 = C (scene packs):** no new field. The export writes `Audio/<scene>.lap` and
  `Audio/Shared.lap`; the clip cache counts by scene as above.
- **D4 = c (string paths):** the pack gains a path table, `Assets-relative path → Sound handle`.
