---
name: profile
description: LuxEngine performance investigation. Measures before changing anything — rules out the present/VSync ceiling, decides CPU- vs GPU-bound from the in-editor Profiler and Renderer Debugger, captures Tracy (CPU + GPU zones) or RenderDoc/Nsight when needed, and reports before/after numbers on a fixed protocol. Use when the user reports low FPS, hitches, slow loads, "this pass is expensive", or asks to optimize or benchmark something.
---

# profile — LuxEngine performance investigation

The expensive mistakes in performance work here are all measurement mistakes: optimizing a frame
rate that was pinned by presentation, comparing a focused run against an unfocused one, reading a
Debug build, or "fixing" a pass nobody measured. This skill makes the measurement come first and
stay honest.

`/profile` with no argument walks the user through a triage. `/profile <symptom or target>` runs the
procedure against that.

**Read first:** `.claude/docs/Rendering.md` — § *Performance rules*, § *GPU timing has two
consumers*, and § *Present mode, and why frame rate is not a render-cost question*. That doc is the
authority. `docs/RENDERER_PERF_BASELINE.md` is a point-in-time record: its method is still useful,
but it predates the Tracy GPU context and says GPU zones were reverted. They were not — trust
`Rendering.md`.

---

## Non-negotiables

1. **No number, no change.** Every optimization is justified by a measurement taken before it and
   verified by the same measurement after it. "Looks faster" is not a result.
2. **Release, never Debug, never Dist.** Debug absolute numbers are meaningless (no optimizer, and
   `enableDebugRuntime` turns on the Khronos validation layer, which taxes every Vulkan call —
   `Core/Source/Lux/Core/Window.cpp`). Dist compiles Tracy out (`LUX_ENABLE_PROFILING` is
   `!LUX_DIST && TRACY_ENABLE`, `Core/Source/Lux/Debug/Profiler.h`). A build generated with
   `--no-tracy` has no zones at all.
3. **Same conditions on both sides.** Same config, scene, camera, viewport size, resolution scale,
   quality settings, threading policy, and **window focus** (focus alone changes the frame rate 2x
   under `MAILBOX` — `Rendering.md`). If any differ, the comparison is void; say so.
4. **Median of at least three runs**, after a warm-up.
5. **Never trade image quality for speed silently.** In particular never propose enabling TAA,
   SMAA T2x, or any temporal accumulation as an optimization — temporal techniques are a hard no in
   this engine. Quality trade-offs are the user's call; present them as options.
6. **Settings leak into the project file.** The editor saves every renderer quality option into
   `Editor/LuxSampleProject/LuxSample.luxproj`. Toggling features to measure them will dirty that
   file. Restore it (`git diff` it) before finishing, and never commit measurement-time settings.

---

## Step 1 — Classify the symptom

| Symptom | Go to |
|---|---|
| Low or capped frame rate | Step 2, then 3 |
| Periodic hitch / single long frame | Step 3 (Tracy — the in-editor graphs average it away) |
| Slow startup or scene load | Step 4, CPU. A cold shader cache makes startup slow by itself — check `Editor/Resources/Cache/Shader/` exists before blaming anything |
| "Pass X is expensive" | Step 3 → GPU path |
| Memory growth / VRAM pressure | Renderer Debugger → **Memory** tab, then Step 4 |
| Slow only during Play | Step 3 with Play running; scripts (`ScriptUpdate`) and physics (`PhysicsStepTime`) are separate timers |

State which one it is before measuring.

---

## Step 2 — Rule out the presentation ceiling

A frame rate pinned to the refresh rate, or to a clean multiple of it, is **presentation**, not
render cost. Lowering quality will not move it — that is the diagnostic.

Check, in order (all in **Application Settings**):

- **VSync.** On means the display paces the loop; Frame Rate Limit and Present Mode are disabled.
- **Frame Rate Limit** — frame pacing (`Application::SetTargetFrameRate`) can only slow the loop
  down.
- **Present Mode** (VSync off only): "Mailbox (no tearing)" or "Immediate (uncapped, may tear)".
  `MAILBOX` is *not* uncapped on a composited desktop; only `IMMEDIATE` exceeds the refresh rate in
  a window.
- **Focus.** A script-launched editor window is unfocused and runs a different regime.

If the frame rate sits on the ceiling, stop and report that. There is nothing to optimize.

---

## Step 3 — CPU- or GPU-bound?

Use the lightest tool that answers the question.

### 3a. In-editor (always available)

Both panels are closed by default; open them from the **View** menu (switching the editor to the
Advanced layout also opens them).

- **Profiler** (`Editor/Source/Panels/ProfilerPanel.cpp`) — rolling CPU/GPU frame graph against a
  budget line, a CPU breakdown from `Application::PerformanceTimers` plus the named
  `LUX_SCOPE_PERF` zones, and the per-pass GPU breakdown.
- **Renderer Debugger** → **Overview** / **Profiling** tabs
  (`Editor/Source/Panels/RendererDebuggerPanel.cpp`) — per-pass CPU and GPU ms
  (`SceneRenderer::Statistics::PassProfiles`), `CPU/GPU Delta`, and Main/Render thread Work vs Wait.

Reading the thread timers (these are only meaningful under `ThreadingPolicy::MultiThreaded`; under
`Single` there is no render thread to wait on):

| Timer | Where it is measured | High means |
|---|---|---|
| `MainThreadWorkTime` | `Application::Run`, the main loop body | Game/editor-side CPU: scene update, scripts, physics, ImGui build, render-command recording |
| `MainThreadWaitTime` | `Application::Run`, around `BlockUntilRenderComplete()` | Main is idle waiting for the render thread to finish the previous frame |
| `RenderThreadWorkTime` | `Renderer::WaitAndRender`, executing the command queue | Render-thread CPU plus anything it blocks on (GPU submission, present) |
| `RenderThreadWaitTime` | `Renderer::WaitAndRender`, waiting for the kick | Render thread starved — main is the bottleneck |

**`RenderThreadGPUWaitTime` is declared and displayed ("Render Thread (GPU wait)") but never
written — it always reads 0.** Do not cite it as evidence.

Rule of thumb: render thread waiting → main-thread CPU bound. Main thread waiting and render work
high → look at total scene GPU time (`SceneRenderer::Statistics::TotalGPUTime`, from
`RenderCommandBuffer::GetExecutionGPUTime`) versus the frame time to split render-thread CPU from
GPU.

`UpdateMemoryStatistics` deliberately does not run every frame (it walks every VMA allocation), so
memory numbers lag.

### 3b. Tracy (CPU timeline + GPU zones)

Tracy is built with `TRACY_ENABLE`, `TRACY_ON_DEMAND`, `TRACY_CALLSTACK=10` (`premake5.lua`).

- **The profiler must match the client version.** The client is the `Core/vendor/tracy/tracy`
  submodule; read `public/common/TracyVersion.hpp` (currently **0.13.1**) and use the matching
  `tracy-profiler` / `tracy-capture` release. A mismatched server refuses the connection.
- `TRACY_ON_DEMAND` means **nothing is recorded until a profiler connects.** Launch the editor,
  warm up, *then* connect.
- **GPU zones:** every `SceneRenderer::BeginProfiledGPU` → `Renderer::BeginGPUPerfMarker` →
  `RenderCommandBuffer::RT_BeginTimerQuery` emits both the engine timer query and a Tracy Vulkan
  zone. A capture with **zero GPU zones** means the `TracyVkCtx` failed to create — look for
  `Tracy GPU profiler context ... GPU zones will be unavailable` in the log. It does not mean the
  GPU is idle.
- Frames are delimited by `LUX_PROFILE_MARK_FRAME` in `Application::Run`. The render thread is named
  `"Render Thread"`.
- Nearly every engine function carries `LUX_PROFILE_FUNCTION_AUTO`, so a connected capture has
  real instrumentation overhead. Compare captures with captures, never a captured run with an
  uncaptured one.

Headless capture and extraction (Windows, from the Tracy release folder):

```powershell
.\tracy-capture.exe -o before.tracy -s 20        # connect, record 20 s, write the file
.\tracy-csvexport.exe before.tracy > before.csv   # per-zone stats; -e for self time, -f to filter
```

Use `-e` (self time) when a parent zone is hot only because of a child. For hitches, open the trace
in `tracy-profiler` and find the long frame — aggregates hide spikes.

### 3c. RenderDoc / Nsight Graphics (why a pass is slow)

Every profiled pass is a named debug marker, so a capture shows the labelled pass tree with no extra
code. Use it for overdraw, pipeline state, barriers, and (Nsight) GPU occupancy — once the panels
have told you *which* pass. Launch with the working directory set to the `Editor/` source folder
(see Step 5). A previous barrier audit found no free redundant transitions; don't re-open that line
without new evidence.

---

## Step 4 — Localize, then hypothesize

1. Name the top three costs from the measurement, with numbers.
2. For the top one, find the code and state a *specific* hypothesis — what work is redundant, what
   scales wrong, what runs when its feature is off.
3. Check it against the known performance anti-patterns before inventing a new theory
   (`Rendering.md § Performance rules`): per-frame pipeline/shader/descriptor-layout creation,
   per-frame buffer/image recreation, `std::string`/`std::format` per draw, unbounded per-frame
   vectors, a sync point (`WaitIdle`, fence wait) added inside the loop, passes running while
   disabled, a `ComputeStructureHash()` miss forcing a `RenderGraph` recompile every frame.
4. If the hypothesis needs a finer zone, add one — `LUX_PROFILE_SCOPE("Name")` for Tracy,
   `LUX_SCOPE_PERF("Name")` to also show in the in-editor Profiler.
   **`LUX_SCOPE_PERF` / `LUX_SCOPE_TIMER` declare a variable literally named `timer__LINE__`**
   (`Core/Source/Lux/Core/Timer.h` — the macro does not paste `__LINE__`), so two in the same scope
   fail to compile with a redefinition. Use a nested block. Remove temporary zones afterwards unless
   they match the surrounding instrumentation density.

---

## Step 5 — The benchmark protocol

Use this whenever a number goes into a report or justifies a change.

1. **Build** Release. Verify by the artifact's timestamp, not the exit code
   (`.claude/docs/Building.md`).
2. **Launch** `bin\Release-windows-x86_64\Editor\Editor.exe` with the working directory set to the
   `Editor\` source folder (or pass `-C <repo>\Editor`). Running from the `bin` folder finds no
   `Resources/` and every shader loads with `SourceSize: 0`. On Linux, `scripts/Linux-Run.sh`
   already does this — and note Linux defaults to the single-threaded policy, so its numbers are not
   comparable to a Windows multi-threaded run.
3. **Scene.** Use a fixed scene. For renderer scaling work, generate the stress scenes:
   `python scripts/GenerateBenchmarkScenes.py` writes `Benchmark_10k_Cubes`,
   `Benchmark_100k_Cubes`, `Benchmark_CityBlocks`, `Benchmark_IndoorOccluders`, and
   `Benchmark_ManyLights` into `Editor/LuxSampleProject/Assets/Scenes/Benchmarks/`.
4. **Fix** window size, resolution scale, camera vantage point (screenshot it), quality settings,
   threading policy, present mode. Keep the window **focused**.
5. **Warm up** 3–5 s so shader/pipeline compilation and dynamic resolution settle.
6. **Sample** three runs; report the median.
7. **Restore** `LuxSample.luxproj` if you touched settings.

---

## Step 6 — Report

Lead with the answer, then the evidence:

- **Bottleneck:** CPU (which thread) or GPU (which pass), with the numbers.
- **Conditions:** config, commit, scene, viewport, focus, present mode, threading policy.
- **Before / after** table with medians, if a change was made. State any condition that differed.
- **What was not measured** and any claim that is inference rather than measurement — say so
  plainly.

If the work produced a change, it still goes through `/cr` before committing. Do not commit from
`/profile`.
