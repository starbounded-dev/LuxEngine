# Research: Soundwave Completion

All items below were checked against the tree at `03588551` on 2026-11-01.

## R1 — Is the GPU port worth it? (measure first, constitution IV)

- **Measured:** scratch benchmark (Release, MSVC `/O2`), 16,104-triangle floor + pillars + wall
  run, default `Settings`: `Solve` **2.11 ms per source** (work 2,789), `EvaluateReverb`
  **4.07 ms** (852 traces).
- **Decision:** keep Phase 4. At the default 32-source budget and 20 Hz, a 60 fps frame solves
  ~11 sources on main: ~23 ms. That is the main-thread cost the GPU port removes.
- **Alternatives:** solve on JobSystem workers (cheapest fix for main-thread cost; rejected as the
  *shipped* path because the user chose GPU compute, Decision 8 — kept as the fallback note).

## R2 — How does a compute dispatch reach the GPU and come back?

- **Decision:** one `PipelineCompute` + `ComputePass` built once; dispatch recorded through
  `Renderer::Submit` from the audio window on main; results copied into a `CPUAccess::Read`
  `NRIBuffer` per frame-in-flight and read when that slot comes round — exactly the
  `ClusterCounterReadback` pattern (`SceneRenderer.cpp:9108-9145`, `SceneRenderer.h:525-531`).
- **Rationale:** the pattern already exists, needs no wait (`RT_BeginFrame` waited for the slot),
  and works under both threading policies.
- **Latency:** FramesInFlight frames (2–3), below one 20 Hz acoustic update at 60 fps; the plan's
  "one update of latency" is met.
- **Queue:** the engine's compute passes record on the graphics command buffer unless
  `EnableAsyncCompute`; devices without a compute family already fall back (`b9b57287`). The kernel
  uses the graphics path — no new queue logic.
- **Stall (risk 9.5):** the readback slot only advances when frames render; the kernel counts
  updates since the last completed readback and reports "stalled" in diagnostics.

## R3 — GPU kernel shape

- **Decision:** a faithful GLSL twin of the CPU code, one invocation per source, scratch in
  per-source SSBO ranges. Geometry is flattened on a JobSystem worker from the published
  `GeometryView` (virtual nodes/triangles already enumerable: `GetNode`, `TriangleAtOrder`,
  `GetTriangle`) into world-space node/triangle arrays and uploaded once per geometry generation.
- **Rationale:** the 0.5 dB gate is easiest to hold when the GPU executes the same candidate order
  as the CPU; flattening keeps traversal to one BVH walk.
- **Shortcut:** flattening instanced meshes costs memory (≈ 48 B/triangle); upgrade to a two-level
  GPU traversal when a scene's flattened size exceeds the budget.
- **Alternatives:** GPU ray service fed by a CPU search (rejected: the search is sequential, every
  step would wait a round trip); ray-tracing pipelines (Decision 8: documented future path).

## R4 — Vercidium removal surface (Phase 7)

Tracked files referencing it (excluding `docs/vercidium-repro/`, `specs/`):
build — `Dependencies.lua`, `premake5.lua`, `Core/premake5.lua`, `Editor/premake5.lua`,
`Lux-Runtime/premake5.lua`, `scripts/Configure.py`, `scripts/ci/StageAudioSDKs.py`,
`.github/workflows/main.yml`, `.gitignore`; code — `RaytracedAudioScene.h/.cpp`,
`AcousticMaterial.cpp`, `AudioGeometrySystem.h/.cpp`, `Application.cpp`, `Scene.h/.cpp`,
`EditorLayer.cpp`, `AudioDebugPanel.h/.cpp`, `RuntimeExportUtils.cpp`; tests — `GeometryTests.cpp`,
`run.py`, `run_occlusion_windows.py`, `run_sdk_layout.py`, `README.md`; docs — `CLAUDE.md`,
`AGENTS.md`, `README.md`, `NOTICE`, `.claude/docs/*`, `.specify/memory/constitution.md`,
`docs/AUDIO_*`, `docs/Editor/{Audio,Panels}.md`, `Editor/LuxSampleProject/FMOD_SETUP.md`.
Historical records (`docs/Lux-Commit-History.json`, `docs/Lux-History-Review.md`,
`docs/LuckyEngine-Comparison.md`, `docs/lucky-research/`) stay untouched: point-in-time.

## R5 — Air absorption (Phase 5)

- **Decision (user, 2026-11-01):** absorption darkens occlusion/reverb energy along the path, mostly
  HF; FMOD's distance fade stays authoritative for loudness. Model: ISO 9613-1-style dB/m per band,
  authored as two project rows `AirAbsorptionLF`/`AirAbsorptionHF` (dB per 100 m; defaults
  0.1 / 1.0), applied to indirect paths' extra length over the direct path and to reverb ray
  length. The direct path's own distance is never charged — that is FMOD's fade.
- **Rationale:** charging only the *extra* length is the smallest model that cannot double-fade.

## R6 — Caustic spread (Phase 5)

- **Decision:** reflected energy is split by the surface's existing `Scattering` value: the
  specular share follows the mirror ray; the scattered share is spread over the reverb estimate.
  No new optical model (spec assumption).

## R7 — Path overlay (Phase 6)

- **Decision:** draw with the editor's existing debug line renderer (`Renderer2D` lines in the
  viewport overlay, `Rendering.md § Editor debug geometry`), so no new `(set, binding)` and nothing
  drawn when off.
