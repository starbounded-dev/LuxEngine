# Renderer Performance Baseline (Phase 0)

This is the measurement harness for the renderer-performance work. **Every later
optimization (Phase 1+) must be validated against numbers captured here** — no
"looks faster," only before/after deltas on the same scene.

## What instrumentation exists now

Phase 0 wired the renderer into two complementary measurement systems:

1. **Tracy (CPU timeline).** Tracy was already compiled into all non-Dist builds
   (`TRACY_ENABLE` + `TRACY_ON_DEMAND`, `LUX_ENABLE_PROFILING = !LUX_DIST`), but
   `SceneRenderer` emitted **zero** zones — the whole renderer was one opaque block.
   Now:
   - Every render pass is a named Tracy zone. All 39 passes funnel through
     `SceneRenderer::ScopedCPUProfile`, so a single instrumentation point in its
     ctor/dtor (`Core/Source/Lux/Renderer/SceneRenderer.cpp`) covers them all.
   - `BeginScene`, `EndScene`, `FlushDrawList` are named zones.
   - `RenderGraph::BuildAndCompile` is its own zone — this is the **Phase 1 target**
     (the graph is rebuilt + recompiled every frame regardless of whether topology
     changed). Watch this zone's cost specifically.

2. **In-engine stats (GPU timeline + counts).** `SceneRenderer::Statistics`
   (per-pass GPU timestamp queries, draw calls, instance/cull counts) is surfaced by
   the editor's **Render Stats** panel and **Renderer Debugger** panel. GPU timing
   comes from real GPU timestamp queries, not CPU-side guesses.

> Tracy gives CPU-thread detail; GPU timing comes from the in-engine per-pass
> timestamp queries (NOT Tracy-Vulkan). A `TracyVkZone` integration was attempted and
> **reverted** — its calibration `vkQueueSubmit` fights nvrhi's queue ownership and it
> crashed at startup. It is unnecessary: the engine already attributes GPU cost per pass
> (below), and RenderDoc/Nsight cover deep GPU dives.

## GPU profiling workflow (Phase 0 — this is your GPU "source of truth")

There are three GPU tools, in increasing depth. Use the lightest one that answers the question.

**1. Renderer Debugger panel (in-engine, always available) — start here.**
Open it in the Editor. It reads real per-pass GPU timestamp queries
(`RenderCommandBuffer::GetTimerQueryTime`) and shows:
- Per-pass **GPU ms** (and CPU ms) in a table, with rolling history graphs.
- `Scene GPU Command Buffer` (total GPU), `Profiled GPU Pass Sum`, and **`CPU/GPU Delta`** —
  the fastest way to tell whether a frame is CPU- or GPU-bound.
- Main/Render-thread **Work vs Wait** — tells you if the render thread is starved or saturated.
This is the equivalent of Unreal's `stat GPU` / GPU Visualizer. For routine "which pass got
slower" questions, this is all you need.

**2. RenderDoc / Nsight Graphics (deep GPU capture) — for per-draw / barrier / occupancy.**
Every pass is already emitted as a **named GPU debug marker**
(`beginMarker` → `vkCmdBeginDebugUtilsLabelEXT`), so a capture shows the labelled pass tree
(`ShadowMapPass`, `GBufferPass`, `GTAO`, `VolumetricClouds`, …) with **zero extra code**.
Use this to inspect overdraw, barriers/transitions, pipeline state, and (Nsight) GPU occupancy
when the panel says a pass is hot but not why.
- **RenderDoc**: cross-vendor, best for frame debugging + correctness (also surfaces the
  same Vulkan validation issues we've been chasing).
- **Nsight Graphics** (NVIDIA, you're on a 4070 Ti): best for GPU occupancy / stall analysis.

**3. Tracy = CPU only.** Don't rebuild GPU timing in Tracy. CPU zones + the GPU panel +
RenderDoc/Nsight is the same split Unreal/Unity/AAA teams use.

> **Rule before Phase C GPU work:** capture a *heavy* scene (Sponza+), read the top 3 GPU
> passes from the panel, then open RenderDoc/Nsight on those specific passes. Never optimize
> a GPU pass you haven't seen in a capture.

## Build to profile with

Use a **non-Dist** configuration (Tracy is stripped from Dist):

- **Release** — the representative perf build. Profile here for real numbers.
- **Debug** — fine for sanity/relative checks, but absolute numbers are not
  representative (no optimizer).

```
MSBuild Lux.sln /m /p:Configuration=Release /p:Platform="Mixed Platforms"
```

Run the **Editor** from `bin/Release-windows-x86_64/Editor/`.

> **Known caveat (pre-existing, unrelated to Phase 0):** the generated
> `Core/Core.vcxproj` references `Source\Lux\Terrain\TerrainHeight.{h,cpp}`, which do
> not exist on disk — a stale project reference from the in-progress terrain work. A
> full `Core` build fails with `C1083: Cannot open source file 'TerrainHeight.cpp'`
> until either those files are added or projects are regenerated
> (`scripts/Win-GenProjects.py`). This is not caused by the profiling changes;
> `SceneRenderer.cpp` itself compiles clean.

## Benchmark protocol (repeatable)

The goal is a **deterministic, comparable** measurement. Until an automated
camera-path/headless benchmark exists, use this fixed manual protocol:

1. **Fixed scene.** Pick one scene and freeze it (e.g. `LuxSampleProject`'s main
   scene, or a dedicated `PerfBench` scene). Record: mesh count, light count,
   which heavy features are on (shadows, GTAO, SSR, clouds, bloom).
2. **Fixed viewport.** Same window size every run (e.g. 1920×1080). Resolution
   scale = Native. Record it.
3. **Fixed camera.** Park the camera at one documented vantage point that frames a
   representative amount of the scene. Don't move it during sampling. (Screenshot it
   so the next run matches.)
4. **Warm up.** Let it run ~3–5 s after load so shader/pipeline compilation, asset
   streaming, and dynamic-resolution settle before sampling.
5. **Sample.** Read the Render Stats panel; for CPU detail, capture a Tracy trace
   (connect Tracy profiler, capture ~300 frames, save the `.tracy`).
6. **Repeat 3×** and take the median — single frames are noisy.

## Baseline record sheet

Fill this in once, on the build/scene/camera above. This is the bar Phase 1 must beat.

| Field | Value |
|---|---|
| Date / commit | 2026-06-27, dev (Phase 0 instrumentation) |
| Config | Release |
| Scene / mesh count / light count | LuxSampleProject sample scene; 2 lights (skylight + zone light) |
| Viewport / resolution scale | (as captured) |
| Trace | `traceProfiler2026-06-27-2-50.tracy` (~996 frames) |
| Extraction tool | `.tracytools/csvexport/Release/tracy-csvexport.exe` |

Mean per-frame CPU times (from `tracy-csvexport`, 996 frames):

| Metric | Source | Baseline (mean/frame) | After Phase 1 |
|---|---|---|---|
| `SceneRenderer::FlushDrawList` / `EndScene` | Tracy zone | **3.09 ms** | **2.56 ms (−17.4%)** |
| ↳ render-graph cost | Tracy zone | `BuildAndCompile` **0.853 ms** | `Build` 0.408 + `Compile` ~0 = **0.408 ms (−52%)** |
| ↳ `RenderGraph::Compile` invocations | Tracy count | **996 / 996 frames** | **1 / 3403 frames** |
| `SceneRenderer::BeginScene` | Tracy zone | 0.115 ms | 0.089 ms |

**Phase 1 result (after-trace `traceProfiler2026-06-28-12-45.tracy`, 3403 frames):**
- `RenderGraph::Compile` ran **exactly once** for the whole capture instead of every
  frame — the structure-hash cache holds on a static scene. Render-graph cost/frame
  dropped 0.853 → 0.408 ms (the residual is `Build`, now the larger half — a future
  target). FlushDrawList overall −0.537 ms/frame (−17.4%); the extra ~0.09 ms beyond
  the render-graph saving is the scratch-buffer reuse.

**Phase 1.5 result (after-trace `traceProfiler2026-06-28-13-43.tracy`, 1705 frames):**
- Lazy resource/pass names (built only on the non-executable debug-snapshot path) cut
  `RenderGraph::Build` 0.408 → 0.306 ms.
- Removed a hidden *second* `BuildRenderGraph()` per frame in `UpdateRenderGraphStatistics`
  (it now reuses the executable graph already built that frame).
- `FlushDrawList` 2.556 → 2.112 ms (−17.3%). **Cumulative Phase 1 + 1.5: 3.093 → 2.112 ms,
  −31.7%.**

> GPU per-pass execution time is **not** in this trace (no TracyVk yet); the listed
> pass zones (`GBufferPass` 0.059 ms, `PreDepthPass` 0.071 ms, etc.) are CPU-side
> submission cost. The scene is light (2 lights), so the bottleneck is **CPU frame
> submission**, not GPU — exactly what Phase 1 targets. For GPU timing use the
> in-engine Renderer Debugger panel.

### How to re-capture (every Phase 1 before/after)

```
.tracytools/csvexport/Release/tracy-csvexport.exe <trace.tracy> > zones.csv
```
Compare `SceneRenderer::FlushDrawList` and `RenderGraph::BuildAndCompile` mean_ns
against this baseline.

### Per-pass table (paste from Renderer Debugger / Tracy)

| Pass | CPU ms | GPU ms |
|---|---|---|
| ShadowMapPass | | |
| PreDepthPass | | |
| GBufferPass / ForwardGeometryPass | | |
| LightCullingPass | | |
| GTAO (compute + denoise) | | |
| SSR | | |
| VolumetricCloud* | | |
| BloomCompute | | |
| CompositePass | | |
| ... | | |

## Phase 1 hypotheses to confirm against this baseline

These are the predicted wins; the baseline exists to prove or disprove them:

- **Render-graph recompile skip** — `RenderGraph::BuildAndCompile` runs every frame
  (`SceneRenderer.cpp`, in `FlushDrawList`). Expectation: a large, near-constant CPU
  cost that drops to ~0 on frames where pass topology is unchanged.
- **Per-frame copies** — `FlushDrawList` copies `MaterialScene::GetMaterials()` and
  `TextureScene::GetTextureHandles()` by value despite const-ref getters; plus ~6
  local `std::vector`s reallocated each frame.
- **Bindless table re-bind** — a `MaxGPUTextureSceneTextures`-wide loop calls
  string-keyed `SetInput` across 5 passes every frame.

Re-capture with the identical protocol after each Phase 1 change and fill the
"After Phase 1" column.
