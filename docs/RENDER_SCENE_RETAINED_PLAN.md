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
  scene sync + submit + draw-list flush drops from **17.3 ms** (98% of a 17.7 ms main thread,
  measured in Phase 0) to **under 1.5 ms** for a frame where nothing changed; every golden capture stays
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
- `Renderer/SceneRenderer.h/.cpp` — **NEW** public `RecordStageCPUProfile(name, ms)` and
  `ScopedStageProfile`; stage zones `"SubmitRenderScene"`, `"FlushDrawList"` (kept out of
  `TotalCPUTime`, because they enclose pass zones).
- `Scene/Scene.cpp` — time the mirror sync into **NEW** `FrameRenderPacket::MeshSyncMs`;
  `SubmitRenderPacket` records it as `"RenderSceneSync"` after `BeginScene`.
- `Editor/Source/Tools/GoldenCapture.cpp` — **NEW** option `MoveEntities=N`: before each frame,
  offset the first N `StaticMeshComponent` entities (by UUID order) by a translation that is a pure
  function of the frame number; restore at exit. `tests/rendering/golden_run.py` — **NEW**
  `"moving": "MoveEntities=256"` feature variant.

**Verification:** Release build; 3 runs default + 3 runs `MoveEntities=256` (A-B-A not needed —
no behaviour change); perf.json shows the three new zones; the moving capture is deterministic
(3 runs pixel-identical to each other).
**Exit:** baseline table recorded in this file (Part 4). **Rollback:** revert; zones are inert.

**Status: done 2026-10-18.** The moving captures are identical to each other and match the unmoved
capture within `golden_compare` tolerance (37 px, max 18, in the capsule rack: coplanar surfaces
whose draw order changes when meshes move). One default run (the first launch after the rebuild)
rendered 0.28% differently from the other two, which matched earlier sessions exactly — a
pre-existing first-launch flake to keep in mind when reading a single run.

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

**Status: done 2026-10-19, sync target met (0.53 ms).** First pass 2026-10-18 reached 1.23 ms; a
second pass (sub-stage timers showed the per-entity `SyncStaticMesh` loop at 0.68 ms of a 1.15 ms
sync) added a dense per-entity record of last frame's inputs in sync order, so an unchanged entity
is a sequential compare that never touches its proxy, plus `MaterialTable::GetGlobalRevision()` so
no table is read per entity while none changed, and skips `EndSync`'s removal walk when every proxy
was synced. Loop 0.68 → 0.10 ms. A-B-A against the first pass (one session, medians of 3): sync
1.27 / **0.53** / 1.28 ms, main thread 10.17 / **9.98** / 10.49 ms; golden default + 15 variants
identical to the first pass, `moving` identical to Phase 0; moving sync 0.85 ms. First pass as built:
`RenderScene::SyncStaticMesh(entityID, StaticMeshSyncInputs)` resolves meshes only when the handle
changes, recomputes bounds only when the transform or mesh changes, and rehashes the material table
only when the table or its **NEW** `MaterialTable::GetRevision()` changes (`MaterialTable`'s
mutable `GetMaterials()` was removed so every edit is counted). The visible list is filled during
the sync. Material rows (`AcquireMaterial` / `ReleaseMaterial`) and texture slots
(`AcquireTexture` / `ReleaseTexture`) are reference counted; `MaterialScene::EndSync` rebuilds a
row when the asset object or `MaterialAsset::GetRevision()` changes. `SetAlbedoColor` and
`SetMaterial` did not reach `UpdateMaterialComplexityMetadata`, so they bump the revision themselves.
Golden: Benchmark default + all 15 feature variants match HEAD in the same session (`p1A` vs
`p1B2`, worst `debugCategories` 2 px); `moving` is pixel-identical to Phase 0's moving capture.
Perf A-B-A (medians of 3): sync 6.42 / **1.23** / 6.13 ms, main thread 15.02 / **9.98** / 14.34 ms,
frame 15.85 / **10.82** / 15.67 ms; Submit and Flush unchanged (Phase 2). Moving (single runs):
sync 6.17 → 1.28–1.39 ms. The remaining sync is the O(entities) scan (view walk, selection
callback, parallel transforms, one hash lookup and compare per entity) — not yet sampled.
Editor scenarios (material edit, Visible toggle, select, delete/undo, Play/Stop) passed in a manual
pass by the user on 2026-10-19. Side finding: `EditorLayer::CastMousePick` ray-tests every entity every frame
(~17% of the Benchmark main thread in the sampler), outside this plan.
An intermittent access violation inside NRI command recording, mostly at shutdown, hit both the
HEAD and Phase 1 builds in this session (event log); it is not caused by this phase.

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

**Status 2026-10-19: step 1 done, target not yet met.** Step 1 (no change to draw output):
per-proxy submit cache (`m_ProxySubmitCache`: material resolution, asset, sort keys, flags);
`SubmitMeshPassDraw` counts repeat keys before touching the draw-command cache; per-pass sort keys
read once per frame; shadow-caster motion in a dense vector with an exact row compare instead of a
hashed `unordered_map`; transform-bucket vectors recycled through a pool; the submesh world
transform computed only where it is used; `Ref` parameters by const reference. A-B-A against
Phase 1 (one session, medians of 3): SubmitRenderScene 9.66 / **4.48** / 8.68 ms, FlushDrawList
1.76 / 1.87 / 1.62 ms, main thread 12.54 / **7.70** / 11.24 ms; the Benchmark default image is
pixel-identical in all three B runs. The golden feature variants were not run for this step (the
user chose perf-only verification). Remaining: Submit + Flush 6.35 ms vs the 2.5 ms target — the
per-instance culling/LOD and two hash lookups per pass per instance, and the flush (object-index
build, `CalculateShadowCasterHashes`). Next is a sampler profile, then persistent buckets.

**Step 2 (2026-10-19, no change to draw output).** A sampler profile after step 1 put the per-submesh
submit lambda at 27% self (culling, LOD, bucket appends) and `MeshKeyHasher` + two draw-list /
transform-map lookups per pass at ~15%. Changes: `MeshKeyHasher` is a multiply-xorshift mix
(MSVC's `std::hash<uint64_t>` is byte-wise FNV; draw order never depends on map order because
`BuildSortedDrawCommandOrder` sorts on every key field); each transform bucket remembers its draw
per pass (`TransformMapData::PassCommands`, `ShadowTransformMapData::ShadowCommand`), so an opaque
instance costs one hash lookup instead of three and a shadow instance one instead of two;
`CalculateShadowCasterHashes` hashes the rows 64 bits at a time. A-B-A against step 1 (medians of
3, this session ran faster than the step 1 session): SubmitRenderScene 3.91 / **3.27** / 3.93 ms,
FlushDrawList 1.48 / **1.26** / 1.50 ms, main thread 6.34 / **5.53** / 6.42 ms; default image
pixel-identical. **Submit + Flush is 4.53 ms, target 2.5 ms not met.** What remains is the
per-instance camera work itself — main-view, cascade and spot frustum tests and LOD selection for
~10k submeshes, plus the per-instance bucket append — which this phase keeps on the CPU by design;
Phase 3 moves exactly that to the GPU. Closing the gap on the CPU would need instance buckets that
persist across frames and are only edited when an instance's visibility or LOD changes.
Golden (2026-10-19, after step 2): Benchmark default + all 15 feature variants (including
`meshShaders`, `noOcclusionCulling`, `colliders`, `moving`) are pixel-identical to the Phase 1 set
(`p1C` vs `p2C`, 0 px), and `moving` still matches Phase 0.
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

**Status 2026-10-19: implemented, golden identical, Debug validation run blocked.** As built: proxy
instances with a GPUScene row join persistent main-view `CullGroup`s (mesh, submesh, and material key
when transparent; `MeshKey::GPUCullGroup`), rebuilt during a submit only when the scene's change list
or the submit-cache epoch moved. `FlushDrawList` expands each group into one draw per LOD
(`ExpandCullGroupDraws`), each with its own visible range and indirect args; `MeshCulling.glsl`
(three `uvec4` per cull record, LOD thresholds packed, no new binding) picks each instance's LOD with
`SelectStaticMeshLOD`'s metric. An early dispatch of the same shader (`EarlyPass`, frustum + LOD, no
HZB) writes the prepass's instances into `ObjectIndexes` (now `GPUOnly`), and the prepass draws
cull-group draws indirect. Selection outlines, shadows and non-proxy submissions keep the CPU path;
gated off with the mesh-shader prepass. Golden: Benchmark default + 15 variants pixel-identical to
Phase 2 (`p2C` vs `p3A`; one `noSSR` run showed a stray selection outline from a click during the
capture, its re-run was identical). Perf A-B-A against Phase 2 (medians of 3): SubmitRenderScene
3.40 / **2.91** / 3.30 ms, FlushDrawList 1.26 / 1.37 / 1.24 ms, main thread 5.64 / **5.22** / 5.52 ms;
GPU: early cull 0.05 ms, other passes within noise. Debug + sync validation: blocked at first by an
unrelated ImGui assert and transfer-queue barrier errors (fixed in 0155a567); the Debug build of
this checkout (Phase 3 included) then captured Benchmark with 0 validation ids. Open: at Debug exit,
VMA reports "Unfreed dedicated allocations" (first observable now; not yet attributed — the
renderer's caches hold mesh and material Refs until the SceneRenderer is destroyed, as its
draw-command cache already did). What remains per instance on the CPU is shadow culling/LOD
(Phase 4).

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

**Status 2026-10-19: implemented and verified; one open GPU question (moving scenes).** Golden:
default + 15 variants identical to Phase 3 (`p3A` vs `p4A`; `noSSR` checked against the clean re-run,
and `debugCategories` flips between two pre-existing states — its image equals Phase 1's `p1A`).
Perf A-B-A against Phase 3 (medians of 3): SubmitRenderScene 3.16 / **0.30** / 3.22 ms, main thread
5.66 / **2.87** / 5.84 ms, frame 6.65 / **4.25** / 6.85 ms; FlushDrawList unchanged (~1.5 ms). With
nothing moving the shadow passes and the shadow cull stay at 0 ms GPU (cached). Debug + sync
validation: 0 validation ids.
Moving scenes, A-B-A of `MoveEntities=256` against Phase 3: the first version cost +0.7 ms GPU (shadow
cull 0.32 ms re-culling every static caster each moving frame, ShadowMapPass +0.3 ms from indirect
dynamic draws). Fixed by keeping moving casters on the CPU shadow path (groups hold static casters
only) and running the shadow cull only when a static cache re-renders: GPU 5.44 / 5.78 / 5.54 ms,
shadow cull 0 ms, frame 8.25 / **6.92** / 8.17 ms, main thread 7.09 / **5.09** / 6.56 ms, moving images
identical. **Open:** ShadowMapPass stays ~0.2 ms above Phase 3 on moving frames (2.96–3.07 vs
2.2–2.9 ms) although its draws, the static cache and the cull are now the same; declaring the shadow
passes' buffer reads only on cull frames did not change it (reverted). Needs a GPU capture.
As built: a shadow-casting cull-group instance that is static — its proxy has not moved (**NEW**
`StaticMeshRenderProxy::LastMovedFrame`) for `StaticShadowCasterStableFrames` syncs — joins a
`ShadowCullGroup` (mesh, submesh); the groups rebuild on the frame one settles
(`m_NextStaticCasterFrame`). A caster still moving keeps the CPU shadow path (exact lists,
`UpdateShadowCasterMotion`, `CalculateShadowCasterHashes`). **NEW** `ShadowCulling.glsl` (same buffers
and slots as `MeshCulling.glsl`, no new binding) runs before the shadow passes only when a static
cache re-renders, and appends each instance to its (cascade or spot atlas, LOD) slot, testing the
CPU's own `Frustum` planes (packed after the cull records) and the tiny-caster rule; the static
caches draw those slots indirect. The LOD metric moved to **NEW** `Include/GLSL/MeshLOD.glslh`,
shared by both cull shaders. Instead of hashing the static casters' transforms, the static caches
re-render when the static caster set (an order-independent hash) or the cull's camera inputs
(`ShadowCullCameraState`) change. With the groups kept, `SubmitRenderScene` skips every non-selected
proxy they cover. Behaviour change: while the camera moves, the static caches re-render on any
position change (the CPU path only did when an LOD, tiny-caster or cascade membership changed); a
static camera renders them once as before. Golden on the final build (`p4E`): default + 15 variants
identical to Phase 3 (`noSSR` against its clean re-run; `debugCategories` in its Phase 1 `p1A` state).

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

**Status 2026-10-19: done.** The user's editor pass (Play/Stop, prefab edit mode, hot reload of a mesh and a material, the Retained Scene rows with nothing moving and moving) passed. Golden on the final
build (`p5A`): default + 15 variants identical to Phase 4 (`debugCategories` in its other known
state, equal to `p1B2`), 0 validation ids. Single-threaded policy (`--threading single`): identical
to the multi-threaded image, 0 validation ids (sync 1.59 ms there: the transform pass runs inline).
Final A-B-A against Phase 0 in the Part 4 table. Done: **NEW**
`SceneRenderer::Statistics::RetainedScene` and a "Retained Scene" table in the Renderer Debugger's
GPU Scene tab (one table, unique ID, no loops; closed on its only path). The open question on
hot-reloaded meshes is answered — a proxy kept the `StaticMesh`/`MeshSource` it first resolved
while the handle stayed the same, so a reload never reached it (pre-existing, carried over from
before Phase 1); fixed with **NEW** `AssetManagerBase::GetDataGeneration()` (bumped by `ReloadData`,
async loads landing, runtime-pack reloads), which `RenderScene` compares to re-resolve every mesh on
the frame it moves. Scene switch / Play / `Clear`: every renderer cache keys on the render scene's
unique generation, so a new mirror rebuilds them on its first submit; no new GPU allocations to
tear down. Lux-Runtime builds (Release). Linux: the new code is portable C++20 and the Linux build
uses clang (`Linux-Build.sh`), which accepts the `Ref<T> T;` members the new structs share with
`StaticDrawCommand`; not built on Linux here.

## Part 4 — Verification (whole plan)

- Golden: default + every `FEATURE_VARIANTS` entry + `moving`, pixel-identical to Phase 0 after
  every phase.
- Perf: perf.json medians, A-B-A in one session (session drift was 22→31 ms for the same code).
- Editor scenarios list in Phase 1, re-run after Phases 2–5.
- Baseline (Phase 0, 2026-10-18, Release at 42bc7805 + Phase 0, medians of 3, one session):

  | Variant | RenderSceneSync | SubmitRenderScene | FlushDrawList | Main thread | Frame | GPU |
  |---|---|---|---|---|---|---|
  | default | 7.61 ms | 8.09 ms | 1.57 ms | 17.70 ms | 20.87 ms | 2.86 ms |
  | `MoveEntities=256` | 7.41 ms | 7.60 ms | 1.54 ms | 16.82 ms | 19.66 ms | 4.47 ms |

  Moving costs the same as static today because every mesh is rebuilt every frame either way; after
  Phase 1–2 the moving row shows what changes cost, the default row what retention saves.

  After Phase 1 (A-B-A in one session, see the Phase 1 status for the A rows):

  | Variant | RenderSceneSync | SubmitRenderScene | FlushDrawList | Main thread | Frame | GPU |
  |---|---|---|---|---|---|---|
  | default | 1.23 ms | 7.13 ms | 1.35 ms | 9.98 ms | 10.82 ms | 2.00 ms |
  | `MoveEntities=256` (1 run) | 1.28 ms | 6.57 ms | 1.25 ms | 9.33 ms | 12.33 ms | 4.30 ms |

- **Final result (2026-10-19, after Phase 5).** A-B-A in one session, Phase 0's build (`8346bfd0`)
  vs the final build, Benchmark default, medians of 3:

  | | RenderSceneSync | SubmitRenderScene | FlushDrawList | Sync + submit + flush | Main thread | Frame |
  |---|---|---|---|---|---|---|
  | Phase 0 (A1) | 8.35 ms | 8.61 ms | 1.77 ms | 18.73 ms | 19.00 ms | 23.00 ms |
  | **Final (B)** | **0.68 ms** | **0.26 ms** | **1.44 ms** | **2.38 ms** | **2.69 ms** | **6.92 ms** |
  | Phase 0 (A2) | 7.94 ms | 8.41 ms | 1.66 ms | 18.01 ms | 18.39 ms | 22.15 ms |

  Against the success criteria: sync + submit + flush fell ~7.7x (18.4 → 2.38 ms) but **misses the
  < 1.5 ms target**, almost entirely because of `FlushDrawList` (1.44 ms: the object-index build,
  the bindless texture and material tables, `CalculateShadowCasterHashes` for the CPU-path casters);
  the frame is 3.3x faster and main-thread time 7x lower. Images: the final build's Benchmark capture
  is pixel-identical to Phase 0's own (`p1A` vs `p5A`, 0 px), and every phase matched the one before
  it across the golden set. Caveat for reading old builds: shaders compile from source at runtime,
  so an old exe loads the current `MeshCulling.glsl`, whose layout changed in Phase 3; Phase 0's exe
  now renders nondeterministically (two of its runs differ by 13%) and its GPU numbers are not
  comparable, while its CPU numbers are. The same applied to the Phase 3 A-B-A's A arm (CPU columns
  only were used). Open after the plan: `FlushDrawList` (~1.4 ms) and the moving-scene
  ShadowMapPass +0.2 ms (Phase 4).

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
