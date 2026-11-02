# Completion report: Soundwave completion (plan Phases 4–7)

Written 2026-11-02. All 25 tasks in `tasks.md` are checked. The work landed on
`features/soundwave-acoustic-kernel` (last commit `ecc2dbc5`) and was then merged into
`feature/native-audio-system` (`990ec80a`), which also replaces Asterwave and carries the native
miniaudio backend that replaces FMOD. Evidence is listed per success criterion, with where it was
measured.

## Success criteria

| Criterion | Status | Evidence |
|---|---|---|
| **SC-001** Clean checkout with no Vercidium builds, runs Play and exports on both platforms | ✅ Met | CI green on `ecc2dbc5` and on the merged `70d08111` (Windows Debug/Release/Dist, Linux Debug/Debug-AS/Release/Dist, native Windows/Linux). A planted `vaudionative.dll` is not exported (exporter guard). Play and export pass in the editor on the merged branch too (see SC-003). |
| **SC-002** Every earlier project format loads and plays | ✅ Met | YAML with `Source: Engine\|Raytraced` loads with the field ignored (occlusion settings tests). The sample project's FMODDemo ran in Play with 0 validation errors on the Soundwave branch. |
| **SC-003** GPU and CPU differ by ≤ 0.5 dB | ✅ Met | `run_soundwave_gpu.py` on an RTX 4070 Ti: 0.0086 dB worst over 5 scenes, re-run on the merged branch. In editor Play on the merged branch (Debug): 269 traces each, decay gap 0.00009 dB, 0 validation messages; all 5 Sponza cases (corner, doorway open/closed, direct blocked/clear) pass in Play and in the packaged runtime with GPU reverb. |
| **SC-004** No main-thread wait; within the audio budget | ✅ Met | Solves run on JobSystem workers and the GPU readback is polled (`Threading.md`). FMODDemo Play (Debug, merged branch): Soundwave's main-thread update 0.046 ms against the 2 ms budget, audio CPU 0.75%; one 3.65 ms update at Play start, while pipelines were still being created, logged the budget warning once. |
| **SC-005** 60 m courtyard source stays audible with air absorption | ✅ Met | `SoundwaveReverbTests.cpp` courtyard case: air is charged only on excess path length, never the direct path. |
| **SC-006** Stone room and corridor differ in send by > 0.1 | ✅ Met | `SoundwaveReverbTests.cpp`: corridor send < stone send − 0.1 and a shorter decay. |

## User stories

- **US1 No Vercidium:** removed from build, link, deploy, CI, tests and docs. On the merged branch
  Asterwave (the Vercidium-derived replacement) is removed too.
- **US2 Old projects keep working:** legacy `Source` field is accepted and dropped on save.
- **US3 GPU solve:** listener reverb on the GPU (`AudioAcousticKernel`), per-source solves on
  workers, CPU cross-check under `LUX_DEBUG`.
- **US4 Air absorption, bounces, spread:** authored in Project Settings, observable in the Audio
  Debugger (Paths column, room readout).
- **US5 Path overlay:** drawn by `EditorLayer::DrawAudioOcclusion` behind "Show Occlusion Paths";
  checked in editor Play on the Soundwave branch; its code is byte-identical on the merged branch.

## Not verified

- A Release-build budget measurement (the numbers above are from a Debug build).
