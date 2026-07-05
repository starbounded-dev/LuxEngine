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

## Status ledger

| Phase | Status | Result |
|---|---|---|
| **Phase 0** — measurement infra | ✅ Done | Tracy CPU zones on all 39 passes; in-engine GPU per-pass timing documented; baseline protocol in `RENDERER_PERF_BASELINE.md`. |
| **Phase 1** — render-graph recompile cache + scratch reuse | ✅ Done | `RenderGraph::Compile` 996/996 frames → 1/3403; `FlushDrawList` 3.09 → 2.56 ms. |
| **Phase 1.5** — lazy graph names, removed double `BuildRenderGraph` | ✅ Done | `FlushDrawList` 2.56 → 2.11 ms (**cumulative −31.7%**). |
| **Phase 2** — submission-path allocations + build config | ✅ Implemented, awaiting Windows measurement | Six fixes below. |
| **Phase 3** — full rendering audit: stability + zero-cost-when-off | ✅ Implemented, awaiting Windows measurement | Seven fixes below (A8 deferred). |

**Phase 2 fixes (2026-07-02, one commit each):**

1. **Dist build config** (`Core/premake5.lua`, `premake5.lua`) — Core's Dist filter no
   longer overrides `optimize "Full"` down to `"On"`; `LinkTimeOptimization` (/GL+/LTCG)
   added workspace-wide for Dist. Release untouched (Tracy baseline build).
   *Verify:* regenerate projects, check vcxproj for /GL, FPS spot-check (no Tracy in Dist).
2. **Push-constant scratch** (`SceneRenderer.cpp` `RT_DrawStaticMesh`) — the per-draw
   per-pass `std::vector<uint8_t>` heap allocation on the render thread is now a reused
   render-thread-only member. *Verify:* render-thread `RenderCommandQueue::Execute` zone.
3. **`MeshDrawParams` capture** (`SceneRenderer.cpp`, 8 submit sites) — draw lambdas no
   longer copy `TransformMapData` (with its `ObjectIndices` heap vector) per draw; they
   capture a 4-scalar POD snapshot. *Verify:* per-pass CPU zones (ShadowMapPass,
   PreDepthPass, GBufferPass, TransparentForwardPass).
4. **Upload-lambda init-captures** (`SceneRenderer.cpp` FlushDrawList) — removed the
   local-copy-then-capture-copy of 8 vectors/frame; GPUScene instance vectors are moved.
   *Verify:* `FlushDrawList` zone.
5. **`SyncRenderScene` scratch** (`Scene.cpp`) — sync-item vector is thread_local scratch,
   cleared at both ends. *Verify:* `Scene::SyncRenderScene` zone.
6. **Version-gated table copies** (`TextureScene`, `MaterialScene`, `SceneRenderer`) — the
   full texture/material table copies are skipped when the submitted scene's monotonic
   version is unchanged. The texture *resolve loop* intentionally still runs every frame
   (it picks up async texture-load completions, which don't mark the scene dirty).
   *Verify:* `FlushDrawList` zone; add/remove a material + texture at runtime and confirm
   the change appears.

**Phase 3 fixes (2026-07-03, one commit each) — from the full rendering audit:**

1. **Descriptor re-Bake bug** (`Platform/Vulkan/DescriptorSetManager.cpp`) —
   `InvalidatedInputResources` was never cleared in the live path, so the first
   invalidation (guaranteed by the startup resize) made every dynamic pass rebuild ALL
   its binding sets × frames-in-flight, every frame, forever. Now cleared at the top of
   `InvalidateAndUpdate`. **Prime frame-time-instability suspect.** *Verify:* the
   `DescriptorSetManager::InvalidateAndUpdate ... updating N descriptors` trace log stops
   repeating after warm-up/resize; frame-time graph flattens.
2. **GPUScene debug snapshot on-request** (`SceneRenderer` + `RendererDebuggerPanel`) —
   the O(instances+materials) validation loops now run only on frames where the panel's
   GPU Scene section requests them. *Verify:* `FlushDrawList` CPU with the panel closed.
3. **PreIntegration gated behind SSR** — its visibility pyramid is consumed only by SSR.
4. **Cluster froxel passes skip with zero local lights** — build early-outs; culling
   zero-fills the grids instead of dispatching.
5. **Atmosphere UBO idles when sky/clouds/fog are all off** — disabled-flags UB written
   once per frame-in-flight buffer, then no rebuild/upload until a feature activates.
6. **Shadow default 4K→2K** (options + project defaults + High preset; Ultra=4K,
   Cinematic=8K) — saves ~200 MB VRAM and shadow-render bandwidth.
7. **GPU timer/pipeline queries disabled in Dist** (`RenderCommandBuffer.cpp`).

*(A8 — JumpFlood RGBA32F→smaller format — was investigated and deferred: the algorithm
uses all four channels (xy=seed offset, z=distance, w=inside/outside), so only a
precision-reduction to RGBA16F is possible and that needs visual verification.)*

**Post-Phase-3 bugfix — stale wrapped-framebuffer self-heal (2026-07-03):** framebuffers
wrapping shared images via `ExistingImages` bake the image's nvrhi handle and were never
re-checked — the runtime-fullscreen "black geometry, bright sky" bug (deferred lighting
writing into an orphaned SceneColor). `Framebuffer::HasStaleAttachments()` + a per-frame
repair sweep in `BeginScene` now self-heals the whole class. *Verify:* fullscreen export
lights correctly; the `had stale attachment handles` warning names the trigger framebuffer
on the first fullscreen frames (report it for the targeted root-cause follow-up) and stays
silent afterwards.

**Phase 4 progress (2026-07-03, Loading & Stutter session):**

1. **Batched resource uploads** (`Renderer::RecordResourceUpload/FlushResourceUploads`;
   converted: `VertexBuffer`, `IndexBuffer`, `Image2D::SetData`, `TextureCube`) — one
   shared command list replaces a vkQueueSubmit per mesh/texture; the batch flushes
   automatically before every `RenderCommandBuffer::RT_Submit`, so uploads always land
   ahead of any consumer. Also stops retaining a command list per buffer forever.
   *Verify:* load a heavy scene / stream assets while watching the frame-time graph —
   load-time spikes should shrink dramatically; visuals identical.
2. **Bloom + SSR pre-convolution pyramids RGBA32F→RGBA16F** (+ shader storage layouts) —
   half the bandwidth on every down/upsample. *Verify:* bloom/SSR before-after eyeball.
3. **PSO disk cache — investigated, blocked:** graphics/compute pipelines are created
   inside NVRHI (`PipelineCompute` etc. hold nvrhi handles); the vendored `nvrhi` submodule
   wasn't checked out in this environment, so whether the fork exposes a
   `VkPipelineCache` hook couldn't be verified. Next session with the submodule present:
   check `nvrhi::vulkan::DeviceDesc` for a pipeline-cache field; if absent, patch the
   fork to create/serialize one (`~/.lux/pipeline.cache`-style). The legacy
   `VulkanComputePipeline.cpp` per-pipeline `vkCreatePipelineCache` is dead code (live
   compute goes through nvrhi) — remove during the SceneRenderer split.
4. **Upload-race note:** with batched uploads the content-vs-consumer ordering is now
   structural. The remaining race is only "buffer object not yet created" on async loads,
   which the existing null-guards handle (mesh appears a frame later). A per-mesh ready
   flag is the polish item if the one-frame pop-in ever bothers.

**Phase 4 progress — memory diet (2026-07-04):**

1. **Editor render targets skipped in runtime (~180 MB)** —
   `SceneRendererSpecification::EnableEditorRenderTargets` (runtime sets false) gates
   SelectedGeometry, JumpFlood ×3, AO-Debug, GBufferDebug, wireframe target creation; all
   references null-guard. *Verify:* runtime VRAM drop; editor selection/debug views
   unchanged.
2. **Mesh CPU memory** — removed `m_TriangleCache` entirely (3 full Vertex structs per
   triangle, built by importer + runtime deserializer, **zero consumers**); the runtime
   additionally compacts every MeshSource after GPU upload to positions + indices
   (physics cooking is the only CPU consumer and reads exactly that). Editor retains full
   data for export. *Verify:* runtime RAM drop; mesh colliders (incl. spawned at runtime)
   identical; editor mesh import/export identical.
3. **Truthful memory HUD** — `VulkanAllocator::GetStats` now reports device-local
   usage/budget from `VK_EXT_memory_budget` (driver-truth incl. NVRHI's allocations)
   instead of a dead VMA-side tracker that showed ~0. *Verify:* HUD ≈ GPU-Z dedicated
   VRAM numbers.
4. **Aliasing coverage of big targets — investigated, REJECTED:** the exclusions in
   `IsRenderGraphAliasCandidate` are correctness, not oversight. GTAO/SSR/Cloud history
   buffers persist across frames (temporal accumulation reads last frame's result) and
   can never be transient; PreDepth/GBuffer/SceneColor are read throughout the frame
   (SSR, GTAO, debug views, TAA), leaving no dead window to alias into. Do not extend
   aliasing to these.

**Phase 4 progress — CPU frame cost (2026-07-04):**

1. **Sort-order cache (A1-lite)** — per-pass draw sorting skipped when the draw-list key
   set fingerprint is unchanged (MeshKey embeds all sort inputs). *Verify:*
   `FlushDrawList` CPU on a static-membership scene; visuals identical while
   adding/removing/selecting meshes. Full FMeshDrawCommand retention remains future work
   (camera-driven CPU culling changes list membership every frame on moving cameras).
2. **Dirty-range GPUScene uploads (A2-prime)** — per-sync dirty ranges replayed once per
   frame-in-flight buffer; full uploads only on scene switch/count growth or when dirty
   volume exceeds a full array. *Verify:* upload closure cost in Tracy; GPUScene debug
   snapshot diagnostics stay clean while moving objects.
3. **Pending-slot bindless resolve** — steady state resolves only streaming-pending slots
   + transients, with a 32-frame full-sweep hot-reload safety net. *Verify:* texture
   streaming still flips white→real; editor texture hot-reload updates within ~32 frames.
4. **Granular descriptor rebake** — `BakeSet` rebuilds only the changed set indexes
   instead of every binding set on any invalidation.
5. **Small always-on trims** — spot-shadow machinery skips with zero spot lights;
   directional shadow UBO idles when cascades are unchanged (memcmp + per-FIF counter);
   the statistics draw-list re-walk compiles out of Dist.
6. **Parallel command recording — NOT attempted here:** the render command queue is
   single-producer and NVRHI multi-command-list recording changes the threading model;
   needs a build+validation cycle. Revisit with the async-compute (B1) work.

**Phase 4 progress — GPU frame cost & bandwidth (2026-07-04):**

1. **Surgical UAV flag** — STORAGE usage now granted only to Storage-usage images and
   mip-chained sampled textures (the compute mip generator); all framebuffer attachments
   lose it so framebuffer/DCC compression can re-engage. **The item to measure first**
   (RenderDoc: GBuffer/SceneColor no longer report STORAGE; GPU frame time on heavy scenes).
2. **AO composite folded into deferred lighting** — the full-res Zero_SrcColor multiply
   pass is gone; deferred samples u_GTAOTex itself (same upscale/decode, same
   __HZ_AO_METHOD permutation). GTAO chain now registers between GBuffer and Deferred.
   AO debug view unchanged (keeps the AO-Composite shader standalone).
3. **GBuffer ID merge** — material+object IDs packed into one RG32UI attachment
   (6→5 color targets; velocity slot 4, depth wrap slot 5).
4. **Empty-pass graph gating** — Selected/Transparent/Wireframe nodes skip registration
   when their draw lists are empty (executable graphs only).
5. **Effect defaults** — GTAO denoise baseline 4→2 passes; High preset enables SSR+GTAO
   temporal accumulation.

**Deferred from this batch (design notes):**
- **Octahedral GBuffer normals (RG16F)** — opted-in but deliberately held for its own
  session: the normal attachment is read *raw* (`.xyz`) by GTAO.hlsl (HLSL!), SSR.glsl,
  SSR-Composite.glsl, AO-Composite.glsl (debug), DeferredLighting's fold helpers, and
  written raw by the forward/transparent shader — every one needs the encode/decode pair
  landed together, which deserves a fresh, focused diff rather than the tail of this one.
  Encode/decode belong in LuxGBuffer.glslh; writers: GBuffer_Static (via EncodeGBuffer)
  + the forward PBR shader; readers listed above.
- **B1 async compute (design)**: move GTAO+denoise, cluster light-cull, bloom, and the
  cloud raymarch to nvrhi's compute queue, overlapping ShadowMap/PreDepth/GBuffer on
  graphics. The render graph already carries the dependency edges — the work is (a) verify
  the vendored nvrhi fork's multi-queue API (`CommandQueue::Compute` command lists +
  queue semaphores / `executeCommandLists` overloads; submodule wasn't checked out here),
  (b) split RT_Submit's single-queue mutex model per queue, (c) insert cross-queue waits
  at the graph edges (GTAO→Deferred, cull→lighting, bloom→composite). Validate with the
  Renderer Debugger per-pass GPU times: shadow+GBuffer time should absorb the compute.
- **B2 VRS (design)**: raster passes only (cloud/fog/atmosphere composites — the compute
  passes can't use VRS); needs the nvrhi fork's variable-rate-shading state API verified.
  2x2 rate on the volumetric composites is the standard cheap win.
- **#8 mesh shaders / ray tracing** — roadmap-final, unchanged.

**Phase 4 candidates (audit findings that need build/measure or shader edits — do with
Tracy + validation on):**

- **Blanket `isUAV = true` on every color image** (`Image.cpp:190-199`) adds STORAGE usage
  to all render targets, likely disabling framebuffer compression → bandwidth tax on every
  full-res pass. Audit shaders for storage-image bindings first, then restrict to
  `Usage == Storage` + explicit opt-ins. Biggest GPU-bandwidth suspect.
- **Synchronous per-resource GPU uploads** — every mesh/texture load creates its own
  command list and submits immediately under a global queue mutex
  (`VertexBuffer.cpp:23-27` etc.), and the command list is retained per buffer forever.
  Batch into a per-frame upload list / transfer queue. Biggest streaming-hitch suspect.
- Format diets needing shader edits: Bloom + PreConvolution pyramids RGBA32F→RGBA16F;
  JumpFlood RGBA32F→RGBA16F (see A8 note); GBuffer normal → octahedral RG16F; merge the
  two R32UI id targets.
- Cluster grid caching on resize/projection change (currently rebuilt per frame while
  lights exist).
- Empty-pass graph gating (Transparent/Selected/Wireframe still open + clear render
  passes when their draw lists are empty) — interacts with render-target aliasing.
- Composite-chain merging: Skybox→Deferred→AO→SSR→Cloud→Fog→Composite→DOF each do a
  full-res scene-color read-modify-write; several are mergeable.
- Correctness/sync audit (thread handoff, upload races, barrier semantics) — still
  pending; the audit session for it was cut short.

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
primitive actually changes. **Status correction:** the `MeshDrawCommandCache` is *not* a
scaffold — it is implemented and live (`SubmitMeshPassDraw`, with age-based pruning at
`MeshDrawCommandCacheRetireAge = 300`). What remains of A1 is **retaining the draw lists
across frames**: `ClearFrameMeshPasses` wipes every pass's `DrawList`/`DrawOrder` each frame
and `BuildSortedDrawCommandOrder` re-sorts every pass every frame. Gate that on
`RenderSceneSyncStats` dirty counts. Three blockers make this a measure-validated change,
not an inspection-safe one:
1. per-frame camera-dependent CPU frustum culling (`isInstanceVisible`) feeds the lists;
2. transient (debug/collider) submissions are interleaved with cached ones;
3. `m_MeshTransformMap` offset assignment assumes a fresh build.
**Win:** removes most of the remaining `FlushDrawList` CPU.
**Reference:** Unreal *"Mesh Drawing Pipeline"* docs; The Cherno's Hazel render-pass videos.

### A2-prime. Dirty-range GPUScene uploads
After Phase 2, the largest remaining per-frame memcpy is the **full persistent GPUScene
instance re-upload** in the FlushDrawList upload lambda (the code comment there is explicit:
`StorageBufferSet` owns one buffer per frame-in-flight, so per-slot dirty tracking is needed
before partial uploads are correct). Implement per-frame-in-flight dirty ranges in `GPUScene`
(the `TextureSceneDirtyRange`/`MaterialSceneDirtyRange` machinery is the in-repo pattern to
copy), then upload only dirty rows. Runtime-verification only — do with Tracy running.

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

1. ~~**Phase 0 GPU timing**~~ — done (see Status ledger).
2. ~~**Phase 1/1.5 render-graph caching**~~ — done (−31.7% FlushDrawList).
3. ~~**Phase 2 submission-path allocations + build config**~~ — implemented; validate on
   Windows against the baseline before proceeding.
4. **A1 completion (retained draw lists)** — biggest remaining *measured* CPU win; needs
   Tracy before/after (see A1 blockers).
5. **A2-prime dirty-range GPUScene uploads** — kills the largest remaining per-frame memcpy.
6. **B1 async compute** — biggest GPU win for least risk; the graph already models dependencies.
7. **E structural split + Dist stripping** — makes the rest safe and ships lean.
8. **B2 VRS** — cheap GPU win on the heavy full-screen passes.
9. **D streaming/PSO** — kills hitching (perceived performance).
10. **B3 mesh shaders**, **C clustered shading** — larger architectural upgrades.
    (Note: clustered froxel light culling has since landed in the renderer core — validate
    it with GPU timing, then retire the C-bonus item.)
11. **B4 ray tracing** — last, largest, most optional.

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
