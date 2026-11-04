# LuxEngine — Lucky Engine Merge Plan

This plan turns the LuckyEngine study (`docs/LuckyEngine-Comparison.md`, `docs/lucky-research/*.md`,
`docs/Lux-History-Review.md`) into engine changes, phase by phase. It is a planning document: what is
actually built is described in `.claude/docs/Architecture-LuxEngine.md`, and every phase updates that
doc when it changes an interface. Lucky's source (`M:/LuckyEngine/MAC-Source`, Apache-2.0, pinned at
`b9ec0f117`) is a reference for *methods*; nothing is pasted wholesale, because bindings, threading
(Lucky's editor is single-threaded, Lux's Windows editor is not), vertex format and asset formats differ.

**Goal card**
- **Goal:** bring the transferable Lucky methods the research recommends into Lux, starting with the
  correctness and robustness candidates, then content quality and editor documentation.
- **Success:** each phase fixes a concrete failure or adds a concrete capability that can be checked
  (a fixture, a stress case, a build, a golden image), and leaves the engine shippable.
- **Non-goals:** Lucky's robotics stack (MuJoCo, ONNX policies, LiDAR, datasets, gRPC, Hub/auth), its
  path tracer, DLSS/NRD/temporal reconstruction, a CMake migration, replacing FMOD/VA, yaml-cpp,
  Lucky's per-frame CPU gather, or its whole-image resource-state model.
- **Constraints:** no temporal rendering (deferred by priority, not part of this plan); self-contained
  editor (only small permissive vendored libraries); existing `.luxproj` / `.luxscene` / assets and
  runtime packs keep loading; correct under both threading policies; editor launches only with the
  user's OK in that turn.

**Decisions this plan is built on**

| Decision | Choice | Consequence |
|---|---|---|
| Command-arena overflow policy | Grow by chaining fixed pages (never fail, never move) + high-water telemetry | No dropped command, no VERIFY in Release; a runaway producer shows up in stats instead of corrupting memory |
| Imported node placements | One extra `Submesh` descriptor per additional node reference, sharing the geometry range and its LODs | Every consumer (renderer, colliders, acoustic geometry, picking, bounds) sees all placements with no consumer change; explicit `SubmeshIndices` lists keep their indices |
| 2D slot overflow | Flush the batch and start a new one when a texture/font slot table is full | Arbitrary distinct textures per scene; submission order kept |
| Font atlas cache key | Hash of font bytes + atlas settings + format version, validated on read, written atomically; stays in `Resources/Cache/FontAtlases` beside the shader cache (moving it was not needed for correctness) | Changed/same-named fonts regenerate; corrupt files are a cache miss |
| WeakRef | Lazily-allocated control block + `Lock()` that is safe against a concurrent final release | +8 bytes per `RefCounted`; a weak user gets a `Ref` or nothing |
| LOD producer | meshoptimizer 1.1 (MIT, ~16k lines of C++), attribute-aware with border lock, replacing position-grid averaging | LOD visuals change (better shading at seams/hard edges); golden set re-baselined only with the user's acceptance |
| Help content | Packaged Markdown under `Editor/Resources/Docs`, rendered by an in-editor Help panel; no website, no account | Works offline; contextual buttons open a topic ID |
| Things the research already settled | Descriptor range ordering (Lux sorts by `baseRegisterIndex`), meshoptimizer meshlets (measured slower, Phase 8 of the render-features plan) | Not re-done |

## Status (2026-10-25, branch `feature/lucky-merge`, uncommitted)

| Phase | State | Evidence |
|---|---|---|
| 1 Command queue | Done | Standalone stress: 35 MB in one frame, 64-byte-aligned captures, a 5 MiB command, order kept, pages reused |
| 2 Node placements | Done | Assimp with Lux's flags: `tests/meshes/SharedMeshNodes.gltf` is 1 mesh placed by 3 nodes; the walk gives 3 placements (was 1) |
| 3 2D slots | Done | Builds; per-chunk tables bound on the render thread in command order. Not driven with >31 textures in the editor yet |
| 4 Font cache | Done | Builds; validation and atomic write by reading. Not exercised with a corrupt file yet |
| 5 WeakRef | Done | ASan stress: 300k last-release vs. `Lock()` races on 3 threads, no use-after-free; 111 forced address reuses all rejected. Live-address registry removed |
| 6 meshoptimizer LODs | Done; the user accepted the new images, golden reference is now `lm8` (Benchmark, RenderFeatures) | Goldens with LODs off are pixel-identical to `dev` (Benchmark, RenderFeatures). LOD error vs. full detail: Benchmark 8.9% → 2.9% of pixels (mean 1.12 → 0.30), RenderFeatures 0.21% → 0.03%. GPU 1.87 → 1.81 ms (single run). Import: Sponza 32 → 206 ms (grid LODs kept 7% of triangles at LOD1, meshoptimizer 51%) |
| 7 Help | Done, UI not yet driven | 17 pages, 0 broken links, all 7 C# snippets compile against ScriptCore; new projects now get a buildable `.csproj` (verified with `dotnet build`) |
| 8 Runtime mesh reader | Done | Headless harness against Core.lib (see Part 4) |

## Part 0 — Where we are (verified 2026-10-25 at `76062c9c`)

| Capability | State | Evidence |
|---|---|---|
| Render command arena bounds | ❌ unchecked: a 10 MiB arena written past its end | `Renderer/RenderCommandQueue.cpp` — `Allocate` writes header + payload with no capacity test; payload alignment is `alignof(RenderCommandFn)` only |
| Producer routing (main / render / background) | ✅ working | `Renderer.h` — `Submit` three-branch routing; must be preserved |
| Imported node placements | ❌ last node wins | `AssimpMeshImporter.cpp` — `TraverseNodes` assigns `Submesh::Transform` per node reference; `RenderScene.cpp` `RebuildStaticMeshGPUInstances`, `JoltShapes.cpp`, `PhysicsScene.cpp`, `Scene.cpp` (acoustics) each use one `submesh.Transform` |
| GPUScene instance identity | ✅ `(primitive, submesh index)` | `GPUScene.cpp` — `InstanceKey`; a duplicated submesh descriptor gets its own key |
| Renderer2D texture slots | ❌ 32 slots, write past the array on the 32nd distinct texture | `Renderer2D.cpp` (four `DrawQuad` variants) and `m_FontTextureSlots` in `DrawString`; `Flush()` is a TODO |
| Font atlas cache | ⚠️ name+size key, raw header trusted, relative `Resources/Cache` path | `Renderer/UI/Font.cpp` — `TryReadFontAtlasFromCache`, `CacheFontAtlas` |
| WeakRef | ⚠️ live-address check, no pinning, address reuse looks valid | `Core/Ref.h` — `WeakRef::IsValid`; cross-thread use in `ImGuiLayer.cpp` (render task) |
| LOD generation | ⚠️ position-grid averaging, rebuilt at every load | `Renderer/Mesh.cpp` — `BuildClusteredSubmeshLOD`, `BuildRenderGeometry`; no meshoptimizer in `Core/vendor` |
| Meshlet producer | ✅ settled: meshoptimizer meshlets measured slower here | `docs/RENDER_FEATURES_PLAN.md` Phase 8 |
| Descriptor range ordering | ✅ sorted and looked up | `RHI/RHIShader.cpp` — `std::sort` by `baseRegisterIndex`, `GetNRIRangeIndex` |
| In-editor help | ❌ Help menu has only "Open User Data Folder" and "About" | `EditorLayer.cpp` (command registry `add(..., "Help", ...)`); `docs/Editor/*.md` exist but ship nowhere |
| Runtime mesh pack reader | ⚠️ trusts offsets/counts | `Asset/MeshRuntimeSerializer.cpp` — `DeserializeFromAssetPack` |
| Fixed-step script hook | ❌ scripts only get frame `OnUpdate` before `StepPhysics` | `Scene.cpp` `OnUpdateRuntime`; `PhysicsScene::Simulate` |

Runtime verification so far: none (read and grepped only). Each phase states its own checks.

## Part 1 — Phases

### Phase 1 — The render command queue cannot overrun

**Changes**
- `Renderer/RenderCommandQueue.{h,cpp}` — pages of 4 MiB in a vector; `Allocate(fn, size, alignment)`
  places header and payload with the callable's own alignment, opens a new page when the current one
  is full (oversized commands get a page of their own size), never moves memory. `Execute` walks pages
  in order; spare pages are kept for reuse up to the high-water count. `GetStats()` reports
  commands/bytes/pages for the last executed frame and the high-water marks.
- `Renderer.h` — `Submit` passes `alignof(FuncT)`; `GetRenderCommandQueueStats()`.
- `Editor/Source/Panels/RenderStatsPanel.cpp` — one line with queue usage.

**Thread and lifetime:** unchanged — the main thread fills, the render thread executes after
`SwapQueues`; pages are owned by the queue.
**Verification:** Release build; a debug self-check that submits >10 MiB of commands in one frame
(command-line flag only, never by default) executes them all in order.
**Docs:** `Threading.md § Renderer::Submit` (no fixed capacity; alignment honoured).

### Phase 2 — Every imported node reference is a placement

**Changes**
- `AssimpMeshImporter.cpp` — `TraverseNodes`: the first node referencing a mesh keeps the original
  submesh; each further reference appends a copy of the submesh descriptor (same vertex/index range,
  material, bounds) with that node's transforms, and the node lists the new index. Mesh-source bounds
  include every placement's transformed AABB.
- `Mesh.cpp` — `BuildRenderGeometry` builds LODs (and meshlets) once per distinct geometry range and
  shares them with every submesh that uses the range.
- `MeshSource::GetBoundingBox` becomes the union of the placed submesh boxes (`AABB::Transformed`).

**Verification:** a generated glTF fixture (one mesh, three nodes, nested rotation, negative scale)
imports as three submeshes with three transforms; collider cooking and LOD counts match.
Runtime packs need no format change (submeshes are already serialized).
**Docs:** Architecture § 2.8 (mesh sources: one submesh per placement).

### Phase 3 — 2D batches survive any number of textures and fonts

**Changes:** `Renderer2D.{h,cpp}` — every vertex chunk (one draw) has its own texture/font slot table;
when a table is full the next primitive moves to the next chunk, and each chunk's table is bound to
the material on the render thread just before its draw (so the material is no longer written from the
main thread while the render thread reads it). Quad chunk rollover (`GetWriteableQuadBuffer`) stays.
**Verification:** 40 distinct sprite textures and 40 fonts in one scene draw without validation errors
(user-run, or a scene left for them).

### Phase 4 — Font atlas cache is content-keyed and validated

**Changes:** `Renderer/UI/Font.cpp` — key = FNV-1a of the font bytes + em size + atlas settings +
`k_FontAtlasCacheVersion`; header carries magic, version, dimensions, payload size; a mismatch is a
miss; writes go to a temporary file and `FileSystem::ReplaceFileAtomically`; superseded atlases of
the same font and size are removed. The folder stays `Resources/Cache/FontAtlases`, like the shader
cache.
**Verification:** truncated/garbage cache file regenerates; changed font with the same name regenerates.

### Phase 5 — WeakRef can be locked safely

**Changes:** `Core/Ref.{h,cpp}` — `RefCounted` gets an atomic pointer to a lazily created
`WeakControlBlock` (weak count, instance pointer, small mutex). `Ref::DecRef`'s final release clears the
block's instance under its lock before deleting; `WeakRef::Lock()` takes the lock, increments the
strong count only if it is non-zero, and returns a `Ref`. `IsValid()` reads the block. The live-address
registry stays for `Ref` debugging but no longer decides weak validity. Cross-thread users
(`ImGuiLayer` render tasks) switch to `Lock()`.
**Verification:** a stress self-test (two threads, last-release vs. lock, 1e6 iterations) in the
headless harness; full build.
**Docs:** `Conventions.md § Memory and ownership`, Architecture Part 3.

### Phase 6 — meshoptimizer conditions imported geometry

**Changes:** vendor `Core/vendor/meshoptimizer` (1.1, MIT) through `Dependencies.lua`/`Core/premake5.lua`.
`Mesh.cpp` — per distinct geometry range: vertex-cache + overdraw (threshold 1.05) reorder of LOD0
(same triangles), LOD chain from `meshopt_simplifyWithAttributes` (normals weighted 0.5,
`meshopt_SimplifyLockBorder`, ratio 0.5 per level, error 0.02 of extent, stop when < 5% saved),
replacing `BuildClusteredSubmeshLOD`. Rigged submeshes keep LOD0 only (as today).
**Verification:** import time per mesh logged before/after; golden set — LOD0 identical, distant LODs
expected to change: show crops, user accepts or rejects.

### Phase 7 — Help that ships with the editor

**Changes:** `Editor/Resources/Docs/` (**NEW**) — index + topic pages derived from `docs/Editor/*.md`
plus a first end-to-end tutorial; `Editor/Source/Panels/HelpPanel.{h,cpp}` (**NEW**) — topic list,
search over titles, back/forward, Markdown rendering (headings, paragraphs, lists, emphasis, inline and
fenced code with Copy, links to other topics); `ImGuiEx::DocsButton` (**NEW**) and a compile-time
component → topic table, drawn in component headers; Help menu entries and F1. Found while writing
the tutorial: a new project had no `.csproj`, so it could not build scripts at all;
`EditorLayer::CreateProject` now writes one.
**Verification:** user opens Help, follows a component button, searches, copies a code block.

### Phase 8 — Runtime mesh data is validated before it is published

**Changes:** `MeshRuntimeSerializer::DeserializeFromAssetPack` — check header, version, every offset/size
against the asset's packed size, counts against sizes, submesh ranges against vertex/index counts,
node children/submesh indices; read into locals, publish only when all pass.
**Verification:** headless harness feeds truncated and corrupted copies of a real packed mesh; each
returns null with a logged reason, none crashes.

## Part 2 — Follow-ups (not in this plan)

Each needs its own decision or measurement first: fixed-step C# `OnFixedUpdate` (gameplay semantics —
user decision); process host vs. project script ownership; optional feature modules; out-of-process
crash watchdog; prefab structural reconciliation; Jolt bulk body add; profiling readiness metadata
(warmup, readiness timeout, accepted-sample counts); cluster light culling (pre-transformed lights,
16-bit indices, visible overflow); PSC shadow-caster volume; split deferred lighting experiment;
dependency-aware editor geometry cache (profile import first).

## Part 3 — Risks

- **Placements multiply collider triangles.** Correct (each placement collides) but larger cooked
  meshes; measured on the sample project in Phase 2.
- **meshoptimizer LODs cost import time.** Measured in Phase 6; a geometry cache follows if it hurts.
- **WeakRef touches the most-included header.** Full rebuild; behaviour verified by the stress test.
- **Editor runs need the user's OK.** Phases end with a build and harness checks; UI and golden checks
  are listed for the user.

## Part 4 — Research notes

The research brief is the study itself (`docs/LuckyEngine-Comparison.md`, decisions table). Extra
sources for this plan: meshoptimizer 1.1 header (`meshopt_simplifyWithAttributes`,
`meshopt_SimplifyLockBorder`; vendored copy in Lucky's tree, MIT); imgui_markdown is zlib but has no
code blocks ([enkisoftware/imgui_markdown](https://github.com/enkisoftware/imgui_markdown)), so the Help
panel renders its own Markdown subset.
