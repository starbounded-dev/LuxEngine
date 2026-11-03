# Implementation Plan: Full FMOD Replacement (banks included)

**Branch**: `002-fmod-replacement` (work lands on `feature/native-audio-system`) | **Date**: 2026-11-02 |
**Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/002-fmod-replacement/spec.md`

## Summary

FMOD is already optional (native audio became the default in Phase 9 of
`docs/AUDIO_NATIVE_BACKEND_PLAN.md`). This plan removes it completely, and replaces what **banks**
did with something native. Banks decided what audio is in memory, streamed from their files and
grouped data for export. The recommended replacement is **no banks**: each Sound gets a loading
behaviour (Preload / On demand / Stream), clips load with the scenes that reference them and are
freed when none do, and streamed clips read in chunks straight from the asset pack through a
miniaudio VFS. Optional Audio Groups can come later. Each choice below has alternatives; the
phases are written for the recommended set and say what changes if another option is chosen.

## Decisions (the owner's to make; details and trade-offs in [research.md](research.md) § 4)

| # | Question | Options | Recommended |
|---|---|---|---|
| D1 | What replaces banks | A. No banks, per-Sound loading · B. Lux Sound Banks (`.lbank`) · C. Automatic per-scene audio packs · D. A now, optional Audio Groups later | **D** |
| D2 | Where export audio lives and streams from | a. Inside `AssetPack.lap` through a VFS · b. A separate `Audio.lap` · c. Loose files | **a** |
| D3 | Old FMOD data in projects | a. Read and ignore · b. a + a "Remove FMOD data" action · c. Hard break | **b** |
| D4 | Script API | a. Remove the FMOD APIs (compile errors that name the replacement) · b. Warning stubs · c. String paths become Sound asset paths, shipped in a table · plus keeping or renaming `EventInstance` | **a, keep `EventInstance`** |
| D5 | Backend seam | a. Delete `--audio-backend` · b. Keep the seam with one implementation | **a** |
| D6 | Sample FMOD Studio project | Move its clips to `Assets/Audio/Clips/` and delete the rest · archive it · keep it | **move and delete** |
| D7 | Live Update replacement | a. None · b. Hot-apply Sound and mixer edits during Play | **b** (if not already live) |
| D8 | Per-language audio | All languages in the main pack · per-language packs (needs D2 b) | **main pack** |

If D1 is **B** (Sound Banks), Phase 1 is replaced by: an `.lbank` asset type (the six-step asset
playbook), a bank editor panel, `Audio.LoadBank(SoundBank)` / `UnloadBank`, and one pack file per
bank (D2 b becomes required). If D1 is **C**, Phase 1 keeps the reference counting, and Phase 2
writes one audio pack per scene plus a shared pack.

## Technical Context

**Language/Version**: C++20 (Core, Editor, Lux-Runtime); C# net9.0 (ScriptCore, sample scripts)

**Primary Dependencies**: vendored miniaudio 0.11.25 and MiniaudioCpp (already present; nothing new
is added). FMOD Core/Studio is removed.

**Storage**: `AssetPack.lap` (`AssetPackSerializer`, per-scene asset index); `.lsound` YAML; runtime
project format, bumped to 31 for loading behaviours

**Testing**:
- `tests/audio/native` (CMake, offline `ma_engine` rendering);
- `tests/audio/run_sdk_layout.py`;
- `tests/rendering/golden_run.py --play` / `--export-dir` (needs the owner's OK to launch the editor);
- the YAML oracle (`tests/yaml/run.py`).

**Target Platform**: Windows now; Linux follows the owner's standing rule

**Project Type**: game engine (static library + editor + runtime player)

**Performance Goals**:
- A preloaded Sound adds no first-play frame spike above 2 ms (SC-005).
- A 5-minute streamed track holds under 1 MB of audio memory (SC-004).
- Memory falls to the new scene's needs within 1 s of a switch (SC-003).

**Constraints**:
- No new dependency.
- The audio thread never blocks on file IO: miniaudio's job threads page streams.
- Exports from formats 17–30 still load.

**Scale/Scope**: about 1.9k lines of FMOD backend and roughly 40 files that mention banks are
deleted or rewritten (research.md § 1).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status |
|---|---|
| I. Self-contained, small | ✅ It removes a proprietary SDK and a separate authoring app; adds no dependency. Binary size falls (FMOD DLLs are already absent from native builds); memory now falls instead of only growing. |
| II. Conventions, existing helpers | ✅ Reuses `RuntimeAssetManager` / `AssetPackSerializer` (per-scene index), `FileStreamReader`, miniaudio's resource manager and VFS, `AssetManager` dependencies, `ImGuiEx` for the debugger view. |
| III. Threading | ✅ Stated per phase: the main thread decides releases at scene switch; the asset thread preloads; miniaudio's job threads read the VFS; the audio callback never touches files. No renderer work. |
| IV. Independently verifiable phases | ✅ Each phase has its own build and test, below. The editor is launched only with the owner's OK. Phase 0 measures before anything changes. |
| V. Docs stay true | ✅ Each phase lists the docs it updates (Architecture §2.10, Building.md, CLAUDE.md, README, the editor manual). |

**Post-design re-check:** still passes. The data model adds one field and one optional entity; the
contracts change the script API only as D4 decides.

## Phases

Each phase builds on Windows (`scripts/Lux-BuildTest.ps1 -Configurations Debug`), passes
`tests/audio/native`, and is committed and pushed before the next starts. A phase that adds or
removes files re-runs `Win-GenProjects.py`.

### Phase 0 — Decisions and baseline
- The owner picks D1–D8.
- Measure today's sample export:
  - audio memory over a scene switch (`AudioPerformance` clip memory);
  - the first-play frame time of the Fart Sound;
  - memory while the music streams.
- Check whether Sound Editor edits already apply to playing voices (input to D7).
- **Done when:** decisions are recorded here and the three numbers are in `report.md`.

### Phase 1 — Loading model (D1 = A/D)
- `Sound` gains `Loading` (Preload / OnDemand / Stream); a node's `Stream` flag stays, and decides
  per clip ([data-model.md](data-model.md)).
- A clip cache in the native backend counts references from loaded scenes and live instances. It
  frees a clip (unregister from miniaudio, drop `EncodedData`) when the count reaches 0, after the
  last voice using it ends.
- `RuntimeAssetManager`, on scene change, releases assets the new scene's index does not list.
  Today nothing is ever released; this is the first release path, scoped to Audio and Sound
  assets.
- Preload runs on the asset thread before the scene starts.
- **Threads:**
  - main: scene switch and the release decision;
  - asset thread: loading;
  - miniaudio job threads: decoding.
- **Test:** an offline test with two scenes asserts clip memory rises and falls, and that a clip
  playing across the switch is not freed.

### Phase 2 — Streaming from the pack (D2 = a)
- An `ma_vfs` over `AssetPack.lap`: `luxpack/<handle><ext>` opens the clip's offset and size, with
  one file handle per open stream.
- The pack writer records each Audio entry's offset and size (no bytes are copied into memory for
  stream clips). Runtime project format 31; readers keep 17–30.
- `AudioFile::EncodedData` is filled only for non-stream clips.
- **Test:**
  - export the sample;
  - a test streams a 5-minute clip from a pack (SC-004);
  - a stress test runs 16 streams while meshes load.

### Phase 3 — Legacy data and the sample (D3 = b, D6)
- The "Remove FMOD data" action (Project Settings → Audio) reports, then strips GUIDs, Studio fields
  and `.fspro` / `.bank` registry entries in one undoable save.
- The sample: move `fart-01` and `Music_1.mp3` to `Assets/Audio/Clips/` (registry paths), delete
  `Assets/Audio/SampleProject/`, and drop the FMOD GUIDs from AudioDemo and Benchmark (the
  generator too).
- **Test:**
  - the YAML oracle;
  - opening a pre-Phase-9 copy of the sample lists every FMOD reference, and one action leaves zero
    (SC-006, editor OK needed).

### Phase 4 — Script API (D4 = a)
- Remove `Audio.LoadBank`, the `event:/` string overloads, `AudioSourceComponent.SetEvent(string)`
  and `Audio.PlaysSoundAssets`, behind `[Obsolete(..., error: true)]` stubs that name the
  replacement, for one release.
- `AudioDemo.cs` loses its FMOD branch.
- Regenerate the API docs (`tools/ScriptApiDocs`, `--check`).
- **Test:** ScriptCore and the sample scripts build; the API-doc check passes.

### Phase 5 — Editor (FR-007, FR-008, D7)
- Remove:
  - the Project Settings Studio section and the bank build / "Rebuild Banks On Play";
  - event pickers;
  - the `.fspro` / `.bank` content types.
- The Audio Debugger's bank list becomes **Loaded clips**: handle, Sound, loading behaviour, memory,
  streaming, reference count.
- If D7 = b: hot-apply Sound saves and mixer Apply to playing voices.
- **Test:** build; an ImGui audit (`Conventions.md` § ImGui correctness); an editor pass with the
  owner's OK.

### Phase 6 — Delete FMOD (D5 = a)
- Port, then delete:
  - the FMOD-only tests (`tests/audio` host, `AuthorFixture.js`);
  - `SceneAudioVerification`'s FMOD block (the `--play` check reads native parameters).
- Delete:
  - `Audio/FMOD/`, `AudioBankBuilder`, the manifest writer (the reader stays for 17–30);
  - `--audio-backend`, `ValidateAudioSDK` and the FMOD entries in `Dependencies.lua`;
  - `AudioBackend` / `GetBackend()` and their callers;
  - the `fmod-audio` setup option, `build-fmod-*` CI jobs, `StageAudioSDKs.py`, the `AUDIO_SDK_*`
    secret docs;
  - the README licensing section, `docs/Editor/FMOD.md`.
- **Test:**
  - `grep -ri fmod` (SC-001);
  - a fresh-clone build;
  - `tests/audio/native`;
  - `run_sdk_layout.py`, rewritten or deleted along with the option.

### Phase 7 — Parity leftovers (User Story 4)
- A music timeline view in the Sound Editor (markers and tempo on the waveform).
- Dialogue clip drag-and-drop and a play button.
- Running the sample-project validation in the editor.
- **Test:** an editor pass with the owner's OK.

### Phase 8 — Verification
- SC-001 to SC-006 on Windows.
- An export run in Lux-Runtime.
- Listening checks by the owner.
- Results go in `report.md`.

## Project Structure

### Documentation (this feature)

```text
specs/002-fmod-replacement/
├── spec.md
├── plan.md              # this file
├── research.md          # inventory, prior art, the eight decisions
├── data-model.md        # Sound.Loading, clip cache entry, pack audio entry, legacy data
├── quickstart.md        # how to verify each phase
├── contracts/
│   ├── scripting-api.md # C# changes per D4
│   └── export-format.md # pack audio entries, VFS names, format 31
└── tasks.md             # /speckit-tasks, after the decisions
```

### Source Code (repository root)

```text
Core/Source/Lux/Audio/
├── Sound.{h,cpp}                    # Loading behaviour (Phase 1)
├── Native/NativeAudioEventInstance.cpp  # clip cache, release (Phase 1)
├── Native/NativePackVFS.cpp         # NEW: ma_vfs over the asset pack (Phase 2)
├── AudioBankBuilder.*, AudioBankManifest.* (writer)   # deleted (Phase 6)
└── FMOD/                            # deleted (Phase 6)
Core/Source/Lux/Asset/
├── AudioAssetSerializer.cpp         # offset and size for stream clips (Phase 2)
└── AssetManager/RuntimeAssetManager.cpp  # release on scene change (Phase 1)
Core/Source/Lux/Serialization/AssetPack*.cpp   # audio entries (Phase 2)
Editor/Source/Panels/
├── AudioDebugPanel.cpp              # Loaded clips view (Phase 5)
├── ProjectSettingsWindow.cpp        # Studio section out, "Remove FMOD data" in (Phases 3, 5)
└── SoundEditorPanel.cpp             # Loading field (1), timeline view (7)
ScriptCore/Source/Lux/Audio.cs       # D4 (Phase 4)
tests/audio/native/NativeBackendTests.cpp   # tests for Phases 1–2
```

**Structure Decision**: no new project or folder. One new source file (`NativePackVFS.cpp`); the
rest are edits and deletions in existing files.

## Complexity Tracking

None. Every principle passes without an exception.
