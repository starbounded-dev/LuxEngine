# Report: Full FMOD Replacement

Point-in-time results, filled in phase by phase. Windows, Debug build, RTX GPU, 2026-11-03.

## Phase 1 (Setup) — baseline before any change (HEAD efd8bb4e)

### T001: baseline export

Commands:
- `golden_run.py --config debug --label base --scenes AudioDemo --play --export-dir bin/fmodless-base/AudioDemo`
  (the start scene was pointed at AudioDemo for this run only, then reset);
- the export run with `LUX_RUNTIME_AUDIO_REPORT`.

`SceneAudioVerification` now writes `AudioClipMemoryMiB` (from `AudioPerformance` stats) beside
`AudioCPUPercent`, so every later phase reports the same figure.

| Figure | Editor Play | Exported game |
|---|---|---|
| Clip memory reported (`AudioClipMemoryMiB`) | 0.46 MiB | **48.06 MiB** |
| Audio CPU | 3.91 % | 0.65 % |
| Frame time (Debug, golden capture) | median 47.6 ms, p95 50.3 ms | — |
| `AssetPack.lap` | — | 412.5 MB (every scene's assets) |

What the 48 MiB is:
- **Held all game:** the encoded bytes of both clips, read whole from the pack (fart-01.wav
  0.34 MiB + Music_1.mp3 3.04 MiB).
- **Estimated:** `RegisterPackedClip` (`NativeAudioEventInstance.cpp:834`) adds each packed clip's
  full decoded size (frames × channels × 4 bytes). That is right for the decoded Fart, but the Music
  Sound streams, so its about 44 MiB "decoded" share is an estimate, not a measured allocation. T013
  replaces the running total with per-clip `Bytes` that count what is actually held.
- In editor Play the music streams from its loose file and is not counted. Only the decoded Fart
  (0.46 MiB) shows.

**Freeing:** the counter `s_ClipMemoryBytes` never decreases. `RuntimeAssetManager` frees loaded
assets only at `Shutdown`, so no scene switch can lower audio memory. This is read from the code: the
sample has no scene-switch flow to measure, and T010 adds the test that does.

**First-play frame time (SC-005): not measured.** Neither the golden capture (its timing starts
after the image capture) nor the runtime report records the frame on which the Fart first plays
(`OnCreate`). T026 adds a frame-time check around the first play once preloading exists.

### T002: do Sound edits reach playing voices?

**No.** Each instance copies its Sound when it is created (`NativeAudioEventInstance.cpp:156`
`Definition` "a private copy"; `:1051`), and `SoundEditorPanel::SaveDocument` only re-serializes the
asset. An edit applies to the next instance. **T054 (live edits, D7 b) is needed.**

### T003: FMOD inventory

- 120 files outside `Core/Source/Lux/Audio/FMOD/` and `Core/vendor` mention FMOD (the list follows;
  many are comments, generated API pages and sample metadata).
- 18 files call `AudioEngine::GetBackend()`. T042 keeps only the native branch in each:

  - `Core/Source/Lux/Asset/AudioAssetSerializer.cpp`
  - `Core/Source/Lux/Audio/AudioEngine.h`
  - `Core/Source/Lux/Audio/AudioValidation.cpp`
  - `Core/Source/Lux/Audio/DialogueDirector.cpp`
  - `Core/Source/Lux/Audio/FMOD/FMODAudioBackend.cpp`
  - `Core/Source/Lux/Audio/Native/NativeAudioBackend.cpp`
  - `Core/Source/Lux/Editor/SceneHierarchyPanel.cpp`
  - `Core/Source/Lux/ImGui/AudioWidgets.cpp`
  - `Core/Source/Lux/Scene/Scene.cpp`
  - `Core/Source/Lux/Scripting/AudioScriptBindings.cpp`
  - `Editor/Source/EditorLayer.cpp`
  - `Editor/Source/Panels/AudioDebugPanel.cpp`
  - `Editor/Source/Panels/AudioMixerPanel.cpp`
  - `Editor/Source/Panels/ContentBrowser/ContentBrowserItem.cpp`
  - `Editor/Source/Panels/ContentBrowserPanel.cpp`
  - `Editor/Source/Panels/ProjectSettingsWindow.cpp`
  - `Editor/Source/Panels/SoundEditor/SoundEditorPanel.cpp`
  - `Editor/Source/RuntimeExportUtils.cpp`

<details><summary>The 120 files that mention FMOD</summary>

- `.github/workflows/main.yml`
- `Core/Source/Lux/Asset/AssetManager/EditorAssetManager.cpp`
- `Core/Source/Lux/Asset/AssetTypes.h`
- `Core/Source/Lux/Asset/AudioAssetSerializer.cpp`
- `Core/Source/Lux/Audio/AudioAccessibilityMixer.h`
- `Core/Source/Lux/Audio/AudioAccessibilitySettings.h`
- `Core/Source/Lux/Audio/AudioBankBuilder.cpp`
- `Core/Source/Lux/Audio/AudioBankBuilder.h`
- `Core/Source/Lux/Audio/AudioEngine.h`
- `Core/Source/Lux/Audio/AudioEventInstance.h`
- `Core/Source/Lux/Audio/AudioEventRef.h`
- `Core/Source/Lux/Audio/AudioFileUtils.cpp`
- `Core/Source/Lux/Audio/AudioFileUtils.h`
- `Core/Source/Lux/Audio/AudioMixerSettings.h`
- `Core/Source/Lux/Audio/AudioSourcePlayback.cpp`
- `Core/Source/Lux/Audio/AudioValidation.cpp`
- `Core/Source/Lux/Audio/AudioValidation.h`
- `Core/Source/Lux/Audio/AudioZoneSystem.cpp`
- `Core/Source/Lux/Audio/DialogueDirector.cpp`
- `Core/Source/Lux/Audio/DialogueDirector.h`
- `Core/Source/Lux/Audio/DialogueTable.h`
- `Core/Source/Lux/Audio/MusicDirector.cpp`
- `Core/Source/Lux/Audio/Native/NativeAudioAccessibilityMixer.cpp`
- `Core/Source/Lux/Audio/Native/NativeAudioBackend.cpp`
- `Core/Source/Lux/Audio/Native/NativeAudioEngine.cpp`
- `Core/Source/Lux/Audio/Native/NativeAudioEventInstance.cpp`
- `Core/Source/Lux/Audio/Native/NativeAudioPerformance.cpp`
- `Core/Source/Lux/Audio/Native/NativeAudioValidation.cpp`
- `Core/Source/Lux/Audio/Native/NativeInternal.h`
- `Core/Source/Lux/Audio/Native/NativeSoundResolver.cpp`
- `Core/Source/Lux/Audio/Sound.cpp`
- `Core/Source/Lux/Audio/Sound.h`
- `Core/Source/Lux/Editor/SceneHierarchyPanel.cpp`
- `Core/Source/Lux/Editor/SceneHierarchyPanel.h`
- `Core/Source/Lux/ImGui/AudioAccessibilityWidgets.cpp`
- `Core/Source/Lux/ImGui/AudioWidgets.h`
- `Core/Source/Lux/ImGui/ImGuizmo.cpp`
- `Core/Source/Lux/Physics/JoltPhysics/JoltBody.cpp`
- `Core/Source/Lux/Physics/JoltPhysics/JoltBody.h`
- `Core/Source/Lux/Physics/JoltPhysics/JoltContactListener.h`
- `Core/Source/Lux/Physics/PhysicsBody.h`
- `Core/Source/Lux/Physics/PhysicsScene.cpp`
- `Core/Source/Lux/Physics/PhysicsScene.h`
- `Core/Source/Lux/Physics/PhysicsTypes.h`
- `Core/Source/Lux/Project/Project.h`
- `Core/Source/Lux/Project/ProjectSerializer.cpp`
- `Core/Source/Lux/Scene/Components.h`
- `Core/Source/Lux/Scene/Scene.cpp`
- `Core/Source/Lux/Scene/SceneAudioSurfaces.cpp`
- `Core/Source/Lux/Scene/SceneAudioVerification.cpp`
- `Core/Source/Lux/Scene/SceneAudioVerification.h`
- `Core/Source/Lux/Scene/SceneSerializer.cpp`
- `Core/Source/Lux/Scripting/AudioScriptBindings.cpp`
- `Core/Source/Lux/Vendor/TextEditor.cpp`
- `Core/premake5.lua`
- `Editor/LuxSampleProject/Assets/Audio/SampleProject.fspackage`
- `Editor/LuxSampleProject/Assets/Environment/dikhololo_night_4k.hdr`
- `Editor/LuxSampleProject/Assets/Environment/pink_sunrise_4k.hdr`
- `Editor/LuxSampleProject/Assets/Environment/rooitou_park_4k.hdr`
- `Editor/LuxSampleProject/Assets/Environment/venice_dawn_1_4k.hdr`
- `Editor/LuxSampleProject/Assets/Meshes/Source/TestScene.blend`
- `Editor/LuxSampleProject/Assets/Scenes/AudioDemo.luxscene`
- `Editor/LuxSampleProject/Assets/Scripts/Source/AudioDemo.cs`
- `Editor/Resources/Docs/api/Accessibility.md`
- `Editor/Resources/Docs/api/Audio.md`
- `Editor/Resources/Docs/api/AudioCategory.md`
- `Editor/Resources/Docs/api/AudioListenerComponent.md`
- `Editor/Resources/Docs/api/AudioSourceComponent.md`
- `Editor/Resources/Docs/api/Dialogue.md`
- `Editor/Resources/Docs/api/Music.md`
- `Editor/Resources/Docs/api/Subtitle.md`
- `Editor/Resources/Docs/api/index.md`
- `Editor/Resources/Docs/components/audio.md`
- `Editor/Resources/Docs/editor/audio-authoring.md`
- `Editor/Resources/Docs/editor/export.md`
- `Editor/Resources/Docs/index.md`
- `Editor/Source/EditorLayer.cpp`
- `Editor/Source/EditorLayer.h`
- `Editor/Source/Panels/AudioDebugPanel.cpp`
- `Editor/Source/Panels/AudioDebugPanel.h`
- `Editor/Source/Panels/AudioMixerPanel.cpp`
- `Editor/Source/Panels/ContentBrowser/ContentBrowserItem.cpp`
- `Editor/Source/Panels/ContentBrowser/WaveformThumbnailer.h`
- `Editor/Source/Panels/ContentBrowserPanel.cpp`
- `Editor/Source/Panels/ProjectSettingsWindow.cpp`
- `Editor/Source/Panels/TextEditorPanel.cpp`
- `Editor/Source/Panels/TextEditorPanel.h`
- `Editor/Source/RuntimeExportUtils.cpp`
- `Editor/Source/RuntimeExportUtils.h`
- `Editor/Source/Tools/GoldenCapture.h`
- `Editor/premake5.lua`
- `Lux-Runtime/premake5.lua`
- `ScriptCore/Source/Lux/Accessibility.cs`
- `ScriptCore/Source/Lux/Audio.cs`
- `ScriptCore/Source/Lux/Dialogue.cs`
- `ScriptCore/Source/Lux/Music.cs`
- `ScriptCore/Source/Lux/Scene/Components.cs`
- `scripts/BuildOptions.py`
- `scripts/Configure.py`
- `scripts/GenerateBenchmarkScenes.py`
- `scripts/Setup.py`
- `scripts/Win-GenProjects.py`
- `scripts/ci/StageAudioSDKs.py`
- `tests/audio/AccessibilityTests.cpp`
- `tests/audio/AudioTestHost.h`
- `tests/audio/AudioTestLog.h`
- `tests/audio/DialogueTests.cpp`
- `tests/audio/MusicTests.cpp`
- `tests/audio/PerformanceTests.cpp`
- `tests/audio/PhysicsAudioTests.cpp`
- `tests/audio/PlatformTests.cpp`
- `tests/audio/README.md`
- `tests/audio/fixtures/README.md`
- `tests/audio/native/NativeBackendTests.cpp`
- `tests/audio/native/README.md`
- `tests/audio/run.py`
- `tests/audio/run_scene_acoustics.py`
- `tests/audio/run_sdk_layout.py`
- `tests/audio/run_soundwave_gpu.py`
- `tests/rendering/golden_run.py`

</details>

## Phase 3 (US2) — Sound Banks and pack streaming (4ba4c8ef)

T026's export check, same commands as T001 (`golden_run.py --label p3`):

| Figure | Baseline (T001) | With Sound Banks |
|---|---|---|
| Clip memory, editor Play | 0.46 MiB | 1.19 MiB (decoded Fart 0.46 + the music's two stream pages, an upper bound, 0.73) |
| Clip memory, exported game | **48.06 MiB** | **1.19 MiB** |
| Audio validation issues | 9 (Benchmark emitters with no Sound) | 2 (AudioTest scene only) |
| FMOD libraries in the export | none | none |

- No "played outside any loaded bank" warning: AudioDemo's bank loads before the demo plays.
- The exported game's audio verification passed (status OK).
- The export step again re-saved `AudioDemo.luxscene` in a new entity order (no content change); reverted.
- **Not verified:** the three new panels (Sound Bank Editor, the Sound Editor's Sound Banks section, the
  mixer's Scene Sound Banks) were not clicked through; computer-use access to the editor was declined.
  They compile, and their ImGui scopes were audited by reading.

## Phase 4 (US1) — no FMOD anywhere

T027–T046 are done. Where the work differs from tasks.md:

- **T027:** the Sound and Sound Bank path table lives in the runtime project (format 31,
  `ProjectAudioSettings::AudioAssetPaths`), not in the pack: the project is already loaded before any
  scene pack, and the pack format stays unchanged. The editor resolves through the asset registry; both
  give the same answer.
- **T028:** the refusals say "it is an FMOD event reference; FMOD is gone, assign a Sound asset" and
  "it is an FMOD bank; FMOD is gone, make a Sound Bank (New > Sound Bank) and load it by its path".
- **T033:** the sample project's Studio keys were removed by hand. Desktop profiles no longer have
  `StudioPlatform` / `BankOutputPath`: old files load with the keys skipped, and the next save drops them.
- **T038:** `AssetType::AudioProject` / `AudioBank` stay in the enum (numeric stability for old registries);
  `.fspro` and `.bank` are no longer imported. The Content Browser hides Studio project folders.
- **T039, test fates:**

  | File | Fate | Why |
  |---|---|---|
  | `AccessibilityTests.cpp` | deleted, partly ported | settings round trips: `TestProjectAudioSettings`; mono, compression and category gains: `TestAccessibility`. The caption and cue service (`AudioAccessibility`) has **no native test** |
  | `DialogueTests.cpp` | deleted, partly ported | table round trips: `TestProjectAudioSettings`; clip overrides: `TestDialogueClips`. `DialogueDirector` queueing, barks and subtitles have **no native test** |
  | `MusicTests.cpp` | deleted, ported | timeline notifications and `MusicDirector` transitions: `TestMusicTimeline`; the director's YAML still runs in `run.py` |
  | `PerformanceTests.cpp` | deleted, ported | settings round trips: `TestProjectAudioSettings`; voice budget and virtualization: `TestVoiceBudget` |
  | `PhysicsAudioTests.cpp` | deleted, ported | `TestPhysicsAudio` |
  | `PlatformTests.cpp` | deleted, ported | profiles' budgets: `TestProjectAudioSettings`; focus mute: `TestVolumeAndBuses` |
  | `GeometryTests.cpp`, `GeometrySerializationTests.cpp`, `SourceSerializationTests.cpp` | deleted, **not ported** | they ran inside the FMOD host; the portal/source YAML round trips and shutter/portal geometry checks have no runner now |
  | `AudioTestHost.h`, `AuthorFixture.js`, `fixtures/`, `run_sdk_layout.py` | deleted | FMOD host, Studio fixture author, built banks, SDK layout check |

- **T040:** the report key is now `CheckedSources` (the acoustics each source was given);
  `run_scene_acoustics.py` reads it.
- **T041:** `DialogueTranslation::AudioKey` is kept as legacy data (read, saved, never played), and
  `AudioEventInstance::SetProgrammerSound` stays in the header as a stub that fails, so old scripts compile.
  The runtime project still writes an empty `AudioBankManifest` so its layout does not change.
- **T043–T044:** `tests/audio/run.py` and `scripts/Linux-Build.sh` were edited for Linux but not run
  (Linux builds are deferred). The CI artifact checks still fail if an FMOD library is packaged.
- Commit 73dbe5e0's message says the FMOD build "still builds"; it was not compiled with FMOD then.

T046 results:

- SC-001 grep: what remains is legacy readers and migration messages (`AudioEventRef`, the resolver's
  refusals, `ProjectSerializer`, `Project::LoadRuntime`, `Scene.cpp`, the inspector warning, the obsolete
  C# members, `RuntimeExport::ShipsLibrary`, the CI artifact guard, `.gitignore`'s `Core/vendor/FMOD/`),
  point-in-time docs with a banner, and `std::fmod`.
- The native tests pass (2/2), the YAML check is down to 32 old problems (the sample project's line is updated), the sample's scripts build,
  and the API docs pass `--check`.
- Fresh clone (SC-002, a7353fb2): `git clone` into `bin/fresh-clone`, submodules from GitHub,
  `Win-GenProjects.py` with default options (vs2022, no optional settings), then a Debug build of `Lux.sln`:
  0 errors; `Editor.exe` and `Lux-Runtime.exe` built, and no FMOD file is in the output. The first build
  failed only because a fresh clone has never restored the C# projects' NuGet packages; MSBuild
  `/restore` fixes that (it is the same without this change).

## Phase 5 (US3) — Remove FMOD data

T047–T050 are done; T051 (the editor run on a pre-removal copy) waits for an editor session.

- `AudioLegacyCleanup` works on the files' text, not by loading and re-saving scenes: it removes only
  the FMOD lines, so a scene keeps its entity order and every other line (re-saving reordered
  entities before). It checks each cleaned file still parses, and refuses a file where the FMOD key
  opens a list item.
- Besides what T049 lists, it removes dialogue `AudioKey`s and GUID-keyed accessibility entries.
  `EditorAssetManager` now skips AudioProject/AudioBank registry entries on load.
- The info line for Studio settings reads "FMOD Studio settings are ignored; use Project Settings >
  Audio > Remove FMOD data".
- The test (`TestLegacyCleanup`) covers a scene with Guid-only and mixed references and a decoy inside
  a string, a project with Studio settings and profiles, a registry, a dialogue table, CRLF files, a
  refused file, and Scan + Apply on a folder (a second scan finds nothing; a stale scan is refused).
- **T051 / SC-006 (user-run, 2026-11-03):** on the pre-removal sample (0a3dfa00^, extracted to
  `bin/legacy-sample`), Scan listed the FMOD references, Remove FMOD Data cleared them, and a second scan
  found none; "it all works". Afterwards no scene, project or registry file in the copy holds an FMOD key.
- Run on the current sample as well, it removed the empty FMOD keys US1 had left in `AudioDemo`,
  `AudioTest` and `Benchmark` and one real reference (`AudioTest`'s FMOD-only source, `event:/Fart`,
  which never had a Sound). `GenerateBenchmarkScenes.py` now writes references the way the engine does
  (only `Sound`, or `{}`), so regenerating Benchmark keeps them clean. YAML check: 31 old problems.

## Phase 6 (US4) — remaining parity gaps

T052–T055 are done; T056 (driving them in the editor) waits for an editor session.

- **T052:** the Sound Editor's Music section draws the first clip's waveform (the clip the engine counts
  beats on) under bar lines (numbered when there is room), beat ticks (left out under 4 px a beat) and
  the markers. Dragging a marker moves it; the editor's existing edit tracking makes a drag one undo step.
- **T053:** dropping an audio file on a translation's Clip already worked (`PropertyAssetReference` takes
  Content Browser drops of its type). Added a Play/Stop button per translation: the line's Sound in 2D,
  with that language's clip, outside the asset's instance limit and cooldown, like the Sound Editor's
  audition.
- **T054:** `AudioEngine::ApplySoundEdit`, called when the Sound Editor saves. Live instances take the new
  copy and keep parameter values by name; levels, pitch, curves, attenuation and parameters apply in place,
  a bus change re-routes each voice, and a change to the tree, clips, start offsets, loops, streaming or 3D
  on/off restarts the instance. `TestLiveSoundEdits` checks each case by rendering. Mixer **Apply** does
  not keep voices playing: it restarts the engine, and is only enabled outside Play, where the voices are
  previews; bus volume and mute already apply live. Left as is.
- **T055:** already present. The Audio Debugger's **Validate Project Audio** runs
  `AudioValidation::ValidateProject`, the same check export runs, and lists the issues. No change.
- None of the new UI has been clicked through yet; its ImGui scopes were audited by reading.
