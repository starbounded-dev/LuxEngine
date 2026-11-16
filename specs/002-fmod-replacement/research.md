# Research: Full FMOD Replacement

Facts were read from the source in this session. The prior art comes from the engines' public
documentation as known to the planner; no web search was run, and nothing here depends on a version
detail.

## 1. What FMOD still does here (inventory)

| Area | Where | Native equivalent today |
|---|---|---|
| Playback backend | `Core/Source/Lux/Audio/FMOD/` (10 `.cpp` + `FMODInternal.h`, about 1.9k lines) | `Audio/Native/`, the default since Phase 9 |
| Banks: build | `AudioBankBuilder` (runs `fmodstudiocl`), "Rebuild Banks On Play" | none |
| Banks: load and unload | `AudioEngine::LoadBank(s)`, `LoadRuntimeBanks`, `UnloadAllBanks`, `GetBankRevision` | stubs that warn once (`NativeAudioEngine.cpp:840–877`) |
| Banks: export | `AudioBankManifest`, `RuntimeExport::PrepareAudioBanks` / `CopyAudioBanks`, runtime format 17+ | skipped on native |
| Banks: scripts | `Audio.LoadBank`, `event:/` path overloads, `AudioSourceComponent.SetEvent(string)` | `Audio.PlaysSoundAssets`; Sound overloads |
| Project settings | `ProjectAudioSettings` Studio fields (`StudioProjectPath`, `StudioBankOutputPath`, platform, desktop profiles, `EnableLiveUpdate`, `RebuildBanksOnPlay`) | ignored on native |
| Asset types | `AssetType::AudioBank` (`.bank`) and the `.fspro` type | — |
| Editor | Project Settings Studio section, Audio Debugger bank list, event pickers | Sound pickers exist |
| Live tuning | FMOD Studio Live Update | none |
| Verification | `SceneAudioVerification.cpp` FMOD parameter block, `tests/audio` FMOD host (`AudioTestHost.h`, `AuthorFixture.js`) | native tests in `tests/audio/native` |
| Sample data | `Assets/Audio/SampleProject/` (FMOD Studio project, 7.7 MB, which also holds the clips the Sounds use) | `Assets/Audio/Sounds/*.lsound` |
| Build and CI | `--audio-backend`, `ValidateAudioSDK`, `build-fmod-*` jobs, `StageAudioSDKs.py`, `AUDIO_SDK_*` secret | native is the default |

**The gap banks leave.** In an export, every Audio asset's encoded bytes sit in `AssetPack.lap`.
`AudioAssetSerializer` reads the whole file into `AudioFile::EncodedData`, and
`RegisterPackedClip` hands those bytes to miniaudio. `RuntimeAssetManager` frees loaded assets only
at `Shutdown`, so audio memory only grows, and a "streamed" packed track still holds its whole
encoded file. Banks solved both problems: they decided what is loaded, and FMOD streamed from the
bank file.

## 2. How other engines replace "banks"

| Engine | Unit of loading | Who decides | Streaming |
|---|---|---|---|
| FMOD / Wwise | the **bank** / SoundBank: authored groups, built offline | the designer authors them; game code loads and unloads | from the bank file |
| Unity | the **AudioClip** asset; Load Type = Decompress On Load / Compressed In Memory / Streaming; Preload Audio Data | automatic with the scene that references it; Addressables groups for bundles | from the clip's data in the build |
| Unreal | the **Sound Wave**; Loading Behavior = Retain on Load / Prime on Load / Load on Demand; stream caching with one memory budget | automatic per asset; one cache budget for the whole game | chunked, from the pak |
| Godot | the **AudioStream** resource | loaded with the scene's resources | OGG and MP3 decode from the file |

The common pattern outside middleware is **no banks**: each clip is an asset with a loading
behaviour, loaded with the scene that references it, and big tracks are streamed from the shipped
archive. Banks survive in middleware because the authoring tool sits outside the engine. Here, the
authoring tool *is* the engine.

## 3. What miniaudio already gives us

- `ma_resource_manager` supports `MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_STREAM`, `_DECODE` and
  `_ASYNC`. Async loading and stream paging run on its own job threads.
- **Custom VFS** (`ma_vfs` callbacks: open, read, seek, tell, info). A small VFS over
  `AssetPack.lap` can expose each packed clip as `luxpack/<handle><ext>`, a name the code already
  uses, so streaming reads chunks straight from the pack. That needs only a file offset and size per
  clip.
- `ma_resource_manager_register_encoded_data` (used today) keeps the bytes in memory, which is right
  for small, preloaded clips.
- Unregistering (`ma_resource_manager_unregister_data`) already happens in
  `NativeAudioEventInstance.cpp:253`, so freeing a clip is supported.

## 4. Decisions to make (options, recommendation, why)

### D1 — What replaces banks

| Option | What it is | For | Against |
|---|---|---|---|
| **A. No banks: per-Sound loading** (Unity/Unreal model) | Each Sound has a loading behaviour: *Preload* (with any scene that references it), *On demand* (first play), *Stream*. The runtime counts which loaded scenes reference each clip and frees it when none do. | Nothing to author; matches what game makers know; least code; fits the existing per-scene asset index | No hand control over which sounds share memory |
| **B. Lux Sound Banks** (Wwise model) | A new `.lbank` asset lists Sounds. Scripts call `Audio.LoadBank(bank)` / `UnloadBank`. Export writes one file per bank. | Full control; DLC, mods and language packs are natural; familiar to FMOD users | Brings back the chore the owner wants gone; new asset, editor, API and export path; easy to forget a Sound |
| **C. Automatic scene audio packs** | Export splits audio into one pack per scene plus a shared pack. A scene's pack loads and unloads with the scene. | No authoring; memory follows scenes | Coarse; a sound used in two scenes moves to "shared" and stays resident; adds files to the export |
| **D. A now, groups later** | Ship A. If a game needs it, add an optional *Audio Group* tag (a "keep resident" or "preload together" set that scripts can load and unload), without a new file format. | A's simplicity, with a path to B's control when needed | Groups are deferred, so the first game that needs them waits for them |

**Recommendation: D (A first).** It deletes the most code, needs no authoring, and the existing
per-scene asset index already knows which scene uses which clip.

### D2 — Where audio lives in an export, and how it streams

| Option | For | Against |
|---|---|---|
| **a. Inside `AssetPack.lap`, streamed through a miniaudio VFS over the pack** | One file, nothing loose; the `luxpack/` naming already exists | The pack reader must be thread-safe for miniaudio's job threads (one file handle per open stream) |
| **b. A separate `Audio.lap` beside the asset pack** (same VFS) | Audio can be patched alone; per-language packs become possible | A second file to keep in step with the first |
| **c. Loose files under `Assets/Audio/`** | Simplest; moddable | Ships raw assets anyone can copy; many files |

**Recommendation: a**, with b kept open for languages (D8).

### D3 — Old FMOD data in projects

| Option | What it does |
|---|---|
| a. Read and ignore forever, with warnings | What the Phase 10 plan says |
| **b. a, plus a "Remove FMOD data" action** | Project Settings → Audio strips GUIDs, Studio fields and `.fspro` / `.bank` registry entries in one undoable save, with a report first |
| c. Format bump that refuses old data | A clean break; old projects stop opening |

**Recommendation: b.**

### D4 — Script API

| Option | Effect on scripts |
|---|---|
| **a. Remove the FMOD APIs** (`LoadBank`, `event:/` overloads, `SetEvent(string)`, `PlaysSoundAssets`) behind `[Obsolete(..., error: true)]` stubs for one release | The build fails with a message that names the Sound replacement |
| b. Keep them as warning stubs (`LoadBank` returns true; a path resolves to a Sound) | Old scripts keep running |
| c. Make every string path a **Sound asset path**, with a path table shipped in the pack | `Audio.PlayOneShot("Audio/Sounds/Fart")` works in exports too. A typed `Sound` field is still preferred |

A sub-choice: keep the name `EventInstance` (no churn), or rename it to `SoundInstance` (clearer; an
`[Obsolete]` alias for one release).

**Recommendation: a, keeping `EventInstance`.** Add c only if string paths are wanted.

### D5 — The backend seam

| Option | |
|---|---|
| **a. Delete `--audio-backend` and the build split** | One implementation; `Audio/Native/` stays where it is to avoid churn; `AudioEngine::GetBackend()` is removed along with its callers |
| b. Keep the seam with one implementation | Room for a console or platform backend later; costs a define and an option nobody uses |

**Recommendation: a.** The engine-facing headers stay backend-neutral either way, so a seam can be
added back later.

### D6 — The sample's FMOD Studio project (7.7 MB)

**Recommendation: move the clips the Sounds use to `Assets/Audio/Clips/` and delete the rest.**
Archiving it elsewhere in the repository keeps 7.7 MB that nothing reads.

### D7 — Replacing Live Update

| Option | |
|---|---|
| a. Nothing | Edit, stop and replay |
| **b. Hot-apply** | Saving a Sound or pressing Apply in the mixer during Play updates playing voices (volume, pitch, curves, bus; tree changes restart the instance) |

**Recommendation: b**, if Phase 0 shows Sound edits are not already applied live.

### D8 — Per-language audio (FMOD's language banks)

**Recommendation: keep every language in the main pack for now** (it works today through
`DialogueTranslation::Clip`). Per-language packs need D2 b, and can follow when a game needs them.

### Chosen (2026-11-02)

D1 = **B, Lux Sound Banks** (lists inside the one pack, not files of their own); D2 = **a**;
D3 = **b**; D4 = **c, Sound paths**; D5 = **a**; D6 = **move and delete**; D7 = **b**;
D8 = **main pack**. The plan, data model and contracts are written for this set.

## 5. Risks

| Risk | Mitigation |
|---|---|
| VFS reads on miniaudio job threads contend with asset loading | One `FILE*` per open stream; no shared reader; a stress test with 16 streams plus mesh loads |
| Freeing a clip that a playing voice still uses | The release waits until no instance holds the clip (count references in `Leaf`) |
| Preload hitches at scene start | Preload on the asset thread before the scene goes live (`RuntimeAssetManager` already loads by scene); measure SC-005 |
| Deleting FMOD breaks a test that silently covered native behaviour | Port first, delete second (Phase 6 order) |
