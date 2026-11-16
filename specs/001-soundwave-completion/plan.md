# Implementation Plan: Soundwave Completion (Acoustic Kernel Phases 4–7)

**Branch**: `features/soundwave-acoustic-kernel` | **Date**: 2026-11-01 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/001-soundwave-completion/spec.md`

## Summary

Finish `docs/AUDIO_ACOUSTICS_PLAN.md`. Order follows the spec's priorities, not the old phase
numbers: **A** remove Vercidium and the legacy selector (spec P1, plan Phases 6 + 7) — it is
independent of the GPU and retires a licence question; **B** air absorption, deeper bounces and
scattering spread (plan Phase 5); **C** the GPU twin of the CPU kernel (plan Phase 4), measured
first (research R1: 2.1 ms/source on main today); **D** path overlay and docs (plan Phase 6 rest).
Each stage builds, tests and commits on its own.

## Technical Context

**Language/Version**: C++20 (MSVC 14.51 / clang), GLSL 4.6 compute via shaderc

**Primary Dependencies**: Soundwave (in-tree, Apache-2.0), FMOD Studio (kept), NRI/Vulkan, JobSystem

**Storage**: project `.luxproj` YAML audio block; runtime binary audio block (embedded YAML)

**Testing**: `tests/audio/run_soundwave.py`, `run_occlusion_windows.py`, `scripts/Lux-BuildTest.ps1`,
Linux CI `tests/audio/run.py`; new GPU/CPU parity test in the headless harness

**Target Platform**: Windows (render thread on) and Linux (single-threaded)

**Project Type**: engine library + editor + runtime

**Performance Goals**: no main-thread acoustic wait; GPU solve inside the audio budget of
`docs/AUDIO_PERFORMANCE_VALIDATION.md`

**Constraints**: ≤ 0.5 dB GPU/CPU parity; no new mandatory install; old files load

**Scale/Scope**: ~45 tracked files touched for removal; ~700 lines of kernel C++ mirrored in GLSL

## Constitution Check

| Principle | Status |
|---|---|
| I. Self-contained | **Pass, improves**: removes a third-party SDK; nothing added. Constitution text naming Vercidium as "already required" is amended (PATCH) in stage A |
| II. Conventions/helpers | Pass: reuses `ClusterCounterReadback` readback, `Renderer2D::DrawLine`, `ImGuiEx::Property`, settings YAML pattern |
| III. Threading/renderer | Pass with care: dispatch via `Renderer::Submit`; pipeline built once; `(set, binding)` grepped before use; frees through `SubmitResourceFree`; no temporal technique |
| IV. Verifiable phases | Pass: stages A–D each build + test + commit; editor launch only with user OK |
| V. Docs true | Pass: Architecture/Threading/Rendering/Building updated per stage |

No violations; Complexity Tracking empty.

## Stages

### A — Soundwave owns Play; Vercidium leaves the tree (P1)

- Delete `RaytracedAudioScene.h/.cpp`, its Scene members, the VA branches in `Scene.cpp`,
  `Application.cpp` pool shutdown, `AudioDebugPanel`'s VA sections, `AcousticMaterial` VA
  functions; `AudioGeometrySystem`'s VA mirror if nothing else consumes it.
- `AudioOcclusionSource` removed; YAML reader still accepts and ignores `Source`; runtime blocks
  unchanged in shape (embedded YAML).
- Build: `Dependencies.lua` VA rows + `ValidateAudioSDK`, `premake5.lua` call, editor/runtime copy
  lines, `RuntimeExportUtils` library lists, `Configure.py`, `StageAudioSDKs.py`, CI workflow,
  `.gitignore` line, `LUX_ENABLE_RAYTRACED_AUDIO`.
- Tests/runners: drop VA objects/libs from `run.py`, `run_occlusion_windows.py`, `run_sdk_layout.py`,
  `GeometryTests.cpp`.
- Verify: `Lux-BuildTest.ps1` Debug/Release/Dist with `Core/vendor/VA_RAY` renamed away; all audio
  runners; old-format settings round-trip test.

### B — Air absorption, bounces, scattering (plan Phase 5)

- Soundwave `Settings`: `AirAbsorptionLF/HF` (dB/100 m); applied to indirect extra length and
  reverb ray length (R5). Scattering split on reflections (R6). Bounces already authored (1–16);
  add per-bounce counts to diagnostics.
- Editor rows + YAML keys + self-test edits; library tests: courtyard at 60 m, bounce counts.

### C — GPU twin (plan Phase 4)

- Flatten published geometry on a worker; upload once per generation.
- GLSL twin of `Trace`, `EvaluateDirect`, `Solve` propagation, `EvaluateReverb`, one invocation per
  source; fixed reduction order.
- `AudioAcousticKernel` seam in `Core/Source/Lux/Audio/`: pipeline, buffers, ring readback (R2).
- Parity test: CPU vs GPU on the library test scenes, ≤ 0.5 dB.
- Stall diagnostics (R2).

### D — Path overlay, docs (plan Phase 6 rest)

- Viewport overlay drawing the selected source's `SolveResult` paths via `Renderer2D::DrawLine`.
- Docs: Architecture § 2.10 rewrite, Threading/Rendering rows, `docs/Editor/Audio.md`, README,
  `docs/AUDIO_*` setup lines, plan doc status.

## Project Structure

### Documentation (this feature)

```text
specs/001-soundwave-completion/
├── plan.md  research.md  data-model.md  quickstart.md
├── contracts/audio-settings.md
└── tasks.md (from /speckit-tasks)
```

### Source Code (repository root)

```text
Core/vendor/soundwave/{include/soundwave,src,shaders}/   # library + GLSL twin
Core/Source/Lux/Audio/                                     # AudioAcoustics seam, settings, validation
Core/Source/Lux/Scene/                                     # Scene wiring
Editor/Source/{EditorLayer.cpp,Panels/}                    # settings rows, overlay, debug panel
tests/audio/                                               # runners + tests
```

**Structure Decision**: existing engine layout; no new projects.

## Complexity Tracking

None.
