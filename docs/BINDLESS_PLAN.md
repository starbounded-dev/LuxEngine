# LuxEngine Full Bindless Plan

This plan takes LuxEngine from "material textures are bindless" to a renderer where every opaque and
shadow draw reaches its geometry, instance, material and textures by index. Each of those passes
becomes one GPU-driven multi-draw instead of a bind-and-draw per mesh. The remaining per-pass
textures move into one resource heap, and a mesh-shader G-buffer builds on the same data. Rendering
is the feature LuxEngine wants to be known for, and this is the data model that GPU-driven
rendering, mesh shaders and a future material graph all assume.

This is a planning document written on 2026-09-25. For what is actually built, see
`.claude/docs/Architecture-LuxEngine.md` (Renderer) and `.claude/docs/Rendering.md § Bindless
textures`.

**Goal card**

- **Goal:** "Full bindless". Opaque and shadow passes draw through one multi-draw each, reading
  geometry from a shared pool, per-draw data by `firstInstance`, and every texture from a bindless
  heap. Then the G-buffer moves to mesh shaders on GPUs that support them.
- **Success:**
  1. In `FMODDemo` and a generated benchmark scene, pre-depth, G-buffer and the directional/spot
     shadow passes each issue a handful of `drawIndexedIndirect` calls, not one per mesh.
  2. Measured CPU submission time for those passes drops against the Phase 0 baseline.
  3. No validation errors, on both the mutable-heap path and the per-kind fallback.
  4. Images are pixel-identical to the baseline.
  5. On the Windows mesh-shader GPU, the G-buffer runs on mesh shaders with the same image.
- **Non-goals:**
  - moving transparent, selection, wireframe or collider passes (they keep today's path)
  - a GPU-compacted draw count (a later phase)
  - `VK_EXT_descriptor_heap` or `VK_EXT_descriptor_buffer`
  - any temporal technique
  - skinned meshes (skeletal animation doesn't exist yet)
- **Constraints:**
  - Vulkan through NVRHI, which stays: the raw-Vulkan escape hatch is allowed, a backend swap is not.
  - Existing scenes, materials and exports still load.
  - Your Renoir laptop has no mesh shaders, so every phase except Phase 6 must work there.
  - Order of work: make it work, then good, then fast.
- **User decisions (2026-09-25):**
  - Scope is "everything bindless", including the mesh-shader G-buffer.
  - A shared geometry pool.
  - Opaque and shadow passes first.
  - The CPU decides the draw count first.
  - A mutable heap **with a per-kind fallback**.
  - Mesh shaders are tested on your Windows PC.

**Decisions this plan is built on**

| Decision | Choice | Consequence |
|---|---|---|
| Geometry access | **Shared geometry pool** (user). Static meshes suballocate from pooled vertex and index buffers; draws use `vertexOffset`/`firstIndex` | Keeps fixed-function vertex input and the GPU's post-transform cache. Needs a pool allocator. |
| Per-draw data | `firstInstance` = today's `ObjectIndexBase` (`gl_InstanceIndex` already includes it) | Removes the per-draw push constant. Per-pass constants (for example `Cascade`) stay push constants. Needs `drawIndirectFirstInstance`, which your GPU has. |
| Draw count | **CPU-known count** through NVRHI's `drawIndexedIndirect(offset, drawCount)` (user) | Needs the `multiDrawIndirect` device feature, which isn't enabled today. Culled draws keep zero instances, as they do now. |
| Pool growth | **Fixed-size blocks** (for example 64 MiB vertex and 32 MiB index); a pass issues one multi-draw per block | No reallocation or GPU copy while frames are in flight. A new block only adds a draw group. |
| Heap model | **Mutable resource heap** at set 4 when `VK_EXT_mutable_descriptor_type` exists; **per-kind tables at sets 4–7** otherwise (user) | One slot index space in both paths. GLSL declarations are chosen by a global shader macro. The fallback can be forced to test it on the laptop. |
| Kinds in the heap | Texture2D, TextureCube, Texture2DArray, Texture3D | Four kinds fit the fallback's four sets. Buffers use device addresses instead, needing no set. |
| Buffers | **Buffer device addresses** (NVRHI adds the usage flag automatically when the extension is on; `IBuffer::getGpuVirtualAddress`) | No buffer descriptors at all; the mesh-shader path reads meshlets through addresses. Needs `shaderInt64` and `GL_EXT_buffer_reference`. |
| Render targets in the heap | Explicit `setTextureState(ShaderResource)` + `commitBarriers` before the passes that sample them, and back afterwards | NVRHI doesn't track resources reached only through descriptor tables (see Risks). Material textures are already permanently in shader-resource state (`Image.cpp:190`). |

---

## Part 0 — Where we are (verified 2026-09-25)

| Capability | State | Evidence |
|---|---|---|
| Bindless material textures | ✅ Built. Rendered `FMODDemo` in Debug with validation and no errors. ⚠️ No one has looked at the image yet, and it isn't run on Windows. | `ef645e9`; `Renderer/BindlessTextureTable.{h,cpp}`; set 4, `u_GPUMaterialTextures[]` |
| Instance, object-index and material tables | ✅ Storage buffers, indexed by shaders | `GPUSceneInstances`, `ObjectIndexes`, `GPUMaterials` (set 2, binding 7) |
| Per-draw state | ⚠️ Each mesh binds its own VB/IB, a `ObjectIndexBase` push constant, and an indirect draw with count 1 | `SceneRenderer::RT_DrawStaticMesh` (≈`SceneRenderer.cpp:8655`–8740) |
| Legacy material set 0 per draw | ⚠️ Bound per draw, but `GBuffer_Static.glsl` declares no set 0 | `RT_BindMaterialDescriptorSet` in `RT_DrawStaticMesh`; grep of `set = 0` |
| GPU culling | ✅ Writes visible instance counts into indirect args | `MeshCulling.glsl:186` (`atomicAdd` on `b_IndirectDrawCommands`); `m_SBSIndirectDrawCommands` (4096 args) |
| Mesh buffers | ⚠️ One VB and one IB per `MeshSource`; `Vertex` = pos, normal, tangent, binormal, uv | `Mesh.cpp:536-537`; `Mesh.h:20` |
| Meshlets | ⚠️ Pre-depth only; a per-mesh binding set of 4 buffers | `MeshSource::RT_GetOrCreateMeshletBindingSet` (`Mesh.cpp:548`); `SceneRenderer.cpp:7290` |
| Device features | `multiDrawIndirect` ❌ not enabled; buffer device address ✅; descriptor indexing and partially bound ✅; mutable descriptors ❌ not requested | `VulkanDeviceManager.cpp:544-565` |
| Your laptop | multiDrawIndirect, drawIndirectCount, firstInstance, shaderInt64, BDA, mutable descriptors ✅; **mesh shaders ❌**; 32 descriptor sets | `vulkaninfo` (RADV Renoir) |
| NVRHI (June 2026) | multi-draw ✅; bindless layouts ✅ (immutable and mutable); **no draw-indirect-count** | `nvrhi.h:3321`; `vulkan-resource-bindings.cpp:112-163`, 878-894 |
| Bindless write semantics | ⚠️ An immutable layout writes *every* register space of the matching type | `writeDescriptorTable`, `vulkan-resource-bindings.cpp:886-893`. This is why 2D and cube can't share an immutable table. |
| Global shader macros | ✅ Exist | `Renderer::SetGlobalMacroInShaders` (`Renderer.cpp:2141`) |
| Benchmark scenes | ⚠️ Generator exists; no scenes generated | `scripts/GenerateBenchmarkScenes.py` |

## Part 1 — Goals and non-goals

See the Goal card. The order is deliberate: measure, then the heap (the shape everything else binds
to), then geometry, then single multi-draws, then shadows, then the remaining textures, then mesh
shaders. Every phase leaves the engine shippable, and the laptop can run everything but Phase 6.

## Part 2 — Design

```
Renderer (process-wide)
 ├─ BindlessResourceHeap layout   set 4 mutable heap │ fallback: sets 4-7 per kind   (NEW, grows from
 │                                                                                     BindlessTextureTable)
 └─ GeometryPool                  64 MiB VB blocks + 32 MiB IB blocks, free-list      (NEW)
SceneRenderer (per instance)
 ├─ heap tables × FramesInFlight  (own slot space; render-thread writes, as today)
 └─ per pass: indirect args grouped by pool block → drawIndexedIndirect(offset, count) per block
Shaders
 ├─ gl_InstanceIndex (= firstInstance + i) → ObjectIndexes → GPUSceneInstances → GPUMaterials
 ├─ textures: LUX_BINDLESS_MUTABLE ? aliased arrays at (4,0) : one array per set 4..7
 └─ buffers: GL_EXT_buffer_reference addresses (mesh-shader path)
```

**Threads.**
- **Asset worker:** decodes meshes. Suballocation happens when a mesh first syncs to the main
  thread, under a pool mutex. The upload goes through the existing batched upload path
  (`Rendering.md § Batched resource uploads`) into the block, on the render thread.
- **Render thread:** heap and table writes, as in `BindlessTextureTable` today.
- **Main thread:** all ECS work.
- **Both threading policies:** correct under each, because every GPU write goes through
  `Renderer::Submit`.

**Ownership.**
- **Pool ranges:** each `MeshSource` owns its range in the pool. Freeing a range is deferred with
  `Renderer::SubmitResourceFree`, so in-flight frames finish first.
- **Blocks:** live until shutdown.
- **Heap slots:** owned per `SceneRenderer`, as today.

**Compatibility.**
- Scene, material and project formats don't change.
- Exported games must be re-exported: the shader pack changes (set 4 layout and the new macros).

## Part 3 — Phases

Every phase:
- builds Core and Editor in Debug and Release, plus `Lux-Runtime`
- regenerates projects when it adds files
- clears `Editor/Resources/Cache/Shader` when it changes a shared GLSL header
- runs the Debug editor on `FMODDemo` under validation (expected: no validation errors, no
  "no table" reports)
- ends with `/cr`
- is committed on its own

### Phase 0 — Baseline and correctness check (no engine changes)

**Goal:** numbers to beat, and proof that `ef645e9` renders correctly.

**Work**
- Generate the benchmark scenes with `scripts/GenerateBenchmarkScenes.py`.
- `/profile` with a fixed protocol on `FMODDemo` and the largest benchmark scene: fixed camera,
  VSync off, Release. Record:
  - CPU time of the pre-depth, G-buffer and shadow passes' recording, from Tracy zones in
    `SceneRenderer`
  - the number of draws and binding commits per pass (`Renderer Debugger`, `RenderStatsPanel`)
  - GPU time per pass
- Screenshot reference images.
- Check that `FMODDemo` looks the same before and after `ef645e9`, on the laptop and on Windows.

**Exit:** a baseline table appended to this document, and reference images committed next to it.

### Phase 1 — One resource heap, with the per-kind fallback

**Goal:** `BindlessTextureTable` becomes a heap that can hold 2D, cube, 2D-array and 3D textures in
one index space, on both paths. Material textures keep working unchanged.

**Changes**
- `VulkanDeviceManager.cpp`: request `VK_EXT_mutable_descriptor_type` and its feature when
  present, and log the chosen path.
- `Renderer/BindlessTextureTable.{h,cpp}`, renamed or grown into **NEW** `BindlessResourceHeap`.
  One slot allocator; `SetSlot(slot, texture, kind)`.
  - **Mutable path:** an NVRHI `LayoutType::MutableSrvUavCbv` layout, and writes carry the item's
    `dimension`.
  - **Fallback path:** four immutable layouts at sets 4–7, one per kind, sharing the slot numbers.
  - **Forced fallback:** `LUX_BINDLESS_FALLBACK=1` for testing.
- `VulkanShader::CreateDescriptors` substitutes the right layout for sets 4–7.
  `RenderPass::GetBindingSets` places one table (mutable) or four (fallback).
- `Include/GLSL/MaterialScene.glslh`, or a **NEW** `Bindless.glslh`: the declarations.
  - `LUX_BINDLESS_MUTABLE`: aliased `texture2D`/`textureCube`/`texture2DArray`/`texture3D` arrays at
    `(4, 0)`.
  - Otherwise: one array per set 4–7.
  - The macro is set through `Renderer::SetGlobalMacroInShaders` before any shader loads.

**Verification**
- Validation-clean `FMODDemo` on both the mutable path and the forced fallback on the laptop.
- Material images match Phase 0.

**Docs**
- `Rendering.md § Bindless textures` becomes § Bindless resource heap, and sets 4–7 are reserved in
  the fallback.
- Architecture renderer section.
- `Building.md` gets the env var.

**Rollback:** revert to the `BindlessTextureTable` from `ef645e9`; the shader macros default to it.

### Phase 2 — Shared geometry pool

**Goal:** every static mesh's vertices and indices live in pooled blocks. Draws still go through
today's per-mesh path, but bind the block and use `vertexOffset`/`firstIndex`.

**Changes**
- **NEW** `Renderer/GeometryPool.{h,cpp}`:
  - fixed blocks (vertex 64 MiB, index 32 MiB; tuned from Phase 0 numbers)
  - a free-list suballocator per block, under a mutex
  - `Allocate(vertexBytes, indexBytes) → {block, vertexOffset, firstIndex}`
  - deferred `Free`
- `MeshSource` (`Mesh.cpp:536`): allocate a range and upload into it instead of creating its own
  VB and IB. Keep its own buffers for meshlets and any consumer not yet migrated (listed in the
  phase).
- `SceneRenderer::RT_DrawStaticMesh`: bind the block's buffers, and set the offsets in the draw and
  indirect arguments.

**Verification**
- Images identical to Phase 0.
- Load, unload and reload a scene repeatedly: pool usage returns to baseline, with no validation
  errors (catches free-while-in-flight).
- Hot-reload a mesh.

**Docs:** Architecture renderer (the pool, and who owns ranges), `Rendering.md` (buffer rules).

### Phase 3 — One multi-draw per pass per block (opaque)

**Goal:** pre-depth and G-buffer issue `drawIndexedIndirect(offset, count)` once per pool block.

**Changes**
- `VulkanDeviceManager.cpp`: enable `multiDrawIndirect` and `drawIndirectFirstInstance`.
- `SceneRenderer`: build the indirect arguments contiguously per pass and block, with
  `firstInstance` = object index base, instead of one draw per `MeshKey`.
- `PreDepth.glsl`, `GBuffer_Static.glsl`: drop `ObjectIndexBase`; `objectIndex = gl_InstanceIndex`.
- Stop binding legacy set 0 for passes whose shader doesn't declare it.
- GPU culling (`MeshCulling.glsl`) keeps writing instance counts into the same arguments.

**Verification**
- Images identical to Phase 0.
- Draw calls per pass fall to about the number of pool blocks.
- `/profile` CPU submission time on the benchmark scene against the Phase 0 table.

**Docs:** `Rendering.md` (per-draw data through `firstInstance`; the multi-draw rule).

### Phase 4 — Shadows on the same path

**Goal:** the directional (per cascade) and spot shadow passes use the same multi-draw.

**Changes**
- `DirShadowMap.glsl` and `SpotShadowMap.glsl` read the object index from `gl_InstanceIndex`;
  `Cascade` stays a per-pass push constant.
- `SceneRenderer`'s shadow submission builds arguments per cascade or light and block.

**Verification:** identical shadows; draw and CPU numbers as in Phase 3.

### Phase 5 — Everything else through the heap

**Goal:** the textures opaque and shadow passes still bind per pass (environment radiance and
irradiance cubes, the BRDF LUT, shadow maps, noise volumes) are heap slots, reached by index from
`RendererData` or `SceneData`.

**Changes**
- Heap slots for those textures, with indices added to the relevant UBOs. Edit the C++ `UB*`
  structs and their GLSL blocks together (`Rendering.md` must-fix).
- **The render-target rule:**
  - Shadow maps and other attachments aren't tracked by NVRHI when reached only through the heap,
    so the passes that sample them call `setTextureState(ShaderResource)` + `commitBarriers` first,
    and restore the attachment state afterwards.
  - Use the existing command-list access; NVRHI's `setTextureState` is public.

**Verification:** identical images; no validation layout errors, with the shadow map sampled from
the heap.

**Docs:** the render-target rule in `Rendering.md`.

### Phase 6 — Mesh-shader G-buffer (Windows mesh-shader GPU)

**Goal:** on GPUs with `VK_EXT_mesh_shader`, the G-buffer is drawn by task and mesh shaders reading
meshlets through buffer device addresses and materials through the heap. Other GPUs keep Phase 3's
path.

**Changes**
- **NEW** `GBuffer_Meshlet.glsl` (with `GL_EXT_buffer_reference` and `shaderInt64`).
- Meshlet buffers suballocated from the pool.
- Meshlets addressed per instance through a GPU address table instead of
  `RT_GetOrCreateMeshletBindingSet`.
- `SceneRenderer` chooses the path by `Renderer::SupportsMeshShaders()`.
- Pre-depth meshlets move to the same addressing.

**Verification:**
- **Windows:** identical G-buffer against Phase 3, validation clean.
- **Laptop:** unchanged, because the path is disabled there.
- `/profile` GPU time for the G-buffer on Windows, both paths.

**Docs:** Architecture renderer section; `Rendering.md` (the mesh-shader rules).

## Part 4 — Verification

Each phase compares against Phase 0's reference images and numbers. The whole feature is proven
when all of these hold:
- The benchmark scene on the laptop shows the draw and CPU drop from Phases 3 and 4.
- Both heap paths are validation-clean.
- The Windows mesh-shader path matches the traditional path.

## Part 5 — Risks

| Risk | Likelihood | Detection |
|---|---|---|
| NVRHI doesn't track state for resources only reachable through descriptor tables | Certain for render targets | Phase 5 rule. Material textures are safe (`keepInitialState`). Validation on image layouts. |
| Mutable-heap writes need the right `dimension` / view type per kind | Medium | Phase 1 tests every kind on both paths. |
| Pool fragmentation with heavy streaming | Medium | Phase 2 load/unload loop logs pool usage; blocks cap the damage. |
| Old exports with the previous shader pack | Certain | Re-export note in the release notes. |
| Mesh shaders untestable on the laptop | Certain | Phase 6 is last, gated by capability, verified on Windows only. |
| CPU savings smaller than expected on small scenes | Medium | Phase 0 numbers decide whether Phase 3 onward is worth it before building on it. |

## Part 6 — Open questions

- Final block sizes: settled by Phase 0's measured mesh memory.
- Whether the thumbnailer and material preview `SceneRenderer`s should share pool blocks (they do:
  the pool is process-wide) and heap tables (they don't: separate index spaces). Confirm no
  per-renderer assumption in `MeshSource`.
- Which GPU the Windows PC has, for Phase 6's expected numbers.

**What would invalidate the plan:** if Phase 0 shows mesh submission isn't a meaningful part of
frame time, Phases 3–4 lose their performance motivation. Stop and decide with the user whether
bindless is still wanted for the mesh-shader and material-graph future alone.

## Part 7 — Research notes

**Goal:** every opaque and shadow draw reachable by index; one multi-draw per pass.

- **Concept:** a shared VB/IB plus multi-draw indirect with `firstInstance` as the per-draw index is
  the standard GPU-driven layout.
  [Khronos multi-draw indirect sample](https://docs.vulkan.org/samples/latest/samples/performance/multi_draw_indirect/README.html) ·
  [Vulkan Guide: GPU-driven engines](https://vkguide.dev/docs/gpudriven/gpu_driven_engines/)
- **Concept:** `VK_EXT_mutable_descriptor_type` exists to give Vulkan a single D3D12-style resource
  heap instead of parallel per-type arrays.
  [Khronos proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_mutable_descriptor_type.html)
- **Prior art:** Wicked Engine went bindless on Vulkan and DX12, and uses buffer device addresses
  in place of buffer descriptors, citing Vulkan's buffer-descriptor limits.
  [Wicked Engine: bindless descriptors](https://wickedengine.net/2021/04/bindless-descriptors/comment-page-1/)
- **Pitfall:** NVRHI's immutable bindless layouts write every register space of the matching type
  (read in the vendored source), so parallel 2D and cube arrays can't share one immutable layout.
  Hence the fallback puts one kind per set.
- **Rejected:** vertex pulling for all geometry. It loses the post-transform cache on indexed
  meshes, and the user chose the pool.
- **Deferred:** `VK_EXT_descriptor_heap` (NVIDIA drivers 610+). Too new, and NVRHI doesn't support
  it. [NVIDIA blog](https://developer.nvidia.com/blog/streamlining-resource-binding-with-end-to-end-support-for-vulkan-descriptor-heaps/)
- **Rejected:** any temporal technique (engine rule).

**Still unknown:** mutable-descriptor behaviour on the Windows PC's driver; checked in Phase 1 on
Windows.
