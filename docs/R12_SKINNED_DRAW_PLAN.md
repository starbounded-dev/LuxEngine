# LuxEngine R12 Plan: Skinned Mesh Draw Path

This is a planning document, not architecture: what exists is described in
`.claude/docs/Architecture-LuxEngine.md` (see the skeleton and skin-data bullets under *Asset
system*). It plans stage 2 of issue #58 (R12, "Skinned mesh passes", part of M2 skeletal
animation) on PR #160. Stage 1 (skin weights and inverse bind poses on `MeshSource`, mesh pack
format v2) is committed at `58a38631`.

**Goal card**
- **Goal:** an entity with a rigged mesh and a bone pose renders deformed, correctly lit and shadowed,
  through the engine's existing passes, in the editor and in an exported game.
- **Success:** (1) a skinned entity at rest pose renders pixel-equivalent to the same mesh drawn
  static; (2) a posed bone visibly deforms it in depth, G-buffer, directional/spot shadows and the
  selection outline; (3) the existing golden scenes (reference `lm8`) do not change; (4) zero cost
  when no skinned entity exists; (5) no validation errors.
- **Non-goals:** playback, blending, graph, root motion, ACL compression (A2/A3); animation events
  (A4); C# API (A5); editor animation tools (E8); morph targets; dual-quaternion skinning; cloth;
  GPU-driven culling of skinned meshes; ray tracing; the meshlet (mesh-shader) path for skinned meshes.
- **Constraints:** Windows first (Linux deferred, per the Windows-only rule); no temporal techniques;
  no new installs; old scenes and projects must still load; pack format v2 is already in.
- **User's words:** "Just start building" (stage 1); asked for a plan first at the point the draw path
  turned out to touch the core submit code.

## Decisions this plan is built on

| Decision | Choice | Consequence |
|---|---|---|
| Where skinning runs | One compute pass writes a deformed copy of the mesh's vertex buffer (the skin-cache design); every raster pass then draws it unchanged | Reuses every pipeline and shader; skinning math runs once per entity per frame, not once per pass. [UE skin cache](https://forums.unrealengine.com/t/what-is-the-purpose-of-the-gpu-skin-cache/370993) |
| Output format | The same 28-byte `PackedVertex` layout (position, QTangent, UV) | Index buffers, LODs, `draw.VertexOffset`, pipelines and attribute stream stay as they are; needs a GLSL QTangent **encode** (NEW) |
| Skinned entities and the culling machinery | Skinned submeshes never join GPU cull groups or the static shadow cache; they take the existing CPU bucket path (`!gpuCullGroup`) with a key unique to the entity | No change to `AddToCullGroup`, indirect args or shadow caches; skinned meshes always count as moving casters |
| Node transform of a skinned submesh | Ignored; the GPU scene row is the entity's world transform only (`RebuildStaticMeshGPUInstances` skips `submesh.Transform` for rigged submeshes) | Matches the glTF note that skinned meshes ignore their node transform. [glTF tutorial](https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_020_Skins.html) |
| Skinning matrix and math | `palette[i] = boneModelPose[skeletonBone] * InverseBindPose[i]`; linear blend of up to four palette matrices; normals and tangents through the blended 3x3 | Exact for rotation and uniform scale; non-uniform bone scale skews normals slightly (recorded as a known limit) |
| Render-thread work | Palette upload and the skinning dispatch run on the render thread inside a RenderGraph pass | Correct under both threading policies; GPU buffers freed through `Renderer::SubmitResourceFree` |
| Component shape | **User decision D1** (below) | |
| Pose source before A2 | **User decision D2** | |
| Golden/runtime launches | **User decision D3** | |

## Part 0 — Where we are (ledger)

| Capability | State | Evidence |
|---|---|---|
| Skeleton and clip assets, import, bake | ✅ Working (headless test; editor run on Release) | `Core/Source/Lux/Animation/*`, `Asset/SkeletalImporter.cpp`, `tests/animation/run_windows.py` |
| Per-vertex bone weights and inverse bind poses on `MeshSource` | ⚠️ Built, headless-verified, not uploaded | `Mesh.h` `GetBoneInfluences/GetBoneInfo`; `ExtractSkinWeights`; test checks weights and the rest-pose matrix |
| Mesh pack carries skin data | ⚠️ Compiled, never read back | `MeshRuntimeSerializer.cpp` skin section, `MeshSourceFile.h` `HasSkin` |
| Static draws read vertices from the mesh's buffer | ✅ | `SceneRenderer::RT_DrawStaticMesh`: `draw.Vertices = meshSource->GetVertexBuffer()`, `VertexOffset = lod.BaseVertex` |
| Draw shaders take the vertex layout (location 0-4) and fetch the transform by object index | ✅ | `Editor/Resources/Shaders/GBuffer_Static.glsl` (`GetInstanceTransform(ObjectIndexBase + gl_InstanceIndex)`) |
| A CPU bucket path exists beside GPU cull groups | ✅ | `SubmitStaticMeshInternal`: `gpuCullGroup` false uses `GetMeshTransformBucket`, transient GPU scene instances |
| Proxies build GPU scene rows as `World * submesh.Transform` | ✅ | `RenderScene.cpp` `RebuildStaticMeshGPUInstances` |
| Scene feeds proxies from `StaticMeshComponent` | ✅ | `Scene.cpp` `SyncRenderScene`; `StaticMeshSyncInputs` |
| `MeshComponent` / `SubmeshComponent` (Hazel leftovers) are rendered | ❌ Not submitted anywhere in the renderer | grep: only `StaticMeshComponent` reaches `SyncStaticMesh` |
| A buffer that is both a vertex buffer and compute-writable | ❌ Missing | `RHIBufferDesc` has `IsVertexBuffer` and `CanHaveUAVs`; `VertexBuffer` sets only the first; `StorageBuffer` has no vertex flag |
| Skinning compute shader, palette buffer, skin cache | ❌ Missing | no `Skinning.glsl` |
| A headless GPU test pattern | ✅ Exists | `tests/audio/SoundwaveGpuParity.cpp` (raw Vulkan, shaders vs CPU reference) |
| Golden capture harness | ✅ Exists | `Editor/Source/Tools/GoldenCapture.*`, `tests/rendering/golden_run.py`, reference `lm8` |

## Part 1 — Design

- **Component (D1):** an entity keeps its `StaticMeshComponent` and gains a `SkeletonComponent`
  (**NEW**, `Scene/Components.h`: skeleton asset handle, runtime pose). The proxy sees both and
  becomes skinned. Five edits: declaration, copy/duplicate/prefab, serialize and deserialize (optional
  component: old scenes load unchanged), `SceneHierarchyPanel` UI, and the pose into the frame
  packet and `StaticMeshSyncInputs`.
- **Pose into the proxy:** `StaticMeshSyncInputs` gains a pointer to the entity's palette (model-space
  matrices already multiplied by inverse binds, built on the main thread). The proxy compares a
  hash, bumps `Revision` when it changes, and the renderer copies it. A moving pose dirties the
  proxy each frame, which is acceptable (it also drops that proxy's `ProxySubmitCache` entry).
- **GPU resources (NEW, `SkinCache` owned by `SceneRenderer`):** per mesh, a read-only influence
  buffer (32 bytes per vertex) uploaded once; per skinned proxy, a skinned vertex buffer of the mesh's
  vertex count (`IsVertexBuffer` + `CanHaveUAVs`); one per-frame palette buffer holding every skinned
  proxy's matrices at offsets. Freed through `SubmitResourceFree` when the proxy or mesh goes, and
  all of it is dropped (after draining the GPU) on scene switch.
- **Skinning pass:** a RenderGraph compute pass before the depth prepass, feature-gated (not added when
  no skinned proxy is visible). One dispatch per skinned submesh range; reads the mesh vertex
  buffer, influences and palette; writes the skinned buffer; buffer state moves UAV write to vertex
  read with accurate `Reads`/`Writes`.
- **Draws:** `StaticDrawCommand` gains an optional skinned vertex buffer; `RT_DrawStaticMesh` binds it
  instead of the mesh's. The `MeshKey` for a skinned submesh carries a per-entity handle so a bucket
  is one instance. `MeshDrawCommandCacheKey` includes the skinned buffer so a cached command never
  holds a stale buffer.
- **Bounds:** the world bounds sphere of a skinned proxy is the mesh bounds scaled by a conservative
  factor chosen in Phase 3 (shortcut: no per-pose bounds; upgrade when a character visibly pops out).

**Fit table**

| Concern | Answer |
|---|---|
| System / dependencies | Renderer + Scene + Animation; Renderer already depends on Asset and Scene packets (Architecture Part 1) |
| Thread | Palette and component on main; upload and dispatch in render-graph lambdas on the render thread (both policies); no ECS reads at submission |
| Ownership | `Ref<>` for buffers, freed via `Renderer::SubmitResourceFree`; scene-switch teardown drains the GPU first (no dedicated helper exists, so Phase 3 names the mechanism: the existing drain in the scene-switch path) |
| Renderer invariants | New SSBO `(set, binding)` slots are grepped across `Editor/Resources/Shaders/` before use; pipeline built once in `Init()`; pass folded into `ComputeStructureHash()`; zero cost when off |
| Serialization | `SkeletonComponent` serialized only when present; old scenes unaffected; pack v2 already in |
| Editor | Inspector section with ImGui scope/ID verification; undo through the existing component edit path |
| Scripting | Not in this plan (A5) |
| Runtime / Dist / export | The skin section, a skeleton in the pack and the new shader in the shader pack are exercised in Phase 4; the pack collector is A2's (issue #66) |
| Linux | GLSL and portable C++; deferred with the Windows-only rule |
| Build | New files need `scripts\Win-GenProjects.bat`; no new dependency |
| Docs | `Rendering.md` (the no-skinned-variants statement becomes the skin-cache description), `Architecture-LuxEngine.md` |

## Part 2 — Phases

### Phase 1 — Pose data and component, no rendering change
**Goal:** a `SkeletonComponent` exists, saves and loads, and a pure function turns a skeleton, a mesh's
bones and a bone pose into a palette.
**Changes:** `Scene/Components.h` `SkeletonComponent` (**NEW**); `Animation/SkinPalette.{h,cpp}`
(**NEW**: `BuildSkinPalette`); `Scene/Scene.cpp` copy/serialize; `SceneHierarchyPanel.cpp` UI;
`StaticMeshSyncInputs` palette field (unused yet).
**Playbook:** new component, all eight steps in Architecture Part 4 (steps 5-7 noted unused).
**Thread:** main only.
**Verification:** build Debug; `tests/animation` gains a palette test: at rest `palette[i]` equals the
authored root transform for the sample rig; a rotated bone moves a child bone's palette by that
rotation; a scene with the component round-trips. Editor: add the component in the inspector, save
and reopen a scene (needs an editor launch, D3).
**Exit:** old scenes load; the new component survives save, load, duplicate and Play.
**Rollback:** revert the commit; no renderer state exists yet.

### Phase 2 — Skinning kernel and GPU buffers in isolation
**Goal:** the compute shader skins a mesh on the GPU and matches the CPU reference.
**Changes:** `Editor/Resources/Shaders/Skinning.glsl` (**NEW**, includes `QTangent.glslh` plus a
QTangent encode); `Renderer/SkinCache.{h,cpp}` (**NEW**: buffers, upload); a vertex+UAV buffer class
or flag (**NEW**); `tests/animation/SkinningGpuParity.cpp` (**NEW**, raw-Vulkan harness after
`SoundwaveGpuParity.cpp`).
**Playbook:** shader buffer (check `(set, binding)` collisions).
**Verification:** parity test on the sample rig and a synthetic mesh, posed and at rest, within a
stated tolerance for positions, normals and tangents; Debug build; nothing is wired into the
frame, so goldens are unaffected.
**Exit:** parity passes; shader compiles through the engine's shader compiler too.
**Rollback:** revert; the shader is unreferenced.

### Phase 3 — Skinned draws through the retained path
**Goal:** a skinned entity renders in the editor, in all passes.
**Changes:** `RenderScene.cpp` (proxy skin state; rows skip `submesh.Transform` for rigged submeshes);
`SceneRenderer.cpp/.h` (skinning RenderGraph pass; `gpuCullGroup` forced off for skinned;
per-entity `MeshKey` handle; `StaticDrawCommand` skinned buffer; `RT_DrawStaticMesh` override;
`MeshDrawCommandCacheKey` includes it; `ComputeStructureHash`); `FrameRenderPacket.h` palette.
**Playbook:** new render pass (Rendering.md § Adding a pass: all steps).
**Thread and lifetime:** dispatch on the render thread; buffers freed through `SubmitResourceFree`;
scene switch drains first.
**Verification:** (1) golden scene `RigRest`: skinned at rest versus the same mesh static, compared with
`golden_compare.py` within a stated tolerance; (2) `RigPosed`: a bone rotated, reviewed by you;
(3) every existing golden scene unchanged against `lm8`; (4) Vulkan validation clean in both
threading modes; (5) `/profile` shows zero cost with no skinned entity.
**Exit:** all five hold.
**Rollback:** the pass is feature-gated; reverting leaves static rendering untouched.

### Phase 4 — Breadth and hardening
**Goal:** the edges.
**Changes:** transparent pass, selection outline and wireframe for skinned; LOD selection on a skinned
submesh; bounds factor; mesh-shader prepass disabled for skinned; inspector polish; export check
(pack skin section read back, skeleton shipped, `Skinning.glsl` in the shader pack); Dist
verification; `Rendering.md` and `Architecture-LuxEngine.md`.
**Verification:** exported game renders the rig (Debug/Release/Dist as practical); `/profile` numbers
for N skinned entities recorded; Linux explicitly deferred.
**Exit:** export and Dist render a skinned entity; docs updated.

## Part 3 — Risks and open questions

**Risks**
- *Regression of static scenes* (medium likelihood: Phase 3 edits the central submit). Detected by the
  goldens in Phase 3; the changes stay behind skinned-only branches.
- *Per-frame proxy dirtying* of animated entities invalidates caches each frame (low impact at
  character counts; measure in Phase 4).
- *Shadow cost*: skinned casters take the CPU shadow path every frame; fine for a few characters.
- *Buffer state transitions* (UAV write to vertex read) are easy to get wrong; Phase 2's parity test
  and Phase 3's validation run catch it.

**Open questions (each has how to resolve)**
- GLSL QTangent encode: the CPU encode lives in `PackedVertex(const Vertex&)` (`Mesh.cpp`); port and
  test in Phase 2.
- Which `(set, binding)` slots are free: grep in Phase 2.
- Whether the shader pack picks up a new `.glsl` automatically: read `ShaderPack` in Phase 4 (the
  Dist shader pack is recorded as untested).
- Exact scene-switch drain mechanism: read the scene-switch path at the start of Phase 3.

**What would invalidate the plan:** if `RT_DrawStaticMesh`-style draws turn out to read vertex data
anywhere besides the bound vertex buffer (for example a compute stage or the meshlet path for the
main pass), the "swap the buffer" premise fails and Phase 3 changes shape.

## Part 4 — Verification of the whole

`RigRest` equals static; `RigPosed` deforms and shadows correctly; goldens unchanged; exported game
renders the rig; zero cost with none.

## Part 5 — Research notes

- **Skin cache:** Unreal skins positions and tangents in a compute shader, caches the result in
  vertex buffers and has the early-Z, shadow, base and custom-depth passes read it through a
  pass-through vertex factory, so skinning runs once instead of once per pass. [source](https://forums.unrealengine.com/t/what-is-the-purpose-of-the-gpu-skin-cache/370993)
  The benefit is removing repeated work across passes, not cheaper math. Informs the first decision.
- **glTF skinning:** `jointMatrix = globalJoint * inverseBind`; the skinned mesh's own node transform
  is ignored (some sources include the mesh node's inverse to cancel it). [tutorial](https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_020_Skins.html), [Vulkan compute skinning math](https://docs.vulkan.org/tutorial/latest/Advanced_glTF/Skeletal_Compute_Skinning/02_skinning_math.html).
  Stage 1's test already confirms this convention on the sample (rest skinning equals the authored
  root transform).
- **Rejected:** vertex-shader skinning in every pass. It would need skinned variants of the depth,
  masked, shadow, G-buffer, selection and wireframe pipelines and redo the skinning per pass.
- **Not found:** a detailed write-up of Unreal's shadow-depth scheduling; the pass-reuse claim comes from
  engine comments and forum posts.
