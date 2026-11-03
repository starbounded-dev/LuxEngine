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
