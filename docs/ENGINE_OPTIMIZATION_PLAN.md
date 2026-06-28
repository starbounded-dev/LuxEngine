# LuxEngine Optimization Plan — Doing It The Way the AAA Engines Do

This plan adapts proven techniques from Hazel, Unreal, Unity, RAGE (Rockstar), and
Decima (Guerrilla — *Death Stranding* / *Horizon*) to **LuxEngine's actual state**.
It is not a generic checklist: every item is mapped to where this engine is today and
what it already has.

The single most important idea, repeated by every one of those teams: **optimization is
a measurement discipline, not a bag of tricks.** You profile, find the real bottleneck,
fix that one thing, measure again. Most "optimizations" applied without a capture make
things slower or just move the cost. We already started this (Tracy + `csvexport` +
`docs/RENDERER_PERF_BASELINE.md`) — that foundation is Phase 0 and everything depends on it.

---

## 0. Where LuxEngine actually stands

**Already has (genuinely modern):**
- Custom **render graph** with transient-resource aliasing (Frostbite/Unreal-RDG lineage).
- **GPU-driven rendering**: indirect draws, a mesh-culling compute pass, HZB occlusion culling.
- **Tiled light culling** (16px tiles, 256 lights/tile) — i.e. Forward+ / tiled deferred.
- **Bindless** material textures (GPUScene texture table).
- Deferred **and** forward paths; GTAO, SSR, TAA, bloom, DOF, auto-exposure, sky-atmosphere.
- **Nubis-style volumetric clouds** (this is literally Guerrilla/Decima's technique — Andrew
  Schneider's SIGGRAPH work).
- A **JobSystem** and a dedicated **render thread**.

**Available on the GPU but completely UNUSED (only in device setup, never in the renderer):**
- **Mesh shaders** (`VK_NV_mesh_shader`)
- **Ray tracing** (`VK_KHR_ray_query`, `ray_tracing_pipeline`, `acceleration_structure`)
- **Variable Rate Shading** (`VK_KHR_fragment_shading_rate`)
- **Async compute** (a compute queue exists but rendering is effectively single-queue)

**Known problems that are "negative performance" (fix before adding features):**
- Async asset-upload **race** → null GPU buffers (caused the Sponza crash; partially guarded).
- **Depth-layout validation desync** (PreDepth depth ping-pong) — already half-patched.
- `SceneRenderer.cpp` is a **7,900-line god-object** — not slow per se, but it makes every
  optimization risky and unreviewable.
- No **GPU-timeline** profiling yet (CPU Tracy is wired; GPU-Tracy attempt was reverted).

The headline: LuxEngine is **architecturally ahead of where most hobby engines stop**, but it
under-uses the hardware it already enabled, and a few correctness bugs are sapping it.

---

## Phase 0 — Measurement infrastructure (DONE)

> *"You can't optimize what you can't see."* — every engine team, every GDC talk.

It turned out the engine already had nearly everything; the only real work left was to stop
ignoring it. **GPU timing was already in the engine** — the TracyVk attempt was an unnecessary
detour (it crashed because its calibration `vkQueueSubmit` fights nvrhi's queue ownership, and
was reverted).

- [x] **Tracy CPU zones** across the renderer + Vulkan (this session).
- [x] **Repeatable benchmark protocol + baseline sheet** (`RENDERER_PERF_BASELINE.md`).
- [x] **GPU timing — already present, now documented:**
  - **In-engine GPU profiler** (`RendererDebuggerPanel`): per-pass GPU ms + history graphs,
    `Profiled GPU Pass Sum`, `Scene GPU Command Buffer`, **CPU/GPU delta** (which you're bound
    by), and main/render-thread **work-vs-wait** split. This is the primary GPU source of truth.
  - **Named GPU markers** (`beginMarker` → `vkCmdBeginDebugUtilsLabel`): every pass shows by
    name in **RenderDoc / Nsight Graphics** captures with zero extra work — use these for deep
    per-draw GPU dives.
  - **Per-pass timestamp queries** feed both. (`RenderCommandBuffer::GetTimerQueryTime`.)
- Division of labor (the AAA convention): **Tracy = CPU**, **Renderer Debugger panel = quick
  GPU per-pass**, **RenderDoc/Nsight = deep GPU captures**. Do NOT rebuild GPU timing in Tracy.

See `RENDERER_PERF_BASELINE.md` → "GPU profiling workflow" for the step-by-step.

**Reference:** Tracy manual; RenderDoc / Nsight Graphics docs; Unreal *"GPU Visualizer / stat
GPU"* docs (same in-engine-panel philosophy).

---

## Phase A — CPU-side: draw submission & threading

This is where LuxEngine's measured cost actually is right now (light scenes are CPU-bound;
`FlushDrawList` was ~3 ms before this session's cuts).

### A1. Persistent draw-command caching (Unreal's biggest CPU win)
Unreal's **`FMeshDrawCommand`** pipeline caches the per-draw state and only rebuilds when a
primitive actually changes. LuxEngine rebuilds draw lists every frame. There's already a
`MeshDrawCommandCache` scaffold in `SceneRenderer` — finish it: hash (mesh, material,
pass-state) and only re-record on change. **Win:** removes most of the remaining
`FlushDrawList` CPU.
**Reference:** Unreal *"Mesh Drawing Pipeline"* docs; The Cherno's Hazel render-pass videos.

### A2. Job-ify the frame, don't just have a render thread
LuxEngine has a render thread + JobSystem but the frame is largely serial
(`BuildRenderPacket` → `SubmitMeshes` → `FlushDrawList`). The AAA model is **fibers/jobs**:
parallel visibility, parallel command-list recording (one secondary command buffer per
worker), then a serial submit.
**Reference:** Christian Gyrling, *"Parallelizing the Naughty Dog Engine Using Fibers"*
(GDC 2015) — the canonical talk. Unity's **DOTS/Burst/Jobs** is the same idea productized.

### A3. Data-oriented the hot loops
The draw-submission path copies `Ref<>` (atomic refcount bumps) and `std::vector`s per frame.
We already cut some; the rest wants **SoA layout + handles instead of smart pointers** in the
per-frame path (Unreal's primitive scene proxies, Unity's Entities Graphics).
**Reference:** Mike Acton, *"Data-Oriented Design and C++"* (CppCon 2014).

### A4. Parallel command buffer recording
NVRHI supports multiple command lists. Record passes that don't depend on each other on
worker threads. This is standard in every console engine.

---

## Phase B — Use the hardware you already turned on

These are the techniques that separate LuxEngine from the named engines. **All three
extensions are already enabled — the cost is purely implementation.**

### B1. Async compute (highest ROI, lowest risk of the three)
RAGE (RDR2) and Decima lean *heavily* on async compute: run GTAO / SSR / light-culling /
cloud raymarch / bloom on the **compute queue** overlapping the graphics queue's shadow &
g-buffer work. LuxEngine has a compute queue but runs serially. Move the independent compute
passes to async.
**Reference:** *"FrameGraph: Extensible Rendering Architecture in Frostbite"* (O'Donnell, GDC
2017) — covers async-compute scheduling on a graph exactly like LuxEngine's; RDR2 / Decima
SIGGRAPH course notes on async compute.

### B2. Variable Rate Shading (cheap, big GPU win on the heavy passes)
`VK_KHR_fragment_shading_rate` is enabled and unused. Apply VRS to volumetric clouds, fog,
SSR, and bloom — the low-frequency full-screen passes — for a large GPU saving at almost no
visual cost. This is shipping in most current console titles.
**Reference:** NVIDIA VRS docs; *"VRS in <current-gen titles>"* GDC talks.

### B3. Mesh shaders (medium effort, replaces the cull→indirect path)
`VK_NV_mesh_shader` is enabled and unused. The modern GPU-driven path (Alan Wake 2, UE5
Nanite's fallback, many PS5 titles) is **meshlet + mesh-shader culling** instead of
compute-cull + indirect-draw. LuxEngine already has the meshlet-friendly GPU-driven scaffolding
— this is the natural evolution, but do it *after* async compute and only with GPU timing in place.
**Reference:** NVIDIA *"Introduction to Mesh Shaders"*; *"Nanite: A Deep Dive"* (Brian Karis,
SIGGRAPH 2021) for the visibility-buffer + software-raster context.

### B4. (Aspirational) Ray tracing for RTAO / reflections / GI
RT is fully enabled (`ray_query` + acceleration structures). LuxEngine's `ReflectionOcclusionMethod`
enum already anticipates it. RTAO/RT-reflections are the cleanest first step (replace SSR's
screen-space artifacts). This is a large effort — schedule it last.
**Reference:** Decima/Unreal Lumen talks; *Ray Tracing Gems* I & II.

---

## Phase C — GPU pass-level optimization (needs Phase 0 GPU timing first)

Only meaningful once you can *see* GPU pass cost. Then, capture a heavy scene (Sponza+) in
Nsight/RenderDoc and attack the top 3 passes. Likely candidates given the feature set:
shadows, GTAO, volumetric clouds, SSR. Standard moves: half-res + bilateral upsample (already
partly there), better empty-space skipping for clouds (the `WIP: fix empty-space skipping`
commit), reduce overdraw via the depth prepass (already present), trim redundant barriers.

### C-bonus: Clustered shading (evolution of the current tiled cull)
LuxEngine is **tiled** (2D). The modern step (Decima, Doom, many PS5 engines) is **clustered**
(3D froxels) — better for many lights at varying depth and unifies opaque+transparent+volumetric
lighting. Natural upgrade to the existing `LightCullingPass`.
**Reference:** Ola Olsson et al., *"Clustered Deferred and Forward Shading"* (2012);
Tiago Sousa / id, *"The Devil is in the Details: idTech 666"* (SIGGRAPH 2016).

---

## Phase D — Memory & streaming

The Sponza crash was fundamentally a **streaming/lifetime bug**, not a perf bug — but AAA
engines treat streaming as a first-class perf system (RAGE's RDR2 streaming, Decima's tiled
streaming).
- Fix the **asset-upload race** properly: a frame-fenced upload queue / staging ring so a mesh
  is never bound before its buffer is resident (we only added a null-skip guard).
- Budget-driven texture/mesh streaming with mip-bias under pressure (the `DistanceMipBias`
  knobs already exist — wire them to a residency budget).
- **PSO caching** to disk (you already cache shader permutations) to kill hitching — the #1
  perceived "performance" complaint in shipped games is pipeline-compile stutter.
**Reference:** *"The Rendering of Rise of the Tomb Raider"*; UE *"PSO Caching"* docs.

---

## Phase E — Structural & correctness (do continuously, not last)

- **Split `SceneRenderer.cpp`** (7,900 lines) into per-domain files. Net-zero perf, but it's a
  prerequisite for safely doing A–D. (Unreal/Unity keep renderer subsystems small and separable.)
- **Strip debug/profiling from Dist** builds (the stats/snapshot machinery currently runs in
  shipping).
- Treat **every Vulkan validation error as a bug** (the depth-layout desync) — `AGENTS.md`
  already says this. Validation errors mean undefined behavior that *will* bite on another driver.

---

## Priority order (what to actually do, in sequence)

1. **Phase 0 GPU timing** (RenderDoc/Nsight now; Tracy-Vk later) — unblocks everything GPU.
2. **A1 draw-command caching** + **A2/A4 parallel recording** — biggest *measured* CPU win.
3. **B1 async compute** — biggest GPU win for least risk; the graph already models dependencies.
4. **E structural split + Dist stripping** — makes the rest safe and ships lean.
5. **B2 VRS** — cheap GPU win on the heavy full-screen passes.
6. **D streaming/PSO** — kills hitching (perceived performance).
7. **B3 mesh shaders**, **C clustered shading** — larger architectural upgrades.
8. **B4 ray tracing** — last, largest, most optional.

## What NOT to do (the discipline part)
- Don't chase Nanite/Lumen clones — they're multi-year efforts and overkill here.
- Don't micro-optimize a function you haven't seen in a capture.
- Don't add async compute / mesh shaders before GPU timing exists — you can't validate the win.
- Don't optimize the 2-light sample scene; profile a representative heavy scene (Sponza+).

## Curated learning sources (the "internet/YouTube" the request asked for)
- **The Cherno / Hazel** YouTube — direct lineage of this engine; render graph, GPU-driven series.
- **GDC Vault**: Naughty Dog fibers (Gyrling 2015), Frostbite FrameGraph (O'Donnell 2017),
  Nanite deep dive (Karis 2021), Decima/Guerrilla rendering & Nubis clouds (Schneider).
- **Ubisoft**, *"GPU-Driven Rendering Pipelines"* (Haar & Aaltonen, SIGGRAPH 2015) — the
  blueprint for the indirect/cull path LuxEngine already started.
- **Mike Acton**, *"Data-Oriented Design and C++"* (CppCon 2014) — CPU mindset.
- **Unreal Engine source** (GitHub) — RDG, MeshDrawCommands, async compute scheduling.
- **Unity** SRP/BatchRendererGroup + DOTS docs — batching & job-based rendering.
