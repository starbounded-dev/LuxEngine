# Tasks: Full FMOD Replacement (banks included)

**Input**: Design documents from `specs/002-fmod-replacement/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: Included. The plan names a test for every phase, and constitution IV requires concrete
verification. Native tests go in `tests/audio/native/NativeBackendTests.cpp` (offline `ma_engine`,
no device).

**Organization**: Tasks are grouped by user story. US2 (memory follows needs, i.e. Sound Banks and
streaming) comes before US1 (no FMOD anywhere): both are P1, and US1's script API loads Sound Banks.
Commit and push after each checkpoint. Run `/cr` before each commit. **(editor)** marks a step that
launches the editor and needs the owner's OK for that run.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: US1–US4 from spec.md

---

## Phase 1: Setup (baseline, plan Phase 0)

**Purpose**: Measure before changing anything, and settle the open D7 question.

- [x] T001 **(editor)** Export the sample with `python tests/rendering/golden_run.py --config debug --label base --scenes AudioDemo --export-dir bin/fmodless-base/AudioDemo`, run the export for 30 s, and record in `specs/002-fmod-replacement/report.md`: clip memory before and after a scene switch (`NativeInternal` clip memory, as reported by `Core/Source/Lux/Audio/Native/NativeAudioPerformance.cpp`), the first-play frame time of the Fart Sound, and memory while the music plays
- [x] T002 [P] Read `Editor/Source/Panels/SoundEditor/SoundEditorPanel.cpp` (save path) and `Core/Source/Lux/Audio/Native/NativeAudioEventInstance.cpp` (instance creation), and record in `specs/002-fmod-replacement/report.md` whether a Sound saved during Play changes voices already playing. This decides whether T054 is needed (D7)
- [x] T003 [P] Record the starting FMOD inventory in `specs/002-fmod-replacement/report.md`: the output of `git grep -il fmod -- Core Editor ScriptCore Lux-Runtime tests scripts .github ':!Core/Source/Lux/Audio/FMOD'`, and the 18 files that call `AudioEngine::GetBackend()`

---

## Phase 2: Foundational (the SoundBank asset)

**Purpose**: The `.lbank` asset that US2's loading and US1's script API both use.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [x] T004 Add `AssetType::SoundBank` with its string helpers ("SoundBank") in `Core/Source/Lux/Asset/AssetTypes.h`, and `{ ".lbank", AssetType::SoundBank }` in `Core/Source/Lux/Asset/AssetExtensions.h`
- [x] T005 Create the `SoundBank` asset class in `Core/Source/Lux/Audio/SoundBank.h` and `Core/Source/Lux/Audio/SoundBank.cpp`. Fields: `Sounds` (list of `AssetHandle`; "no duplicates; a missing Sound is a validation error") and `Resident` (bool, default `false`: "loaded at game start and never unloaded"). Add `GetStaticType()`, `Add`/`Remove` that keep the list duplicate-free, and `Validate(std::vector<std::string>&)`
- [x] T006 Create `SoundBankSerializer` in `Core/Source/Lux/Asset/SoundBankSerializer.h` and `Core/Source/Lux/Asset/SoundBankSerializer.cpp`, following `SoundSerializer.cpp`. YAML keys `Sounds` and `Resident`, "bounded like `.lsound` (64 KiB)" at save, load, pack write and pack read. `RegisterDependencies` lists every Sound; the pack serializer writes the handle list. Register it in `Core/Source/Lux/Asset/AssetImporter.cpp` (`s_Serializers[AssetType::SoundBank]`) and mark it packable in `IsSerializedByAssetPack` in `Core/Source/Lux/Serialization/AssetPack.cpp`
- [x] T007 [P] Add **New › Sound Bank** (both menus beside "Sound", line ~255 and ~764) and a `CreateSoundBankAsset()` to `Editor/Source/Panels/ContentBrowserPanel.cpp`, plus a Content Browser icon in `Core/Source/Lux/Editor/EditorResources.h`
- [x] T008 [P] Make `SoundBank` a script field type:
  - `[EditorAssignable] public class SoundBank : AssetRef` in `ScriptCore/Source/Lux/Asset.cs`;
  - `{ "Lux.SoundBank", DataType::SoundBank }` in `Core/Source/Lux/Scripting/ScriptEngine.cpp`;
  - `DataType::SoundBank` in `Core/Source/Lux/Scripting/ScriptEntityStorage.hpp` (size `sizeof(UUID)`, the "SoundBank" string both ways, `FieldUtils::IsAssetType`);
  - scalar and array drawing in `Core/Source/Lux/ImGui/ImGuiEx.cpp`, guarded by `!IsPlaying()` like `Sound`.
- [x] T009 Add a native test "sound bank asset": a YAML round trip of `.lbank` (duplicates rejected; `Resident` default `false`; a file over 64 KiB refused) in `tests/audio/native/NativeBackendTests.cpp`. Then run `python scripts/Win-GenProjects.py --last`, `scripts/Lux-BuildTest.ps1 -Configurations Debug`, and the native tests

**Checkpoint**: `.lbank` assets can be made, saved, packed and assigned to script fields.

**Done (2026-11-03):** as planned, with two deviations. `SoundBankSerializer` lives beside
`SoundSerializer` in `SoundSerializer.{h,cpp}`, sharing templated helpers instead of a copied file.
The Content Browser icon reuses `SoundGraphFileIcon` (no new artwork). `Validate` takes
`std::string*` like `Sound::Validate`.

---

## Phase 3: User Story 2 — Audio memory follows what the game needs (Priority: P1) 🎯 MVP

**Goal**: Sound Banks load and free clips; big tracks stream from the pack (plan Phases 1–2).

**Independent Test**:
- In a two-bank test, memory rises on load and falls on unload.
- A clip shared by both banks survives unloading one.
- A clip that is playing is never freed.
- A 5-minute streamed clip holds under 1 MB (SC-003, SC-004).

### Tests for User Story 2

- [x] T010 [US2] Add a native test "sound banks" in `tests/audio/native/NativeBackendTests.cpp`:
  - load bank A (clips X, Y) and bank B (Y, Z);
  - unload A: X is freed, Y stays;
  - play Z, then unload B: Z stays until the voice ends, then it is freed;
  - check `NativeInternal` clip memory at each step.
- [x] T011 [US2] Add a native test "pack stream" in `tests/audio/native/NativeBackendTests.cpp`: write a minimal pack that holds a 5-minute WAV, stream it through the pack VFS, and assert audio memory stays under 1 MB and the samples match the source
- [x] T012 [US2] Add a native test "pack stream stress" in `tests/audio/native/NativeBackendTests.cpp`: 16 concurrent streams from one pack while another thread reads the same pack file. No read errors, and each stream's samples match

### Implementation for User Story 2

- [x] T013 [US2] Add the clip cache to `Core/Source/Lux/Audio/Native/NativeAudioEventInstance.cpp`, with its API in `Core/Source/Lux/Audio/Native/NativeInternal.h`.
  - Each entry has `Clip`, `BankRefs`, `LiveRefs`, `Resident` and `Bytes`.
  - `Leaf` creation and destruction increment and decrement `LiveRefs`.
  - When both counts reach 0, unregister the clip from miniaudio, clear `AudioFile::EncodedData`, and call `AssetManager::RemoveAsset` on the AudioFile. "A release never runs while `LiveRefs > 0`. It is checked again when the last voice ends."
  - `Bytes` replaces the running `s_ClipMemoryBytes` total.
- [x] T014 [US2] Add bank loading to `Core/Source/Lux/Audio/Native/NativeAudioEngine.cpp`, declared in `Core/Source/Lux/Audio/AudioEngine.h`: `LoadSoundBank(AssetHandle)`, `UnloadSoundBank(AssetHandle)` and `IsSoundBankLoaded(AssetHandle)`.
  - Load and unload are counted per bank.
  - Loading preloads every non-stream clip of the bank's Sounds on the asset thread, and only registers stream clips by their VFS name.
  - Loading and unloading change `BankRefs`.
  - Main thread only; follow `.claude/docs/Threading.md` for the asset-thread handoff.
- [x] T015 [US2] Load every `Resident` bank once the project's assets are available and before the start scene runs (`NativeAudioEngine.cpp`, called from the same place runtime audio settings are applied). Never unload them
- [x] T016 [US2] Add `Scene::SoundBanks` (a list of `AssetHandle`) to `Core/Source/Lux/Scene/Scene.h`:
  - YAML key `SoundBanks` in `Core/Source/Lux/Scene/SceneSerializer.cpp`, beside `PostProcess`, both directions;
  - copied in `Scene::Copy` / `CopyTo` in `Core/Source/Lux/Scene/Scene.cpp`;
  - `OnRuntimeStart` loads the listed banks before scripts' `OnCreate`, and `OnRuntimeStop` unloads them;
  - `Scene::GetAssetList` adds each bank and its Sounds.
- [x] T017 [US2] In `NativeAudioEventInstance.cpp`, when a Sound plays and none of its clips is held by a loaded bank, load it on demand. A Debug build logs `Sound <name> played outside any loaded bank` once per Sound
- [x] T018 [US2] Pack audio entries, runtime format 31:
  - each Audio entry gets `Offset`, `Size`, `Extension` and `Stream` in `Core/Source/Lux/Serialization/AssetPackFile.h`;
  - `Core/Source/Lux/Serialization/AssetPackSerializer.cpp` and `Core/Source/Lux/Asset/AudioAssetSerializer.cpp` write them; `Stream` is set when any Sound streams the clip;
  - bump `Core/Source/Lux/Project/ProjectRuntimeFormat.h` to 31;
  - readers for 17–30 keep the bytes-after-path layout.
- [x] T019 [US2] Create `Core/Source/Lux/Audio/Native/NativePackVFS.cpp` (declared in `NativeInternal.h`): an `ma_vfs` that opens `luxpack/<handle><ext>` at the entry's offset.
  - Each open stream gets its own file handle.
  - `read` / `seek` / `tell` are clamped to `[Offset, Offset + Size)`; `info` reports `Size`.
  - Any other name falls through to the default VFS.
  - Install it in the resource manager config in `NativeAudioEngine.cpp`.
- [x] T020 [US2] In `Core/Source/Lux/Asset/AudioAssetSerializer.cpp`, fill `EncodedData` only for non-stream entries. In `Core/Source/Lux/Audio/Native/NativeSoundResolver.cpp`, have `ResolveClip` return the `luxpack/` name for stream entries without reading them
- [x] T021 [P] [US2] Create the Sound Bank editor panel in `Editor/Source/Panels/SoundBankEditorPanel.h` and `Editor/Source/Panels/SoundBankEditorPanel.cpp`:
  - add Sounds by drag-and-drop; remove; the Resident toggle;
  - total clip memory; an "unbanked Sounds" list for the project; undo through `EditorStack`.
  - Register it in `Editor/Source/EditorLayer.cpp` like `SoundEditorPanel`, and open it on double-click of an `.lbank`.
  - Audit it against `Conventions.md` § ImGui correctness.
- [x] T022 [P] [US2] In `Editor/Source/Panels/SoundEditor/SoundEditorPanel.cpp`, show which banks hold the Sound (or "loads on first play"), with an **Add to bank** picker (FR-004)
- [x] T023 [P] [US2] Add a "Scene Sound Banks" section to `Editor/Source/Panels/AudioMixerPanel.cpp` that edits the active scene's `SoundBanks` list, with undo
- [x] T024 [US2] Export validation in `Core/Source/Lux/Audio/Native/NativeAudioValidation.cpp`:
  - a warning for each Sound a scene references that no bank holds;
  - an error for a bank that names a missing Sound;
  - an error for a scene that lists a missing bank.
- [x] T025 [US2] Sample data:
  - create `Editor/LuxSampleProject/Assets/Audio/Banks/AudioDemo.lbank` holding Fart (9117899157295655424) and Music (8743721474366396702), with its registry entry;
  - list it under `SoundBanks` in `Editor/LuxSampleProject/Assets/Scenes/AudioDemo.luxscene` and `Benchmark.luxscene`;
  - add it to `scripts/GenerateBenchmarkScenes.py`.
- [ ] T026 [US2] Regenerate premake. Run the Debug build, the native tests (T009–T012) and `python tests/yaml/run.py`; update the AudioDemo, Benchmark and registry lines in `tests/yaml/reference.txt`. Update `.claude/docs/Architecture-LuxEngine.md` §2.10 with Sound Banks, the clip cache, the pack VFS and format 31. Then **(editor)** export and run the sample per `quickstart.md` Phases 1–2

**Checkpoint**: banks load and free clips, streamed music reads from the pack, and the sample uses a bank.

**Done (2026-11-03), T010–T025; T026's editor export is still open.** Deviations:
- **T018:** no format bump. The pack already stores each clip's bytes after its path; the reader now
  records `AudioFile::PackFile` / `PackOffset` instead of reading them, so formats 17–30 load unchanged.
- **T013 and T020:** clips never sit in memory as encoded bytes (the VFS reads them in place), so
  nothing is removed from the asset manager. Decoded clips are held by resource-manager data sources
  in `NativeClipCache.cpp`, not in `NativeAudioEventInstance.cpp`.
- **T016:** `Scene::GetAssetList` is unchanged; every scene's pack lists all Sound Banks instead
  (`AssetPack.cpp`), so Resident banks no scene names ship too.
- **T015:** Resident banks load in `Scene::OnRuntimeStart`; the clip cache keeps them loaded until
  shutdown whatever unloads follow.
- A Sound played outside any loaded bank stays decoded until the scene stops
  (`AudioEngine::ReleaseUnusedClips`), not until its voice ends, so replaying it does not decode again.
- The stream memory figure is an upper bound: two pages at the engine rate, in stereo.

---

## Phase 4: User Story 1 — A game ships with no FMOD anywhere (Priority: P1)

**Goal**: Script API on Sound paths and banks, editor without FMOD, the sample without the Studio project, and FMOD deleted (plan Phases 4–6, D6).

**Independent Test**:
- A fresh clone builds with default options and the sample plays and exports.
- `git grep -i fmod` finds only legacy readers, migration messages and plan records (SC-001, SC-002).

### Script API (D4 = c) — `contracts/scripting-api.md`

- [x] T027 [US1] Add a path table to the pack in `Core/Source/Lux/Serialization/AssetPackFile.h` and `AssetPackSerializer.cpp`: "Assets-relative path without extension, `/`-separated, case-sensitive", mapping to the handle, "for Sound and SoundBank assets only". In `Core/Source/Lux/Audio/Native/NativeSoundResolver.cpp`, `FindSound` resolves through this table at runtime and through `EditorAssetManager` in the editor, so both give the same answer
- [x] T028 [US1] Internal calls in `Core/Source/Lux/Scripting/AudioScriptBindings.cpp`:
  - `Audio_LoadBank`, `Audio_UnloadBank` and `Audio_IsBankLoaded` take a handle or a path (T027);
  - string `PlayOneShot` / `CreateInstance` and the zone ambience take Sound paths;
  - an `event:/` string fails with "FMOD events are gone: assign a Sound";
  - a `.bank` path fails with "FMOD banks are gone: make a Sound Bank";
  - remove `Audio_PlaysSoundAssets`.
- [x] T029 [US1] Update `ScriptCore/Source/Lux/Audio.cs` and `ScriptCore/Source/Lux/InternalCalls.cs`:
  - `LoadBank(SoundBank)`, `LoadBank(string)`, `UnloadBank(...)` and `IsBankLoaded(SoundBank)`, behaving as in the contract's "Added with Sound Banks" table;
  - string overloads documented as Sound paths;
  - `PlaysSoundAssets` becomes `[Obsolete("Always true: FMOD is gone.", true)]`.
- [x] T030 [P] [US1] Update `ScriptCore/Source/Lux/Scene/Components.cs`:
  - `AudioSourceComponent.Sound` property and `SetSound(string path)`;
  - `SetEvent(string)` becomes `[Obsolete("Use SetSound(path) or the Sound property.", true)]`;
  - `AudioZoneComponent.SetAmbience(string)` takes a Sound path;
  - keep `SetSnapshot` (mixer snapshot) and `bus:/` APIs as they are.
- [x] T031 [US1] `Editor/LuxSampleProject/Assets/Scripts/Source/AudioDemo.cs`:
  - remove `StringsBank`, `MasterBank`, `EventPath` and every FMOD branch;
  - call `Audio.LoadBank("Audio/Banks/AudioDemo")` in `OnCreate`.
  - Then build `LuxSample.csproj`, regenerate `Editor/Resources/Docs` with `tools/ScriptApiDocs`, and pass `--check`.

### Sample without the Studio project (D6)

- [x] T032 [US1] Move the clips the Sounds use (`fart-01`, `Music_1.mp3`, now under `Editor/LuxSampleProject/Assets/Audio/SampleProject/Assets/FX/`) to `Editor/LuxSampleProject/Assets/Audio/Clips/` with `git mv`, keeping their handles (update `FilePath` in `Assets/AssetRegistry.lzr`, staged without the local Bench_044 entry). Delete `Editor/LuxSampleProject/Assets/Audio/SampleProject/`
- [x] T033 [US1] Strip FMOD data from the sample:
  - `EventGuid`, `EventPath` and `EventBank` (and nested `Guid` / `Path` / `BankName`) in `AudioDemo.luxscene` and `Benchmark.luxscene`;
  - the `EVENT_*` GUIDs in `scripts/GenerateBenchmarkScenes.py` (sources keep `EventSound` / `Sound`);
  - the Studio fields in `Editor/LuxSampleProject/LuxSample.luxproj`.
  - Update the `tests/yaml/reference.txt` lines.

### Editor without FMOD (FR-007, FR-008)

- [x] T034 [P] [US1] Remove from `Editor/Source/Panels/ProjectSettingsWindow.cpp`: the Studio section (project path, bank output, platform, desktop profiles, Live Update, Rebuild Banks On Play, Build Banks Now) and the FMOD dialogue key fields. The native Clip picker stays
- [x] T035 [P] [US1] Replace the bank and event lists in `Editor/Source/Panels/AudioDebugPanel.cpp` with **Sound Banks and clips**: each loaded bank with its memory and load count; each clip with its Sound, memory, streaming state, `BankRefs` and `LiveRefs` (from T013)
- [x] T036 [P] [US1] Remove event GUID pickers and fields from `Core/Source/Lux/Editor/SceneHierarchyPanel.cpp` and `Core/Source/Lux/ImGui/AudioWidgets.cpp`, leaving the Sound pickers and the migration warning
- [x] T037 [US1] In `Editor/Source/EditorLayer.cpp` and `Editor/Source/RuntimeExportUtils.cpp`, remove bank loading on project open, the bank rebuild on Play, `PrepareAudioBanks`, `CopyAudioBanks`, `CopyAudioLibraries` and the FMOD half of `ShipsLibrary`
- [x] T038 [P] [US1] Remove `.bank` from `Core/Source/Lux/Asset/AssetExtensions.h`, and `AssetType::AudioBank` (plus the `.fspro` type) from `Core/Source/Lux/Asset/AssetTypes.h`. The registry loader reports an unknown-type entry with one warning instead of failing; check `Core/Source/Lux/Asset/AssetManager/` and the registry loader

### Delete FMOD (D5 = a)

- [x] T039 [US1] Port or delete the FMOD-hosted tests that use `tests/audio/AudioTestHost.h`: `AccessibilityTests.cpp`, `DialogueTests.cpp`, `GeometryTests.cpp`, `MusicTests.cpp`, `PerformanceTests.cpp`, `PhysicsAudioTests.cpp`, `PlatformTests.cpp`, and `tests/audio/AuthorFixture.js`. Port each behaviour native lacks a test for into `tests/audio/native/NativeBackendTests.cpp`; list each file's fate with a reason in `specs/002-fmod-replacement/report.md`; update `tests/audio/run.py` and `tests/audio/README.md`
- [x] T040 [US1] In `Core/Source/Lux/Scene/SceneAudioVerification.cpp`, remove the `LUX_ENABLE_FMOD` block, and have the `--play` check verify native parameters through `AudioEventInstance`. Update `tests/rendering/golden_run.py` if a report key changes
- [x] T041 [US1] Delete `Core/Source/Lux/Audio/FMOD/`, `Core/Source/Lux/Audio/AudioBankBuilder.{h,cpp}` and the writer half of `AudioBankManifest`. The reader stays for formats 17–30 and logs once: "This export carries FMOD banks, which are ignored; re-export to drop them." Remove the bank API from `Core/Source/Lux/Audio/AudioEngine.h` and its stubs in `NativeAudioEngine.cpp`
- [x] T042 [US1] Delete `enum class AudioBackend`, `AudioEngine::GetBackend()`, `Core/Source/Lux/Audio/Native/NativeAudioBackend.cpp` and `Core/Source/Lux/Audio/FMOD/FMODAudioBackend.cpp`. At each of the 18 callers from T003, keep only the native branch
- [x] T043 [US1] Build system:
  - remove the `audio-backend` option from `premake5.lua`;
  - in `Dependencies.lua`, remove the FMOD entries, `FMODSDKRoot`, `ValidateAudioSDK` / `ValidateFMODSDK`, and make `Miniaudio` / `MiniaudioCpp` unconditional;
  - remove the backend `removefiles` branches in `Core/premake5.lua`;
  - remove `fmod-audio` from `scripts/BuildOptions.py`, and `warn_missing_fmod_sdk` from `scripts/Configure.py` with its calls in `scripts/Setup.py` and `scripts/Win-GenProjects.py`;
  - delete `tests/audio/run_sdk_layout.py` and the CI step "Test dependency layouts";
  - regenerate premake.
- [x] T044 [US1] CI: delete the `build-fmod-windows` and `build-fmod-linux` jobs in `.github/workflows/main.yml`, and delete `scripts/ci/StageAudioSDKs.py`. Update the CI section of `.claude/docs/Building.md` (no audio SDK, no `AUDIO_SDK_*` variable or secret)
- [x] T045 [US1] Docs:
  - delete `docs/Editor/FMOD.md`;
  - remove the README's "Optional: the FMOD backend" section and the `--audio-backend` premake option row;
  - update `docs/Editor/Audio.md`, `docs/AUDIO_DIALOGUE.md`, `Editor/LuxSampleProject/AUDIO_DEMO.md` (banks), `CLAUDE.md` (Build options and Audio), `.claude/docs/Architecture-LuxEngine.md` §2.10 (one backend, no seam) and `.claude/docs/Building.md` (the option table), and FMOD mentions in `NOTICE`.
- [x] T046 [US1] Verify:
  - run `git grep -n -i fmod -- . ':!docs/*_PLAN.md' ':!specs' ':!docs/Lux-Commit-History.json'`; only legacy readers and migration messages may remain (SC-001);
  - in a fresh clone (`git clone` into `bin/fresh-clone`), with default options, run `Win-GenProjects.py` and the Debug build (SC-002);
  - run the native tests.

**Checkpoint**: nothing in the build, the editor, the sample, CI or the docs uses FMOD. Deviations are in `report.md` § Phase 4.

---

## Phase 5: User Story 3 — Old projects open and say what to fix (Priority: P2)

**Goal**: FMOD-era data loads, is reported, and one action removes it (D3 = b, SC-006).

**Independent Test**: a pre-Phase-9 copy of the sample lists every FMOD reference, and one action leaves zero.

- [x] T047 [P] [US3] Add a native test "legacy cleanup" in `tests/audio/native/NativeBackendTests.cpp`: a scene YAML with Guid-only sources, a zone and a music director, and a project YAML with Studio fields. The scan lists each reference; the cleanup leaves none; the result re-serializes without FMOD keys
- [x] T048 [US3] Legacy readers:
  - `Core/Source/Lux/Audio/AudioEventRefSerializer.cpp` keeps reading `Guid` / `Path` / `BankName` and writes them only while non-empty;
  - `Core/Source/Lux/Project/ProjectSerializer.cpp` reads the Studio fields, ignores them, and logs one info line "FMOD Studio settings are ignored; use Project Settings › Audio › Remove FMOD data".
- [x] T049 [US3] Create the scanner and cleanup in `Core/Source/Lux/Audio/AudioLegacyCleanup.h` and `Core/Source/Lux/Audio/AudioLegacyCleanup.cpp`:
  - `Scan(project)` returns each FMOD reference (scene, entity, component, field; project field; registry entry);
  - `Apply(scan)` clears them and saves each touched scene and the project once.
  - The scenes are read without opening them in the editor.
- [x] T050 [US3] In `Editor/Source/Panels/ProjectSettingsWindow.cpp` § Audio, add a **Remove FMOD data** button: show the scan as a list first, apply on confirm, and push an undo entry for the open scene. Audit it against ImGui correctness
- [x] T051 [US3] **(editor)** Check out `0a3dfa00^` into a scratch folder, open its sample, run the action, and record SC-006 in `specs/002-fmod-replacement/report.md`

**Checkpoint**: old projects load, report and clean up.

---

## Phase 6: User Story 4 — Remaining parity gaps closed (Priority: P3)

**Goal**: the music timeline view, dialogue clip drag-and-drop with a play button, editor validation, and live edits during Play.

**Independent Test**: each feature is driven once in the editor (with the owner's OK).

- [ ] T052 [P] [US4] Add a music timeline view to `Editor/Source/Panels/SoundEditor/SoundEditorPanel.cpp`: the clip's waveform with `Sound::Tempo` bar lines and `Markers` drawn on it; drag a marker to move it, with undo
- [ ] T053 [P] [US4] In the dialogue section of `Editor/Source/Panels/ProjectSettingsWindow.cpp`, accept an Audio asset dropped on a translation's Clip field, and add a play button that previews the line through the native backend
- [ ] T054 [US4] If T002 found that Sound edits do not reach playing voices: on Sound save, update playing instances in `Core/Source/Lux/Audio/Native/NativeAudioEventInstance.cpp` (volume, pitch, curves, bus; a tree change restarts the instance). Mixer **Apply** in `AudioMixerPanel.cpp` already rebuilds the mixer; check that it keeps voices playing
- [ ] T055 [P] [US4] Add **Validate project audio** to `Editor/Source/Panels/AudioDebugPanel.cpp`. It runs the export's audio validation (T024) and lists the issues
- [ ] T056 [US4] **(editor)** Drive T052–T055 once on the sample and note the result in `specs/002-fmod-replacement/report.md`

**Checkpoint**: every FMOD-era editor feature has a native counterpart.

---

## Phase 7: Polish & cross-cutting

- [ ] T057 [P] Mark `docs/AUDIO_NATIVE_BACKEND_PLAN.md` Phase 10 as superseded by `specs/002-fmod-replacement` (a status line only; it is a point-in-time record)
- [ ] T058 [P] Check `.claude/docs/Threading.md` names the miniaudio job threads that call the pack VFS, and add a line if not
- [ ] T059 **(editor)** End to end:
  - play AudioDemo and Benchmark;
  - export, and run the export in Lux-Runtime for 30 s;
  - record SC-001 to SC-006 against the T001 baseline in `specs/002-fmod-replacement/report.md`.
- [ ] T060 The owner's listening check: music muffling through walls, Fart pitch variation, a bank load mid-game

---

## Dependencies & Execution Order

- **Setup (T001–T003)** runs first. T002 and T003 are parallel. T001 needs the editor OK.
- **Foundational (T004–T009)** blocks every story. T007 and T008 run parallel to T005–T006 once T004 is done.
- **US2 (Phase 3)** depends on Foundational.
  - Tests T010–T012 are written first.
  - T013 → T014 → T015–T017.
  - T018 → T019 → T020.
  - The UI (T021–T023) is parallel to the runtime work.
  - T024 → T025 → T026.
- **US1 (Phase 4)** depends on US2:
  - T027–T031 depend on T014 (bank loading) and T018 (pack format);
  - T032–T038 depend only on Foundational and can start earlier;
  - the FMOD deletion T039–T046 runs last, port (T039) before delete (T041).
- **US3 (Phase 5)** depends on Foundational only; T048 must land before T041 deletes anything it reads.
- **US4 (Phase 6)** depends on US2 (T055 uses T024) and T002 (for T054).
- **Polish** comes last.

## Parallel Examples

```text
# US2, after T004–T009:
T021, T022, T023        (three editor panels, different files)

# US1:
T030 (Components.cs) beside T029 (Audio.cs)
T034, T035, T036, T038  (different files)

# US3 beside US1's editor work:
T047, then T049 beside T034–T038
```

## Implementation Strategy

1. **MVP = Setup + Foundational + US2.** Sound Banks and streaming work, and the sample uses a bank.
   Memory now falls instead of only growing. Stop and verify (T026) before going on.
2. **US1** removes FMOD. Do the script API and sample first, then the editor, then delete FMOD.
   That deletion is the point of no return; note the last commit before it in `report.md`.
3. **US3** can run beside US1's editor work. It must land before T041.
4. **US4 and Polish** close the gaps.
5. Each checkpoint: build, native tests, `/cr`, commit and push.
