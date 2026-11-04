# Data Model: Full FMOD Replacement

Written for the owner's choices: D1 = B (Lux Sound Banks), D2 = a (inside `AssetPack.lap`), D4 = c
(Sound paths).

## SoundBank (new asset, `.lbank`)

| Field | Type | Default | Rules |
|---|---|---|---|
| `Sounds` | list of `AssetHandle` (Sound) | empty | no duplicates; a missing Sound is a validation error |
| `Resident` | bool | false | loaded at game start and never unloaded |

- A Sound may be in several banks; its clips stay loaded while any bank holding it is loaded.
- Loading a bank preloads every non-stream clip of its Sounds on the asset thread. Stream clips are
  only registered (VFS name), so they cost nothing until they play.
- The YAML is bounded like `.lsound` (64 KiB). The pack serializer writes the handle list.

## Scene (existing): new field

| Field | Type | Rules |
|---|---|---|
| `SoundBanks` | list of `AssetHandle` (SoundBank) | loaded before the scene goes live; unloaded when no loaded scene lists them and no script holds them |

## Sound (existing, `.lsound`)

No new field. A clip node's existing `Stream` flag decides streaming per clip.

## Clip cache entry (runtime, native backend, new)

| Field | Meaning |
|---|---|
| `Clip` | `AssetHandle` of the `AudioFile` |
| `BankRefs` | number of loaded banks whose Sounds use it |
| `LiveRefs` | number of `Leaf` voices currently using it |
| `Resident` | registered with miniaudio (encoded bytes or VFS name) |
| `Bytes` | encoded plus decoded bytes it holds, reported to `AudioPerformance` |

**Lifecycle:** Unloaded → (a bank holding it loads, or first play) → Loading (asset thread) →
Resident → (BankRefs = 0 and LiveRefs = 0) → Released (unregistered, `EncodedData` freed) →
Unloaded. A release never runs while `LiveRefs > 0`. It is checked again when the last voice ends.

## Pack audio entry (export, format 31)

| Field | Meaning |
|---|---|
| `Handle` | the `AudioFile` asset handle |
| `Offset`, `Size` | where the encoded file sits in `AssetPack.lap` |
| `Extension` | the original extension, which picks the decoder (`.wav`, `.ogg`, `.mp3`, `.flac`) |
| `Stream` | whether any Sound streams it; stream entries are never read into memory whole |

Older packs (formats 17–30) carry the bytes after the path, as today. The reader keeps that path.

## Path table (export, D4 = c)

| Field | Meaning |
|---|---|
| `Path` | Assets-relative path without extension, `/`-separated, case-sensitive (`Audio/Sounds/Fart`) |
| `Handle` | the Sound or SoundBank it names |

Written once per pack, for Sound and SoundBank assets only. The editor answers the same lookups from
`EditorAssetManager`, so scripts behave the same in Play and in an export.

## Legacy FMOD data (read-only after Phase 6)

| Data | Kept as | Removed by |
|---|---|---|
| `AudioEventRef.Guid` / `Path` / `BankName` | read, never written once cleaned | "Remove FMOD data" (D3 b) |
| `ProjectAudioSettings` Studio fields | read and ignored, with one info log | same action |
| `AudioBankManifest` in runtime project formats 17–30 | read, banks ignored with one warning | — (old exports only) |
| `.fspro` / `.bank` registry entries | listed as unknown, with a warning | same action |

## Later (not planned now)

- Per-bank pack files (DLC, language packs) can reuse the pack audio entry format unchanged.
