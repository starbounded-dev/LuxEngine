# LuxEngine Material Editor Plan

The phased design for a real material editor in LuxEngine: a rich standard material first, then a
node-based material graph, with live preview, thumbnails, instances, and the organisation tools
people expect from Unreal, Unity, Godot, and Blender. This is a planning document, not a description
of what exists — `.claude/docs/Architecture-LuxEngine.md` (§ 2.3 Renderer, § 2.8 Asset system,
§ 2.9 Editor) describes what is actually built.

*Written 2026-09-18 against `sound-fmod-va` (`b2c2fa0b`), Dear ImGui 1.92.6 WIP.*

---

## Goal card

- **Goal:** a material editor with the real features of Unity, Unreal, Godot, Blender and other 3D
  tools — standard material first, node graph after.
- **User's feature list (verbatim intent):** property controls (base colour, roughness, metallic,
  sheen, opacity, emission); texture mapping (assign, tile, blend diffuse/normal/bump/specular); a
  node graph (math, texture, vector nodes) with search, minimap, comment boxes and sub-graphs; a
  real-time preview on a primitive or model; geometry attribute nodes (UV, geometric normal, vertex
  colour, object/world position); exposed parameters editable in the Lux inspector as material
  instances without reopening the graph; Mix/Lerp, Math, Vector Math, Color Ramp; procedural noise
  (Perlin, Voronoi, Noise, Musgrave-style fractal); live preview + thumbnails; triplanar.
- **Success:** an artist creates a material from the Content Browser, edits it with a live preview,
  builds procedural looks in a graph, exposes parameters, makes instances, and sees correct results
  in the scene, the thumbnails and an exported game.
- **Non-goals:** texture painting and baking (own plan later); vertex/geometry displacement from
  the graph; post-process or decal graphs; importing Blender/Substance node graphs.
- **Constraints:** ImGui stays. No new mandatory installs (`CLAUDE.md § Product Principle`) — only
  small permissive libraries. No temporal techniques. Existing `.lmat` files keep loading. Works in
  the editor, `Lux-Runtime`, Dist (no shader compiler at run time) and on Linux.

## Decisions this plan is built on

| Decision | Choice | Consequence |
|---|---|---|
| Material model | **Standard material first, node graph later** (user, 2026-09-18) | Phases 1–6 give most value inside today's single GPU-driven pipeline; the graph (7–13) is gated by a renderer spike. |
| Material instances | **Yes** (user) | Parent/child `.lmat` with per-property overrides; graphs expose parameters to instances. |
| Old panels | **Replace** (user) | The unregistered `MaterialEditorPanel` / `MaterialsPanel` are deleted once the new editor covers them. |
| Node editor library | **thedmd/imgui-node-editor** (MIT), vendored | Already runs on ImGui 1.92.0 in the local Hazel repo; has groups (comment boxes), copy/paste, zoom. Minimap is built by us. imnodes is the fallback (built-in minimap, no comments). [Part 7](#part-7--research-notes) |
| Graph editor framework | Port patterns from Hazel's `NodeGraphEditor` (Apache-2.0, `M:\Git\Hazel`) | Keeps attribution/NOTICE; uses `choc`, which LuxEngine already vendors (`Dependencies.lua`). |
| Where advanced lobes live | Clearcoat/sheen/subsurface read **per material** in deferred lighting through the G-buffer `MaterialID` | No G-buffer growth; these parameters are per-material, not per-pixel, in the deferred path (per-pixel works in the transparent forward pass). |
| Graph output | Graph → GLSL *surface function* inserted into the engine's G-buffer and transparent shader templates | Graphs change surface inputs, not the lighting model or vertex positions (non-goal). |
| Preview renderer | Second `SceneRenderer` through the editor's `Viewport` class, small and without editor targets — **decided by the Phase 0 spike** | If the spike shows unacceptable cost, fall back to a dedicated lightweight preview renderer. |

---

## Part 0 — Where we are

Everything here was read in source this session; nothing below was exercised at run time except
where stated.

| Capability | State | Evidence |
|---|---|---|
| Editing a material's properties in the editor | ❌ **None reachable** | `MaterialEditorPanel` / `MaterialsPanel` exist but were unregistered from `EditorLayer` in `a5c0600d`; nothing adds them to `PanelManager` today |
| Assigning a material to a mesh | ⚠️ Slot 0 only | `SceneHierarchyPanel.cpp` ~3379–3401 edits `MaterialTable->SetMaterial(0, …)` only |
| Opening a `.lmat` from the Content Browser | ❌ Missing | `EditorLayer.cpp` registers activate callbacks for Scene, Prefab, AudioProject, AudioBank, ScriptFile — not Material |
| Material data model | ⚠️ Basic PBR | `MaterialAsset.h`: albedo colour, metalness, roughness, emission (scalar), transparency; albedo/normal/metalness/roughness maps; flags `DepthTest`, `Blend`, `TwoSided`, `DisableShadowCasting` (`Material.h`) |
| GPU material table | ✅ Working (renders today) | `MaterialScene.h` — `GPUMaterialData` (4 × `vec4`: BaseColor, Scalars, TextureIndices, Metadata); GLSL mirror `Include/GLSL/MaterialScene.glslh`, SSBO `(set 2, binding 7)`, textures `u_GPUMaterialTextures[1024]` `(2, 8)` |
| Emission colour | ❌ Missing | `DeferredLighting.glsl`: `color += m_Params.Albedo * emission` — emission only scales albedo |
| Alpha cutout | ⚠️ Shader-ready, never set | `GBuffer_Static.glsl` discards when `GPU_MATERIAL_ALPHA_MASKED`; `MaterialScene.cpp` only writes `Opaque` or `Blend` |
| Two-sided | ⚠️ Flag only | `GPUMaterialFlags::TwoSided` is written; no per-material cull-mode switch found (`BackfaceCulling` is set per pass in `SceneRenderer.cpp`) — verify in Phase 3 |
| G-buffer channels | ✅ | `LuxGBuffer.glslh`: BaseColor+Opacity, ViewNormal, Metal/Rough/**AO**/**Specular**, MaterialID+ObjectID — AO and Specular channels already exist |
| Per-draw pipeline selection | ⚠️ Keys exist | `SceneRenderer::StaticDrawCommand` carries `PipelineSortKey` / `ShaderSortKey` / `MaterialSortKey`; the G-buffer pass draws `opaquePass.DrawOrder` with one geometry pass |
| Thumbnails | ⚠️ Cache only | `ThumbnailCache` stores/loads images; `SetThumbnailImage` is never called — no generator |
| Preview renderer | ⚠️ Pieces exist | `Editor/Source/Viewport/Viewport.h` wraps `SceneRenderer`; `SceneRendererSpecification::EnableEditorRenderTargets` can drop ~180 MB of editor targets |
| Runtime packing | ✅ | `MaterialSerializer::SerializeToAssetPack` / `DeserializeFromAssetPack` |
| Undo for non-scene edits | ✅ | `EditorLayer::PushUndoCommand(label, undo, redo)` |
| Dependency updates | ✅ Hook exists | `MaterialAsset::OnDependencyUpdated(AssetHandle)` |
| Mesh vertex attributes | ⚠️ No colour / UV2 | `Mesh.h` `Vertex`: Position, Normal, Tangent, Binormal, Texcoord |
| Node editor library | ❌ Not in LuxEngine | Only `NodeGraph/*.png` icons (`EditorResources.h`). A 1.92-patched imgui-node-editor exists in `M:\Git\Hazel\Hazel\vendor\imgui-node-editor` (MIT) |

---

## Part 1 — Goals and non-goals

**Feature targets by source**

| From | Brought in |
|---|---|
| **Godot StandardMaterial3D / Unity Lit** | ORM maps, emission colour + map, AO, height/parallax, cutout/blend/opaque, two-sided, UV tiling/offset, triplanar, detail maps, clearcoat, sheen ("rim"), subsurface, anisotropy, normal strength |
| **Unreal** | Material instances (parent/child overrides), exposed parameters, material graph with search palette, comments, reroutes, preview viewport with shape/HDRI, stats (instruction/texture count), node errors |
| **Unity Shader Graph** | Sub-graphs, blackboard of exposed properties, preview on nodes |
| **Blender** | Principled-BSDF-style inputs, Color Ramp, Noise (fBm / multifractal / ridged — Musgrave merged into Noise since 4.1), Voronoi, Mix, Math/Vector Math, Texture Coordinate / Geometry nodes, Material Preview with HDRI |
| **Substance / 3D tools** | Channel-packed maps with channel selection, tiling preview on plane |

**Performance budget** (Release, `/profile`, focused): opening the editor adds no scene frame-time
while the preview is hidden; the preview renders at ≤ 512² and ≤ 1 ms GPU; graph edits recompile
off the main thread; a graph material costs no more than ~1.5× the standard material in the G-buffer
pass for an equivalent look.

---

## Part 2 — Design

### Layers

```
Core/Source/Lux/Renderer/MaterialAsset.*        standard material data (+ parent/overrides, + graph ref)
Core/Source/Lux/Renderer/MaterialScene.*        GPU material table (grown in Phase 3)
Core/Source/Lux/Renderer/MaterialGraph/          NEW  graph model, node registry, GLSL codegen (no ImGui)
Core/Source/Lux/Asset/MaterialGraphSerializer.*  NEW  .lmatgraph YAML + asset-pack
Editor/Source/Panels/MaterialEditor/             NEW  editor panel, preview, graph canvas (ImGui)
Core/vendor/imgui-node-editor/                   NEW  MIT, vendored via Dependencies.lua
Editor/Resources/Shaders/Include/GLSL/LuxNoise.glslh      NEW  Perlin/simplex/Voronoi/fBm (own code)
Editor/Resources/Shaders/Templates/                        NEW  G-buffer / transparent graph templates
```

Graph **model and codegen** live in `Core` (the runtime needs them only to load compiled results,
and the exporter needs codegen); the **canvas** lives in `Editor`. `Core` never includes
`Editor/Source/**`.

### Threads

| Work | Thread |
|---|---|
| Panels, graph canvas, inspector | Main (ImGui) |
| Preview and thumbnail rendering | Render thread, through the preview `SceneRenderer` and `Renderer::Submit` |
| Graph codegen | Main (cheap, ms) |
| Graph shader compile (shaderc) | Dedicated `Lux::Thread` compile worker; results swapped in on main, pipelines rebuilt through `Renderer::RegisterShaderDependency` / `Renderer::OnShaderReloaded` |
| Asset save/load | Main, as today |

`JobSystem::Submit` is not used for compiles: it runs inline under `SingleThreaded` (Linux default).

### Data and compatibility

- `.lmat` gains optional keys only; every missing key deserialises to today's default. The runtime
  asset-pack material block gets a version bump with defaults for older packs.
- `.lmatgraph` is a **NEW** asset type (graph document). A graph material is a `.lmat` whose
  `Graph:` key references a `.lmatgraph`; instances reference the `.lmat`.
- Graph shaders are ordinary `ShaderLibrary` entries, so runtime export's
  `ShaderPack::CreateFromLibrary` packs them for Dist.

---

## Part 3 — Phases

**Every UI phase** runs the ImGui check from `Conventions.md § ImGui correctness` (scopes closed on
every path, unique IDs — property rows keyed by property name, graph nodes/pins/links by their
stable IDs — popup/window/dock names matched, every new UI state driven). **Every phase** builds
Release and Debug, runs the editor on `LuxSampleProject`, and ends with `/cr`. Phases adding files
regenerate projects.

### Phase 0 — Spike: a second renderer for previews, measured

**Goal:** know whether a preview can use a second `SceneRenderer` safely and cheaply.

**Changes (spike branch)** — create a `Viewport` with its own tiny `Scene` (sphere, directional
light, sky light), `EnableEditorRenderTargets = false`, 256² → 512², render it in a test window.

**Measure / answer:** VRAM and CPU/GPU frame cost with the main viewport open; any shared static
state that breaks (per-renderer `MaterialScene`, `(set, binding)` registries, shadow atlases);
teardown on project close and scene switch (GPU drain needed?); both threading policies.

**Exit:** a results table in Part 7 and a go/no-go. **Rollback:** discard the branch; fall back to a
dedicated forward-lit preview renderer.

### Phase 1 — The Material Editor exists and edits today's material

**Goal:** double-click a material → edit every current property with a live preview, undo and save.

**Changes**
- **NEW** `Editor/Source/Panels/MaterialEditor/MaterialEditorPanel.{h,cpp}` — tabs per open
  material; property grid for albedo, metalness, roughness, emission, transparency, shadow casting;
  texture slots via `ImGuiEx::PropertyAssetReference` with drag-drop from the Content Browser;
  dirty state, Save / Revert; every edit through `EditorLayer::PushUndoCommand` (passed in as a
  callback — the panel does not include `EditorLayer`).
- **NEW** preview (per Phase 0): shape picker (sphere, cube, plane, cylinder, the selected entity's
  mesh), orbit camera, HDRI/sky toggle, background toggle, tiling preview on plane.
- `EditorLayer.cpp` — register the panel; `RegisterItemActivateCallbackForType(AssetType::Material, …)`
  opens it; "Create Material" already exists in the Content Browser (`ContentBrowserPanel.cpp` ~1483).
- `SceneHierarchyPanel.cpp` — material table shows **every submesh slot** (not only slot 0) with an
  "Edit" button per slot.
- Delete `Editor/Source/Panels/MaterialEditorPanel.*` and `MaterialsPanel.*` (dead since `a5c0600d`).
- Docs: `docs/Editor/Panels.md`, `Architecture-LuxEngine.md § 2.9`.

**Playbook:** Add a new editor panel (Architecture Part 4) — all steps.

**Thread and lifetime:** UI on main; preview renderer owned by the panel (`Ref`), destroyed with it;
GPU resources freed through the renderer's normal release path.

**Verification:** open a Sponza material, change roughness → preview and main viewport update; undo;
save; reopen; edit a multi-submesh model's slot 3; close the panel with unsaved changes (prompt);
ImGui check.

**Exit:** materials are editable again; old panels gone. **Rollback:** unregister the panel.

### Phase 2 — Thumbnails that show the material

**Goal:** material thumbnails in the Content Browser show the rendered sphere.

**Changes:** a thumbnail generator that renders the preview scene at 128² for materials whose
thumbnail is stale (`ThumbnailCache::IsThumbnailCurrent`), one per frame at most, and stores it with
`ThumbnailCache::SetThumbnailImage`. Regenerates on save and on dependency (texture) change.

**Verification:** create 20 materials → thumbnails appear without a frame spike (`/profile`); edit a
texture → dependents refresh; restart → thumbnails load from cache.

**Open question resolved here:** whether `ThumbnailCache` writes to disk from a GPU image directly or
needs a CPU readback path.

### Phase 3 — "Lux Standard": the full standard material

**Goal:** the standard material matches Godot/Unity Lit for everyday work.

**Changes**
- `MaterialAsset` / `MaterialSerializer` — new optional properties: emissive **colour** + map +
  intensity (fixes emission-scales-albedo); **ORM** packed map with per-channel selection, or
  separate AO map; specular (F0, into the existing G-buffer Specular channel); normal strength;
  height map → bump, optional parallax occlusion; UV tiling / offset / rotation; alpha mode
  **Opaque / Cutout(threshold) / Blend** (writes the already-supported `Masked` mode); two-sided.
- `MaterialScene.h` `GPUMaterialData` **and** `MaterialScene.glslh` `GPUMaterial` grown in the same
  change (std430; the SSBO keeps `(set 2, binding 7)` and its name, so no collision). More texture
  indices (second `uvec4`).
- `GBuffer_Static.glsl`, `PreDepth` (cutout), `LuxPBR_Transparent.glsl`, `DeferredLighting.glsl`
  (emissive colour, AO) — consume the new fields.
- Two-sided: add a cull-mode bucket for two-sided materials in the G-buffer, pre-depth and shadow
  passes (verify what `TwoSided` does today first).
- `AssimpMeshImporter` — map emissive colour/factor and alpha mode where the source file has them.
- Asset-pack material block version bump with defaults.

**Verification:** old `.lmat` files render identically (screenshot compare on Sponza); each new
property visibly works in preview and scene; cutout leaves on a plant mesh cast correct shadows;
Linux and Dist export render the same; no binding-collision log; `/shader-debug` clean.

**Docs:** `Rendering.md` (material table layout), `Architecture § 2.3`.

### Phase 4 — Advanced shading lobes

**Goal:** clearcoat, sheen, subsurface (wrap + thickness), anisotropy.

**Changes:** per-material constants in `GPUMaterialData`; evaluated in `DeferredLighting.glsl` via
the G-buffer `MaterialID` (per material in deferred), per-pixel in the transparent forward shader.
Each lobe behind a material flag so plain materials pay nothing. Spatial only — no temporal
accumulation.

**Verification:** car-paint (clearcoat), velvet (sheen), wax/skin (subsurface), brushed metal
(anisotropy) test materials; `/profile` shows no cost for materials without the flags.

### Phase 5 — Material instances

**Goal:** Unreal-style parent/child materials.

**Changes:** `.lmat` `Parent:` handle + override set; `MaterialAsset` resolves effective values
(child override, else parent chain); cycle detection on assign; `OnDependencyUpdated` propagates
parent edits; editor shows override markers, "Reset to parent", "Create Instance" in the Content
Browser and editor; `MaterialScene` upserts effective data.

**Verification:** parent colour change updates all children without overrides; overridden children
keep theirs; reparenting and deleting a parent are handled (children fall back and warn); undo.

### Phase 6 — Triplanar, world-space mapping and detail maps

**Goal:** the standard material's mapping options.

**Changes:** mapping mode (UV / triplanar world / triplanar object) with blend sharpness; detail
albedo/normal with mask and tiling. Shader branches behind flags.

**Verification:** triplanar rock on a scaled cube shows no stretching; detail maps visible up close.

### Phase 7 — Spike: graph materials in the GPU-driven path

**Goal:** prove a material can use its own shader and pipeline without breaking GPU-driven drawing.

**Changes (spike):** hand-write one variant of `GBuffer_Static.glsl` with a different surface block;
a material flag selects it; the G-buffer pass switches pipeline when `PipelineSortKey` changes; the
same for pre-depth and shadows when the variant uses cutout. Confirm `ShaderPack` export includes it.

**Measure:** pipeline switches per frame and cost with 1, 10, 50 distinct variants; correctness with
GPU culling on and off; hot reload of the variant.

**Exit:** go/no-go for the graph phases, with numbers.

### Phase 8 — Node editor foundation

**Goal:** an empty-but-usable graph editor for `.lmatgraph` assets.

**Changes**
- Vendor **imgui-node-editor** (MIT) under `Core/vendor/`, starting from the 1.92-patched copy in
  `M:\Git\Hazel`, verified against ImGui 1.92.6; add to `Dependencies.lua`.
- Graph model (**NEW** `Core/Source/Lux/Renderer/MaterialGraph/`): nodes, typed pins, links, stable
  IDs, validation; `.lmatgraph` asset — **playbook: Add a new asset type** (enum + string helpers in
  `AssetTypes.h`, `Asset` subclass, serializer + asset-pack serializer, `AssetImporter`,
  `AssetExtensions.h`).
- Canvas: Output node; **search palette** (right-click / Space, fuzzy — reuse the shared fuzzy
  matcher); **comment boxes** (node-editor groups); reroute nodes; box select; copy/paste; delete;
  undo; zoom-to-fit; **minimap** overlay drawn by us.
- Patterns ported from Hazel `NodeGraphEditor` (Apache-2.0 — keep the licence notice with any
  adapted file).

**Verification:** create, connect, comment, copy/paste, undo 50 steps, save, reopen identical;
ImGui check (pins and links keyed by stable IDs, never loop indices).

### Phase 9 — Graph compiles to a real shader

**Goal:** a graph drives a material in the scene.

**Changes:** codegen (type checking with implicit float→vecN, topological order, dead-node removal,
constant folding) into a surface function inserted into the G-buffer and transparent templates;
compile on the worker thread with shaderc using the engine's options; errors mapped to the failing
node (red outline + message); hash-keyed cache; register in `ShaderLibrary` so hot reload and
`ShaderPack` export work; graph materials go through the Phase 7 pipeline path.

**Verification:** a graph with base colour + roughness constants matches the equivalent standard
material pixel-for-pixel; an invalid connection shows a node error, never a crash; Dist export
renders the graph material.

### Phase 10 — The node library

**Goal:** the nodes on the user's list.

- **Inputs:** Texture Coordinate (UV0), Geometric Normal, World Normal, Position (object/world), View
  Direction, Time, Camera Position; Vertex Colour and UV1 after Phase 13.
- **Constants and parameters:** Float, Vector, Colour, Texture.
- **Math:** add, subtract, multiply, divide, power, abs, min, max, clamp, saturate, floor, fraction,
  sine, cosine, step, smoothstep, one-minus, remap.
- **Vector Math:** dot, cross, length, distance, normalize, reflect, split, combine.
- **Mix / Lerp** (colour and float), **Color Ramp** (baked into a small 1D texture per node).
- **Textures:** Sample Texture 2D (tiling/offset), Normal Map, Bump (from height), **Triplanar**
  sample.
- **Procedural:** Perlin/simplex gradient noise, **Voronoi** (F1, F2, edge), **Noise** with
  fBm / multifractal / ridged / hybrid (Blender 4.1+ Noise; Musgrave parameters as Roughness =
  Lacunarity^(−Dimension)). Written in-house in `LuxNoise.glslh` (ideas from published algorithms,
  no copied code).

**Verification:** a sample graph per node family; rust, rock and wood procedural materials built
from these nodes only.

### Phase 11 — Exposed parameters and graph instances

**Goal:** mark a node as a parameter; edit it per instance in the Lux inspector without reopening
the graph, with no shader recompile.

**Changes:** parameter blackboard in the graph; parameters stored in a per-material parameter block
(**NEW** storage buffer slice — grep the shader corpus for a free `(set, binding)` first); instances
(Phase 5) list graph parameters; the entity inspector shows them under the material slot.

**Verification:** 10 instances of one graph with different colours/speeds render in one scene with a
single compiled shader; changing a value never triggers a compile (log).

### Phase 12 — Organisation for big graphs

**Goal:** large graphs stay manageable.

**Changes:** **sub-graphs** (reusable function graphs as their own asset with inputs/outputs);
collapse selection into a sub-graph; search nodes *in* the graph (jump to match); comment colours;
node preview thumbnails (small per-node output preview); graph stats (texture samples, instruction
estimate).

**Verification:** a 150-node material stays responsive (`/profile` canvas cost); sub-graph edits
propagate to users.

### Phase 13 — Vertex colours and a second UV set

**Goal:** the Vertex Colour and UV1 input nodes.

**Changes:** extend `Mesh.h` `Vertex` (or a second vertex stream), `AssimpMeshImporter`, mesh
serializer and runtime mesh format (version bump), meshlet and skinned paths, every vertex layout
in the shader corpus. Large and cross-cutting — may become its own plan.

**Verification:** a vertex-painted mesh from Blender renders its colours; old meshes load unchanged.

### Phase 14 — Hardening

Dist/runtime export of graph materials and instances; Linux; the performance budget table; docs
(`Panels.md`, `Architecture § 2.3/2.8/2.9`, `Rendering.md`); a sample "Materials" showcase scene.

---

## Part 4 — Verification

- **Per phase:** checks above, ImGui check, `/cr`, Release + Debug build.
- **End to end (after Phase 11 and again after 14):** create a material, build a procedural rust
  graph with noise + Color Ramp + Mix, expose tint and roughness, make three instances, assign them
  in a scene, see correct thumbnails, export a Dist build, and see the same look in the exported game
  on Windows and Linux.
- **Compatibility:** every `.lmat` in `LuxSampleProject` renders identically before and after
  Phase 3 (screenshot diff).

## Part 5 — Risks

| Risk | Likelihood | Detection | Mitigation |
|---|---|---|---|
| Second `SceneRenderer` too heavy or shares broken static state | Medium | Phase 0 | Dedicated lightweight preview renderer |
| Per-material pipelines break GPU-driven batching or cost too much | Medium | Phase 7 | Limit graph variants per frame; bucket by pipeline; keep standard material as the fast path |
| imgui-node-editor needs more 1.92.6 patches than Hazel's copy has | Low–Medium | Phase 8 first build | Fall back to imnodes (MIT, has a minimap) |
| Growing `GPUMaterialData` costs bandwidth | Low | `/profile` in Phase 3 | Pack fields; keep the row ≤ 128 bytes |
| 1024 bindless texture limit hit by texture-heavy graphs | Medium | Phase 9 stats | Report per-material texture counts; raise the limit as a renderer change if needed |
| Parameter block needs a new `(set, binding)` | Certain | Phase 11 | Grep the corpus; follow `Rendering.md` Invariant 1 |
| Vertex format change ripples everywhere | High | Phase 13 | Isolate as its own plan if the scope grows |

## Part 6 — Open questions

1. Phase 0: second renderer vs dedicated preview renderer (spike decides).
2. What `MaterialFlag::TwoSided` does today (Phase 3 verifies).
3. Does `ThumbnailCache` need a GPU readback path to persist thumbnails (Phase 2)?
4. Should graph materials be allowed on skinned meshes from the start, or after hardening?
5. C# control of material parameters at run time (Unreal's dynamic instances) — follow-up plan?

## Part 7 — Research notes

### Research brief — material editor
**Goal (from the Goal card):** a real material editor — standard material, then node graph.

- **Prior art — standard material:** Godot's StandardMaterial3D groups albedo, ORM, metallic/roughness,
  emission, normal, AO, height/parallax, subsurface, back-lighting, rim, clearcoat, anisotropy,
  refraction, detail maps, UV1/UV2 triplanar, transparency modes and cull mode — the target list for
  Phases 3–6. [Godot docs](https://docs.godotengine.org/en/stable/tutorials/3d/standard_material_3d.html)
- **Prior art — instances:** Unreal material instances expose scalar/vector/texture parameters from a
  parent, override them per child without recompiling, and can chain. Phases 5 and 11.
  [Unreal docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/instanced-materials-in-unreal-engine)
- **Concept — layered model:** OpenPBR's base/specular/coat/fuzz/subsurface/emission layering is the
  vocabulary for the Phase 4 lobes (coat = clearcoat, fuzz = sheen).
  [OpenPBR spec](https://academysoftwarefoundation.github.io/OpenPBR/)
- **Prior art — graph compilation:** Unreal turns the material graph into HLSL and builds shader
  permutations per usage — the model for Phase 9's codegen and per-graph pipelines.
  [UE4 shader permutations](https://medium.com/@lordned/unreal-engine-4-rendering-part-5-shader-permutations-2b975e503dd4)
- **Pitfall — material graphs in GPU-driven renderers:** per-material shaders fragment batches;
  visibility-buffer engines bin pixels by material to recover. Phase 7 measures the cost before the
  graph is built. [Filmic Worlds](https://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/)
- **Concept — Blender noise:** Musgrave was merged into Noise in Blender 4.1 (fBm, multifractal,
  ridged, hybrid; Roughness = Lacunarity^(−Dimension)) — Phase 10's Noise node follows that.
  [Blender PR #111187](https://projects.blender.org/blender/blender/pulls/111187)
- **Library — imgui-node-editor** (thedmd, MIT): zoom, selection, groups, copy/paste, state saving;
  vanilla ImGui; the local Hazel copy already runs on ImGui 1.92.0.
  [repo](https://github.com/thedmd/imgui-node-editor)
- **Library — imnodes** (Nelarius, MIT): three files, built-in minimap, no comment groups; 1.92
  support unverified. Fallback. [repo](https://github.com/Nelarius/imnodes)
- **Rejected — VisualNodeSystem** (MIT; comments, reroutes, sub-areas): brings its own node model and
  a jsoncpp dependency; LuxEngine needs its own model for codegen and YAML assets.
  [repo](https://github.com/Azzinoth/VisualNodeSystem)
- **Rejected — ImNodeFlow** (MIT): retained, inheritance-based nodes; no minimap or comments in its
  feature list. [repo](https://github.com/Fattorino/ImNodeFlow)
- **Rejected — rokups/ImNodes:** unmaintained since 2022.
- **Rejected — temporal accumulation** for any preview or lobe (standing rule).

**Informs decisions:** node editor library, graph output, advanced lobes, instances.
**Still unknown:** preview renderer cost (Phase 0); graph pipeline cost (Phase 7).

### Phase 0 results (2026-09-18, Windows, Release, render thread on)

The spike was built directly as the reusable `MaterialPreview` (no throwaway branch).

| Question | Result | How it was checked |
|---|---|---|
| Does a second `SceneRenderer` render correctly next to the main viewport? | ✅ Yes — two at once (editor preview + thumbnailer) | Run: preview and a Content Browser thumbnail both rendered; the main viewport was unaffected |
| Shared static state that breaks (`MaterialScene`, binding registries, shadow atlas) | ✅ None seen | Live edits reached both the preview and the scene; no validation/log errors |
| Teardown | ✅ Clean editor shutdown with both previews alive | `CloseMainWindow`, no Application Error event |
| VRAM / CPU / GPU cost | ⚠️ **Not measured — waived by the user** | Only the baseline was captured (editor closed, uncapped, focused: ~297 fps, ~3.4 ms/frame). The with-preview capture was skipped; nothing noticeable in use |
| Scene switch while a preview is alive | ✅ Works | User-verified: switched scenes freely while editing a material |
| Project close while a preview is alive | ⚠️ Not exercised | `OnProjectChanged` drops both previews |
| Single-threaded render policy | ⚠️ Not exercised | — |

**Decision: go** — keep the second-`SceneRenderer` approach; revisit only if `/profile` shows a real cost.

Found on the way (fixed): `MaterialAsset` stored its values only in the shader push-constant block,
which the transparent shader does not have and the opaque one lacks `Transparency` for, so reading
them was an out-of-bounds read (crash on opening a material). The asset now owns its values
(`.claude/docs/Architecture-LuxEngine.md`, renderer section).

---

**First phase to implement:** Phase 0 — the preview-renderer spike. Implement with `/dev`, review
with `/cr`, ship with `/send-pr`.
