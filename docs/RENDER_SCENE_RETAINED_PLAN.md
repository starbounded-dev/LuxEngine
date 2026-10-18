# LuxEngine Retained Render Scene Plan

Static meshes are rebuilt and resubmitted every frame: `Scene::SyncRenderSceneMirror` rebuilds a
`StaticMeshRenderProxy` for every entity, and `SceneRenderer::SubmitRenderScene` re-resolves
materials, re-runs culling and LOD, and rehashes every submesh into freshly cleared maps. On
Benchmark that is most of the main thread. This plan makes the render scene **retained**:
unchanged meshes cost (almost) nothing on the CPU, and per-frame visibility and LOD move to the
GPU culling pass. It is a planning document written 2026-10-18; what is actually built is described
in `.claude/docs/Architecture-LuxEngine.md` § 2.3 / 2.4 and `.claude/docs/Rendering.md`
§ *Feeding the renderer from the scene*.

**Goal card**
- **Goal:** stop rebuilding and resubmitting ~7k unchanged static meshes every frame; let the GPU
  decide per-frame visibility and LOD.
- **Success:** on Benchmark (Release, 1280x720, golden harness), main-thread time spent in render
  scene sync + submit + draw-list flush drops from ≈13 ms (≈88% of a ≈14.7 ms main thread, Phase 0
  confirms) to **under 1.5 ms** for a frame where nothing changed; every golden capture stays
  pixel-identical (max diff ≤ 1); moving, editing, hiding, selecting, adding and removing meshes
  still render correctly that same frame.
- **Non-goals:** animated (skinned) meshes, sprites/2D, GPU cost of the passes, Release memory
  tracking (user chose to keep it), the meshlet/mesh-shader prepass beyond keeping it working.
- **Constraints:** renderer reads scene state only through the frame packet / `RenderScene`;
  correct under `MultiThreaded` and `SingleThreaded`; no temporal techniques; existing
  `.luxscene` / `.luxproj` load unchanged; `Lux-Runtime` and Dist keep working.
- **User's words:** "Also go GPU-driven" (scope choice, 2026-10-18).

**Decisions this plan is built on**

| Decision | Choice | Consequence |
|---|---|---|
| Scope | Retained CPU caches **and** GPU-driven visibility/LOD (user) | Phases 3–4 touch shaders, indirect draws and shadow passes |
| Change detection on the scene side | Cheap in-place compare per entity (no ECS signals exist) | Still an O(entities) scan, but ~tens of ns each; change lists are a follow-up if the scan shows up |
| Material/texture lifetime | Reference counts held by proxies + a `MaterialAsset` revision counter, instead of "touch every frame" | Removes the per-frame `TouchStaticMeshMaterials` / `UpsertMaterial` walk; material edits still propagate |
| Mesh-shader prepass | Keeps the Phase 2 CPU path (CPU LOD) when `EnableMeshShaders` is on | GPU LOD only for the indirect path; meshlet path stays correct, not faster |
| Transient submissions (debug, colliders, override materials) | Stay per-frame, in their own lists | Retained lists never mix with per-frame ones (old plan's blocker 2) |
| Verification of moving objects | **NEW** golden option that moves N entities deterministically per frame | Automated check of the dirty paths, not only manual editor steps |

## Part 0 — Where we are

| Capability | State | Evidence |
|---|---|---|
| Persistent proxies with dirty flags | ✅ Working (Benchmark renders) | `Renderer/RenderScene.cpp` — `UpsertStaticMesh` computes `RenderProxyDirtyFlags` (Mesh/Material/Transform/Visibility/Selection) |
| Proxies still rebuilt every frame | ❌ the cost | `Scene/Scene.cpp` — `SyncRenderSceneMirror` builds a full `StaticMeshRenderProxy` per entity and `insert_or_assign`s it (sampled: `UpsertStaticMesh` 32%, proxy `operator=` 13% of main thread) |
| Material/texture lifetime = touch every frame | ⚠️ works, forces per-frame walks | `RenderScene::TouchStaticMeshMaterials` → `ResolveRenderMaterialID` → `MaterialScene::UpsertMaterial`; `TextureScene::EndSync` frees slots not touched this frame |
| GPUScene persistent, dirty-range uploads | ✅ Working | `GPUScene` + `SceneRenderer` `m_GPUSceneFullUploadsRemaining` / `GetDirtyRanges()` (old plan's A2-prime is done) |
| Draw-command cache | ✅ Working | `SceneRenderer::SubmitMeshPassDraw` + `m_MeshDrawCommandCache`, pruned at `MeshDrawCommandCacheRetireAge = 300` |
| Draw lists / transform buckets rebuilt per frame | ❌ the cost | `SceneRenderer::ClearFrameMeshPasses` clears `m_MeshTransformMap`, `m_ShadowMeshTransformMap`, every `DrawList`; `SubmitStaticMeshInternal` refills them (sampled ≈50% of main thread) |
| Sort-order reuse | ✅ Working | `BuildSortedDrawCommandOrder` fingerprint |
| CPU per-instance visibility + LOD | ✅ Working, per frame | `IsMainViewVisible`, `GetDirectionalShadowCascadeMask`, `IsSpotShadowCasterVisible`, `SelectStaticMeshLOD` (LOD index is part of `MeshKey`) |
| GPU culling (frustum + HZB) | ✅ Working, main view only | `MeshCulling.glsl`: one workgroup per draw, compacts into `VisibleObjectIndexes` + indirect args; gated by `EnableGPUDrivenRendering` |
| Prepass culling | ⚠️ none on GPU | classic `PreDepthPass` draws all `ObjectIndices` non-indirect (it runs before the HZB exists) |
| Shadow lists | ⚠️ CPU per frame | per-cascade/spot `ObjectIndices` in `m_ShadowMeshTransformMap`; `CalculateShadowCasterHashes` + `UpdateShadowCasterMotion` hash every caster's transform each frame |
| Scene change signals | ❌ none | no `entt` observers; `TransformComponent` fields are written directly |
| Measurement | ✅ tools exist | `tests/rendering/golden_run.py` perf.json (per-pass CPU/GPU ms, main-thread ms); sampler.cpp stack sampler. Session drift is large — always A-B-A in one session |

Sampled main thread after the 2026-10-18 fixes (a19ebe3a, 14fd10c6, 137ef833): ≈14.7 ms;
`SubmitRenderScene` 52%, `SyncRenderSceneMirror` 39%, `FlushDrawList`/`EndScene` 7%.

## Part 1 — Goals and non-goals

See the Goal card. "Nothing changed" means the editor camera moves but no entity does; a camera
move must not dirty retained data, because visibility and LOD are per-frame GPU decisions after
Phase 3 (and cheap per-frame CPU decisions before it).

## Part 2 — Design

**Ownership and threads.** Everything here runs on the main thread (`Scene::Render3D` →
`SyncRenderScene` → `SceneRenderer::SubmitRenderScene` / `EndScene`); GPU work is recorded through
`Renderer::Submit` as today. No new threads. The JobSystem fan-out in `SyncRenderSceneMirror` stays.

**Scene side (`Scene`, `RenderScene`).** `SyncRenderSceneMirror` keeps scanning the view, but per
entity it only gathers inputs (world transform, handles, material-table hash, visibility, selection)
and calls a **NEW** `RenderScene::SyncStaticMesh(entityID, inputs)` that compares against the stored
proxy **in place**. Unchanged → stamp `LastTouchedFrame` and return. Changed → update the stored
proxy, bump a **NEW** `StaticMeshRenderProxy::Revision`, and record the entity in a **NEW**
per-sync change list (`created / changed / removed` IDs). Material lifetime becomes reference
counts: a proxy holds one reference per distinct `RenderMaterialID` it uses; `MaterialScene` frees a
row when its count reaches zero, and `TextureScene` likewise per texture slot. Material edits
propagate through a **NEW** `MaterialAsset::GetRevision()` bumped by every setter (they all already
call `UpdateMaterialComplexityMetadata`, so the hook point exists); `MaterialScene::EndSync` rebuilds
only rows whose asset revision moved — a loop over materials (≈128), not submeshes (≈10k).

**Renderer side (`SceneRenderer`).** A **NEW** per-primitive submit cache, keyed by
`(RenderScene*, PrimitiveID)`, holds everything camera-independent for each submesh: material
handle and asset, transparency, shadow casting, sort keys, mesh key handle, and stable indices into
**persistent buckets** (vectors indexed by bucket id, not `unordered_map` lookups). It is rebuilt
only for proxies in the change list or whose material revision moved. Per frame (until Phase 3) the
CPU only runs the camera-dependent tests and appends to the persistent buckets, which are reset by
size rather than cleared and rehashed. Transient submissions keep their own per-frame lists.

**GPU side (Phase 3–4).** Main-view buckets lose their LOD dimension: one cull group per
`(mesh, submesh, material-for-transparent)` holds all its instances, persistent and uploaded only on
change. `MeshCulling.glsl` picks each instance's LOD (same metric as `SelectStaticMeshLOD`) and
appends it to that LOD's indirect draw. An **early cull** dispatch (frustum + LOD, no HZB) feeds an
indirect prepass; the existing late cull (frustum + LOD + HZB) feeds the G-buffer and transparent
passes. Shadows get their own cull dispatch per cascade and spot tile, with indirect shadow draws;
the shadow-cache invalidation hashes are replaced by dirty signals from the change list.

**Engine-fit notes.**
- Renderer reads only `RenderScene` / packet data — unchanged rule (`Rendering.md`).
- New storage buffers need `(set, binding)` slots grepped against the whole shader corpus
  (`Rendering.md` § Shaders); compute pipelines created once in `Init()`; graph `Reads`/`Writes`
  declared, folded into `ComputeStructureHash()`, and gated to zero cost when GPU-driven is off.
- Teardown: the submit cache and persistent buckets are keyed per `RenderScene`; a scene switch
  (new mirror) drops them on the main thread; GPU buffers go through `Renderer::SubmitResourceFree`.
- Two mirrors per scene (editor + runtime) each feed one renderer — the cache key includes the
  `RenderScene` pointer, like `m_ScratchTextureSceneKey` already does.
- Serialization: none (runtime state only). No new files load from disk.
- Runtime/Dist: same code paths; no shader compiler needed beyond existing shader packs (new shader
  variants must be in the pack). Linux: no platform code; verify single-threaded once.
- Docs: `Rendering.md` § Feeding the renderer, § The GPU material table (lifetime), and the GPU
  culling description; `Architecture-LuxEngine.md` § 2.3 / 2.4 retained model.

## Part 3 — Phases

### Phase 0 — Numbers and a moving-object test, before any change

**Goal:** a per-stage CPU breakdown in `perf.json`, and an automated test that moves objects.

**Changes**
- `Renderer/SceneRenderer.h/.cpp` — **NEW** public `RecordExternalCPUProfile(name, ms)` (wraps the
  private `RecordCPUProfile`); `ScopedCPUProfile` zones `"SubmitRenderScene"`, `"FlushDrawList"`.
- `Scene/Scene.cpp` — time `SyncRenderSceneMirror` and record it as `"RenderSceneSync"`.
- `Editor/Source/Tools/GoldenCapture.cpp` — **NEW** option `MoveEntities=N`: before each frame,
  offset the first N `StaticMeshComponent` entities (by UUID order) by a translation that is a pure
  function of the frame number; restore at exit. `tests/rendering/golden_run.py` — **NEW**
  `"moving": "MoveEntities=256"` feature variant.

**Verification:** Release build; 3 runs default + 3 runs `MoveEntities=256` (A-B-A not needed —
no behaviour change); perf.json shows the three new zones; the moving capture is deterministic
(3 runs pixel-identical to each other).
**Exit:** baseline table recorded in this file (Part 4). **Rollback:** revert; zones are inert.

### Phase 1 — Unchanged meshes cost nothing in the sync

**Goal:** `RenderSceneSync` < 1 ms on Benchmark with nothing moving.

**Changes**
- `Renderer/RenderScene.h/.cpp` — **NEW** `SyncStaticMesh(UUID, const StaticMeshSyncInputs&)`
  replacing the per-frame `UpsertStaticMesh(proxy)` copy; in-place compare; `Revision` and change
  list (`GetChangedPrimitives()`); `EndSync` removal pass unchanged in behaviour.
- `Scene/Scene.cpp` — `SyncRenderSceneMirror` passes inputs, not proxies; material-table hash only
  recomputed when the `MaterialTable` Ref or its count changed.
- `Renderer/MaterialScene.h/.cpp`, `Renderer/TextureScene.h/.cpp` — reference-counted rows/slots;
  `EndSync` frees zero-count entries instead of untouched ones.
- `Renderer/MaterialAsset.h/.cpp` — **NEW** `GetRevision()`, bumped in every setter.

**Thread and lifetime:** main thread only; Refs held by proxies as today.
**Verification:** golden default + all feature variants pixel-identical to Phase 0; `moving`
variant identical to Phase 0's moving capture; perf A-B-A; editor: edit a material colour in the
material editor (updates), toggle `Visible`, select/deselect, delete and undo an entity, Play/Stop.
**Exit:** sync target met, all checks pass. **Rollback:** revert the phase commit.
**Docs:** `Rendering.md` (lifetime rule), `Architecture-LuxEngine.md` § 2.4.

### Phase 2 — Cached submit records and persistent buckets (old plan's A1)

**Goal:** `SubmitRenderScene` + `FlushDrawList` < 2.5 ms with CPU culling/LOD still per frame.

**Changes**
- `Renderer/SceneRenderer.h/.cpp` — **NEW** per-primitive submit cache rebuilt from the change list
  and material revisions; persistent bucket vectors replacing the per-frame `m_MeshTransformMap` /
  `m_ShadowMeshTransformMap` / `DrawList` rebuilds for proxy submissions; transient path keeps its
  own per-frame lists; `ClearFrameMeshPasses` resets bucket sizes, not maps.
- Meshlet prepass (`PreDepthMeshletPass`) reads the same buckets (CPU LOD) — unchanged behaviour.

**Verification:** as Phase 1, plus `EnableMeshShaders=1` (`MeshShaders=1` golden variant) and
`noOcclusionCulling` variants identical; Renderer Debugger culling stats unchanged.
**Exit:** targets met. This is a **shippable stopping point** if Phases 3–4 are deferred.
**Docs:** `Rendering.md` § draw lists.

### Phase 3 — GPU LOD and visibility for the main view

**Goal:** no per-instance CPU work for main-view meshes; buckets uploaded only on change.

**Changes**
- `Editor/Resources/Shaders/MeshCulling.glsl` — per-instance LOD selection (port of
  `SelectStaticMeshLOD`, metric and `MeshLODDistanceScale` identical), per-LOD indirect args;
  **NEW** early-cull variant without HZB.
- `Renderer/SceneRenderer.cpp` — cull groups without LOD in the key; persistent `ObjectIndexes` /
  `MeshCullDrawData` uploads on change only; early cull node before `PreDepth`, prepass drawn
  indirect; graph accesses updated; all gated on `EnableGPUDrivenRendering && !EnableMeshShaders`
  (else Phase 2 path).
- Any new storage buffer: grep `(set, binding)` across `Editor/Resources/Shaders` first.

**Playbook — Add a new render pass** (`Architecture-LuxEngine.md` Part 4), for the early-cull node:
pipeline built once in `Init()`; shader registered as a dependency (and in the shader pack for
Runtime/Dist); accurate `Reads`/`Writes` (it writes the prepass indirect args and visible indices,
the prepass reads them); folded into `ComputeStructureHash()`; skipped at zero cost when the option
is off.

**Verification:** golden default + variants identical **including LOD transitions** (a capture at a
camera distance where LODs switch — Benchmark's generated meshes have LODs; confirm in Phase 0);
GPU time per pass not worse by more than 0.2 ms; Debug + sync validation run clean.
**Exit:** main-view CPU cost independent of instance count. **Rollback:** option off → Phase 2 path.
**Docs:** `Rendering.md` § GPU culling, § Performance rules.

### Phase 4 — GPU shadow culling and dirty-driven shadow caches

**Goal:** shadow lists built on the GPU; no per-frame transform hashing of casters.

**Changes**
- `MeshCulling.glsl` (or **NEW** `ShadowCulling.glsl`) — per-cascade and per-spot-tile frustum
  tests writing indirect shadow draws.
- `Renderer/SceneRenderer.cpp` — `ShadowMapPass` / `SpotShadowMapPass` draw indirect;
  `CalculateShadowCasterHashes` / `UpdateShadowCasterMotion` replaced by invalidation from the
  change list (casters created/moved/removed) plus the existing light/cascade state hashes; the
  static/dynamic cache split keeps working (static = not in the change list for
  `StaticShadowCasterStableFrames` frames).

**Playbook — Add a new render pass**, for each shadow cull node: same five steps as Phase 3.

**Verification:** golden identical (PCSS and default); `moving` variant: moved casters' shadows
correct; shadow cache still skips renders when nothing moves (pass GPU ≈ 0 in perf.json).
**Docs:** `Rendering.md` shadow section.

### Phase 5 — Hardening

Runtime mirror (`BuildRenderPacketRuntime`, Play), prefab edit mode, scene switch teardown, asset
hot reload of a mesh and a material, `SingleThreaded` policy, `Lux-Runtime` export with shader pack,
Linux build. Renderer Debugger shows retained-scene stats (cached records, changed this frame):
verify the new ImGui rows per `Conventions.md` § ImGui correctness (every `Begin*`/`Push*` closed on
every path, unique IDs, stable `PushID` in any loop) and look at them in the editor with nothing
moving and with `MoveEntities` running. Final A-B-A against Phase 0 and the success criteria. Docs
pass.

## Part 4 — Verification (whole plan)

- Golden: default + every `FEATURE_VARIANTS` entry + `moving`, pixel-identical to Phase 0 after
  every phase.
- Perf: perf.json medians, A-B-A in one session (session drift was 22→31 ms for the same code).
- Editor scenarios list in Phase 1, re-run after Phases 2–5.
- Baseline table (filled in Phase 0): RenderSceneSync / SubmitRenderScene / FlushDrawList / main
  thread / frame.

## Part 5 — Risks

- **LOD parity on the GPU** (Phase 3): a different rounding in the metric changes which LOD draws →
  golden diffs. Detect with a capture at an LOD boundary; port the metric verbatim.
- **Prepass becoming indirect** interacts with the meshlet path and alpha-tested materials →
  meshlet path explicitly stays on Phase 2; cutout materials keep the material-bound prepass.
- **Hidden per-frame consumers** of the cleared maps (debug views, stats, `CalculateShadowCasterHashes`,
  `GPUSceneDebugSnapshot`) → grep every reader of `m_MeshTransformMap` / `DrawList` before Phase 2.
- **Material edits not propagating** if a write bypasses the setters → `MaterialAsset` revision in
  every setter + editor check in Phase 1.
- **The O(entities) scan** may still cost >1 ms at 7k entities with hierarchy walks → measured in
  Phase 0/1; change lists (Bevy's move) are the follow-up.

## Part 6 — Open questions

- Do Benchmark's generated meshes have multiple LODs? (Phase 0: check `GetSubmeshLODCount`.)
- Is the transient path used by anything frequent in normal scenes (override materials)? (grep in
  Phase 2.)
- Does a hot-reloaded `StaticMesh` keep its handle (the sync reuses the previous proxy's Refs when
  the handle matches)? (Phase 5 test; possible pre-existing staleness.)
- `MoveEntities` determinism under the render thread. (Phase 0 spike.)

## Part 7 — Research notes

**Goal:** make static meshes retained so per-frame CPU cost tracks changes, not scene size.
- **Prior art:** Unreal's mesh drawing pipeline is retained: static draws are built once when a
  primitive is added and reused until removal; per-frame work is view relevance only, and the
  per-primitive command info is kept separate for cache efficiency. Maps to the submit cache and
  persistent buckets. [UE docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/mesh-drawing-pipeline-in-unreal-engine)
- **Prior art:** Unity's BatchRendererGroup keeps instance data resident on the GPU and rebuilds
  only a small visible-index list per frame; the GPU Resident Drawer adds GPU occlusion culling.
  Lux already has the resident `GPUScene`; Phases 3–4 add the GPU-side lists.
  [Unity blog](https://unity.com/en/blog/engine-platform/batchrenderergroup-sample-high-frame-rate-on-budget-devices),
  [Unity docs](https://docs.unity3d.com/6000.3/Documentation/Manual/urp/gpu-resident-drawer.html)
- **Pitfall:** Bevy found that iterating every visible entity to detect changes became a bottleneck
  and moved to change lists. Lux has no change signals, so Phase 1 keeps a cheap scan and measures
  it; change lists are the follow-up. [Bevy migration notes](https://git.adamlamers.com/PublicArchive/bevy/src/branch/main/_release-content/migration-guides/change_lists.md)
- **Pitfall:** Bevy's change detection reports *mutable access*, not a changed value, so cheap
  "touched" flags over-report; and its extract step, which walks the whole world each frame, was
  itself reported as a large cost in big scenes. Hence in-place value compares, and a measured scan.
  [Bevy issue 5082](https://github.com/bevyengine/bevy/issues/5082)
- **In-repo history:** `docs/ENGINE_OPTIMIZATION_PLAN.md` A1 (retained draw lists) named the same
  three blockers — CPU frustum culling feeding the lists, interleaved transient submissions, offset
  assignment assuming a fresh build. Phases 2–3 resolve each.
- **Rejected:** temporal reuse of last frame's visibility for culling decisions beyond the existing
  HZB (no temporal rendering rule).
