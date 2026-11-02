# Tasks: Soundwave Completion (Acoustic Kernel Phases 4–7)

**Input**: `specs/001-soundwave-completion/` (plan, spec, research, data-model, contracts, quickstart)

Tests are included: the spec's acceptance scenarios and constitution IV require a runnable check
per stage. Story map: US1 no Vercidium, US2 old files load, US3 GPU solve, US4 air/bounces/spread,
US5 path overlay.

## Phase 1: Setup

- [x] T001 Record the user's 2026-11-01 answers (air absorption = darken energy only; asterwave branch superseded) in docs/AUDIO_ACOUSTICS_PLAN.md § 1 and § 10.4

## Phase 2: Foundational

- [x] T002 Map every consumer of `RaytracedAudioScene`, `m_LastAudioAmbience`, `AudioGeometrySystem` VA calls and `AudioOcclusionSource` (grep Core/ Editor/ tests/) and decide per site: delete, or rewire to `AudioAcoustics`

## Phase 3: US2 — Old projects keep working (P1)

**Independent test**: a settings YAML with `Source: Raytraced` loads, plays on Soundwave, re-saves without `Source`.

- [x] T003 [US2] Remove `AudioOcclusionSource` and `Source` from Core/Source/Lux/Audio/AudioOcclusionSettings.h; reader in AudioOcclusionSettings.cpp accepts `Source: Engine|Raytraced` and ignores it ("accepted and ignored on load, never written"), writer omits it
- [x] T004 [US2] Remove `m_OcclusionSource` and its branches from Core/Source/Lux/Scene/Scene.h and Scene.cpp, and the legacy-selection checks from Editor/Source/Panels/ProjectSettingsWindow.cpp self-test
- [x] T005 [US2] Update tests/audio/OcclusionTests.cpp so the legacy `Source` key loads, is ignored and is not re-written

## Phase 4: US1 — No Vercidium anywhere (P1)

**Independent test**: with Core/vendor/VA_RAY moved aside, `scripts\Lux-BuildTest.ps1` passes Debug/Release/Dist and the export library list has no VA file.

- [x] T006 [US1] Delete Core/Source/Lux/Audio/RaytracedAudioScene.h/.cpp and remove every use in Core/Source/Lux/Scene/Scene.h, Scene.cpp, Core/Source/Lux/Core/Application.cpp
- [x] T007 [US1] Remove VA-only code from Core/Source/Lux/Audio/AcousticMaterial.h/.cpp and Core/Source/Lux/Audio/AudioGeometrySystem.h/.cpp (keep what Soundwave or portals consume)
- [x] T008 [US1] Remove the VA sections from Editor/Source/Panels/AudioDebugPanel.h/.cpp and Editor/Source/EditorLayer.cpp; show Soundwave reverb (decay, send, escaped) and kernel diagnostics instead
- [x] T009 [US1] Remove VA rows, `ValidateAudioSDK`, `LUX_ENABLE_RAYTRACED_AUDIO` and copy lines from Dependencies.lua, premake5.lua, Core/premake5.lua, Editor/premake5.lua, Lux-Runtime/premake5.lua
- [x] T010 [P] [US1] Remove VA libraries from Editor/Source/RuntimeExportUtils.cpp deploy lists
- [x] T011 [P] [US1] Remove VA handling from scripts/Configure.py, scripts/ci/StageAudioSDKs.py, .github/workflows/main.yml, .gitignore
- [x] T012 [US1] Drop VA objects and libraries from tests/audio/run.py, tests/audio/run_occlusion_windows.py, tests/audio/run_sdk_layout.py, tests/audio/GeometryTests.cpp
- [x] T013 [US1] Regenerate projects and run scripts\Lux-BuildTest.ps1 (Debug, Release, Dist) with Core/vendor/VA_RAY moved aside

## Phase 5: US4 — Air absorption, bounces, spread (P3, before GPU so the twin ports final maths)

**Independent test**: `run_soundwave.py` shows a 60 m courtyard source audible and distinct results for bounces 1–4.

- [x] T014 [US4] Add `AirAbsorptionLF`/`AirAbsorptionHF` ("float dB/100 m", range "0–20", defaults "0.1" / "2.5") to Core/vendor/soundwave/include/soundwave/Settings.h; apply to indirect extra length in Core/vendor/soundwave/src/Propagation.cpp and ray length in Reverb.cpp
- [x] T015 [US4] Split reflected energy by surface `Scattering` in Core/vendor/soundwave/src/Propagation.cpp (specular share on the mirror path) and add per-bounce path counts to `SolveDiagnostics` in Solve.h
- [x] T016 [US4] Add the two rows to Core/Source/Lux/Audio/AudioOcclusionSettings.h/.cpp (YAML keys per contracts/audio-settings.md) and Editor/Source/Panels/ProjectSettingsWindow.cpp with self-test edits
- [x] T017 [US4] Library tests: courtyard 60 m and bounce counts in tests/audio/SoundwavePropagationTests.cpp; air absorption in tests/audio/SoundwaveReverbTests.cpp

## Phase 6: US3 — GPU solve (P2)

**Independent test**: headless parity run prints max |ΔdB| ≤ 0.5 over the library scenes.

- [x] T018 [US3] Flatten a `GeometryView` to world-space node/triangle arrays in Core/vendor/soundwave/src/Flatten.cpp (+ include/soundwave/Flatten.h), with a library test
- [x] T019 [US3] GLSL twin of the reverb: Editor/Resources/Shaders/Include/GLSL/Soundwave.glslh (traversal) and SoundwaveReverb.glsl (one invocation per ray). Direct walk and propagation stay CPU, on workers (user decision 2026-11-02, see research R3b)
- [x] T020 [US3] `AudioAcousticKernel` in Core/Source/Lux/Audio/AudioAcousticKernel.h/.cpp: pipeline + buffers built once, upload per geometry generation, dispatch via Renderer::Submit, ring readback (ClusterCounterReadback pattern), stall count
- [x] T021 [US3] AudioAcoustics: GPU reverb through the ReverbBackend hook (CPU job fallback) and per-source Solve on a JobSystem worker; Core/Source/Lux/Audio/AudioAcoustics.cpp
- [x] T022 [US3] Parity test CPU vs GPU ≤ 0.5 dB on a real Vulkan device: tests/audio/run_soundwave_gpu.py + SoundwaveGpuParity.cpp

## Phase 7: US5 — Path overlay (P3)

- [x] T023 [US5] Draw the selected source's direct/reflection/opening paths with Renderer2D::DrawLine in Editor/Source/EditorLayer.cpp behind a viewport toggle; nothing drawn when off

## Phase 8: Polish

- [x] T024 Docs: .claude/docs/Architecture-LuxEngine.md § 2.10 rewrite, Threading.md, Rendering.md, Building.md, CLAUDE.md, AGENTS.md, .specify/memory/constitution.md (PATCH), README.md, NOTICE, docs/AUDIO_*.md, docs/Editor/{Audio,Panels}.md, Editor/LuxSampleProject/FMOD_SETUP.md, tests/audio/README.md
- [x] T025 Mark plan status in docs/AUDIO_ACOUSTICS_PLAN.md; `/cr`; commit and push per stage

## Dependencies

T001–T002 → US2 (T003–T005) → US1 (T006–T013) → US4 (T014–T017) → US3 (T018–T022) → US5 (T023) → Polish.
US2 and US1 touch the same Scene code, so they run in sequence. US4 lands before US3 so the GPU
twin ports the final maths once. US5 depends only on CPU results and could run any time after US1.

## Parallel opportunities

T010 and T011 (different files, no dependency on each other). T024's doc files can be split.

## Implementation strategy

MVP = US2 + US1 (Vercidium gone, old files load). Then US4, US3, US5, each committed and pushed on
its own after `/cr`.
