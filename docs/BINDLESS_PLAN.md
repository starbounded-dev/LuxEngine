# LuxEngine Full Bindless Plan

Rebuilt on 2026-11-09 from `feature/native-audio-system` at `87462d63`, replacing the 2026-09-25
version. That version was written for NVRHI and never started. This version is grounded in today's
renderer: Lux's `Renderer/RHI` layer over NRI. It is a planning document, not a description of what
exists. `.claude/docs/Architecture-LuxEngine.md` (Renderer) and `.claude/docs/Rendering.md`
(§ Bindless textures) describe what is built.

The plan takes the renderer from "material textures are bindless" to "every opaque and shadow draw
reaches its geometry, instance, material and textures by index". Each of those passes becomes a
handful of multi-draws instead of a bind-and-draw per mesh. All textures then sit in one directly
indexed descriptor heap, and a mesh-shader G-buffer builds on the same data.

## Goal card

- **Goal:** "full bindless". Pre-depth, G-buffer and shadow passes draw through a few multi-draws
  each, reading geometry from a shared pool, per-draw data through `firstInstance`, and every
  texture from one heap. Then the G-buffer moves to mesh shaders where the GPU supports them.
- **Success:**
  1. On `Benchmark` and `AudioDemo`, each converted pass issues one `CmdDrawIndexedIndirect` per
     (pipeline variant, pool block), not one per mesh.
  2. CPU submission time for those passes drops against the Phase 0 baseline.
  3. Golden captures stay pixel-identical to the reference (`lm8`), within the documented settle
     residual.
  4. No validation errors on the heap path or on the forced fallback path.
  5. On the RTX 4070 Ti, the G-buffer runs on mesh shaders with the same image.
- **Non-goals:**
  - transparent, selection, wireframe and collider passes (they keep today's path);
  - skinned meshes (no skeletal animation exists);
  - `VK_EXT_descriptor_buffer`;
  - any temporal technique.
- **Constraints:**
  - Existing scenes, materials and projects load unchanged.
  - Exports and the editor's Dist `ShaderPack.lsp` are regenerated.
  - Every phase works on the RTX 4070 Ti and on a forced fallback path that stands in for
    laptop-class GPUs (no descriptor heap, no mesh shaders).
  - Make it work, then good, then fast.
- **User decisions (2026-09-25, re-confirmed 2026-11-09):** same scope, including the mesh-shader
  G-buffer; a shared geometry pool; opaque and shadow passes first; mesh shaders tested on the PC.
- **User decisions (2026-11-09):**
  - **Draw count:** the CPU sets it first; the GPU writes it in a later phase.
  - **Heap:** use the **descriptor heap mode** (`NRIDescriptorHeap`, `VK_EXT_descriptor_heap`)
    instead of mutable descriptor sets.
  - **Targets:** the PC plus a laptop fallback path.

## Decisions this plan is built on

| Decision | Choice | Consequence |
|---|---|---|
| Heap model | **Descriptor heap** where `features.descriptorHeap` is true (user). Elsewhere, a **descriptor-set fallback**: one `MUTABLE` range at set 4 when `features.mutableDescriptorType`, else per-kind ranges at sets 4–7 (technical: the laptop needs something) | Two backends behind one slot space. Shaders reach textures through NEW `Bindless.glslh` macros, chosen by a global shader macro (`LUX_BINDLESS_HEAP`). A switch forces the fallback for testing. |
| Heap pipelines | Converted passes use heap pipeline layouts: **no descriptor sets**, `RESOURCE_HEAP_DIRECTLY_INDEXED` (+ `SAMPLER_HEAP_DIRECTLY_INDEXED`) (NRI rule, `NRIDescs.h:1003-1009`) | Per-pass buffers (`GPUMaterials` at set 2, samplers at set 3, the scene buffers) must also come from the heap or from buffer device addresses in those passes. Other passes keep their sets. Phase 0 proves the mix is valid in one command buffer. |
| Heap slot lifetime | One heap. Slots are freed through a retire queue keyed by render frame number, reusable only after `FramesInFlight` frames (technical) | Replaces today's per-frame tables (`BindlessTextureTable::m_Frames`). NRI writes are host-side and thread-safe except on overlapping indices (`NRIDescriptorHeap.h`); all writes happen on the render thread. |
| Kinds | Texture2D, TextureCube, Texture2DArray, Texture3D in one index space (as before) | The heap needs no per-kind split. The per-kind fallback uses 4 sets. |
| Geometry | **Shared pool** (user) of fixed blocks for each of today's three streams: position vertices, attributes, indices (`MeshSource::m_VertexBuffer/m_AttributeBuffer/m_IndexBuffer`) | Keeps fixed-function vertex input and the post-transform cache. A pass issues one multi-draw per (pipeline, block). |
| Per-draw data | `firstInstance` = today's `ObjectIndexBase`, the G-buffer's only per-draw push constant (`GBuffer_Static.glsl:30`, `MeshDrawPushConstants`) | Needs `drawIndirectFirstInstance`. `LightIndex` (the cascade) stays a per-pass push constant. |
| Draw count | CPU-known `drawNum` through `CmdDrawIndexedIndirect(buffer, offset, drawNum, stride, nullptr, 0)` (user) | Needs `multiDrawIndirect`, which `VulkanDevice` does not enable today. Culled draws keep zero instances, as now. The GPU-written count (`countBuffer`, `features.drawIndirectCount`) is Phase 6. |
| Buffers in shaders | Buffer device addresses (`GetBufferDeviceAddress`, `NRI.h:268`; `bufferDeviceAddress` is already enabled) for the mesh-shader path and wherever heap pipelines need a buffer | Needs `GL_EXT_buffer_reference` and `shaderInt64`. No shader uses device addresses today. |

## Part 0 — Where we are

| Capability | State | Evidence |
|---|---|---|
| Bindless material textures | ✅ Working (renders every scene) | `Renderer/BindlessTextureTable.h`: `DescriptorSet = 4`, `MaxCapacity = 16384`, one `texture2D u_GPUMaterialTextures[]` (`MaterialScene.glslh`). Per-frame sets, written on the render thread. **2D only.** sRGB slots since `ece01f54`. |
| Material table, samplers | ✅ Working | `GPUMaterials` storage buffer at set 2, binding 7 (`TextureIndices` per material); samplers at set 3, bindings 0–4 (`Samplers.glslh`). |
| Per-draw state | ⚠️ One draw per mesh | `SceneRenderer::RT_DrawStaticMesh` (`SceneRenderer.cpp:10363`, 9 call sites) binds the mesh's own vertex, attribute and index buffers, an optional material set 0 and push constants. `Renderer::RT_DrawIndexed` (`Renderer.cpp:1653`) issues `CmdDrawIndexedIndirect(..., 1, ...)` or `CmdDrawIndexed`. |
| GPU culling | ✅ Working | `MeshCulling.glsl` and `ShadowCulling.glsl` write instance counts into `m_SBSIndirectDrawCommands` (4096 args); args built per `MeshKey`/LOD in `SceneRenderer.cpp:7720-7742`. Retained render scene plan done (`docs/RENDER_SCENE_RETAINED_PLAN.md`). |
| Pipeline variants | ✅ Known | Opaque draws already split by `TwoSided`, `AlphaMasked` and `Mirrored` (`SceneRenderer.h:1002-1058`; pipelines `GeometryTwoSidedPipeline`, `PreDepthMaskedPipeline`, …). Multi-draws must group by these. |
| Mesh storage | ⚠️ Per mesh | One position VB, one attribute VB and one IB per `MeshSource` (`Mesh.h:321-339`); QTangent attributes (`QTangent.glslh`). |
| Meshlets | ⚠️ Pre-depth only | `PreDepth_Meshlet.glsl`; `MeshSource::RT_GetOrCreateMeshletSet` (a per-mesh set). Built only when `Renderer::SupportsMeshShaders()`. |
| Renderer capabilities | ✅ Available, partly unused | `NRIDescs.h`: `CmdDrawIndexedIndirect` takes `drawNum` and an optional `countBuffer`; `features.drawIndirectCount`, `mutableDescriptorType`, `descriptorHeap`, `meshShader`; `DescriptorRangeBits::PARTIALLY_BOUND/VARIABLE_SIZED_ARRAY/ALLOW_UPDATE_AFTER_SET`; `NRIDescriptorHeap.h` (`CreateDescriptorHeap`, `WriteResourceDescriptors`, `CmdSetDescriptorHeap`). |
| Device features | ⚠️ Partial | `VulkanDevice.cpp:590-642` enables descriptor indexing, partially bound, variable count, update-after-bind and buffer device address. It does **not** enable `multiDrawIndirect`, `drawIndirectFirstInstance`, mutable descriptors or `VK_EXT_descriptor_heap`. |
| Descriptor heap toolchain | ⚠️ Unproven | `VK_EXT_descriptor_heap` arrived in Vulkan 1.4.340 ([Phoronix](https://phoronix.com/news/vulkan-1.4.340-descriptor-heap)). Local SDK is 1.4.341.1 and `scripts/Vulkan.py` installs 1.4.341.1, but **CI pins 1.4.335.0** (`main.yml`). Driver 616.56 (NVIDIA supports it from 610). GLSL support (`GL_EXT_descriptor_heap`) through the bundled shaderc is unverified. |
| Benchmarks | ✅ Exist | `Benchmark.luxscene`, `Scenes/Benchmarks/`, `tests/rendering/golden_run.py`; golden reference `lm8`. |

Only the bindless material textures are verified at runtime. Every other row was read from source.

## Part 1 — Design

```
Renderer (process-wide)
 ├─ BindlessResourceHeap (NEW, replaces BindlessTextureTable)
 │    ├─ Heap backend:     one NRI DescriptorHeap (resources + samplers), CmdSetDescriptorHeap per cmd buffer
 │    └─ Fallback backend: set 4 MUTABLE range | sets 4-7 per kind   (forced by LUX_BINDLESS_FALLBACK=1)
 │    one slot allocator, retire queue keyed by render frame number
 └─ GeometryPool (NEW): position / attribute / index blocks, free-list suballocation
SceneRenderer
 └─ per converted pass: indirect args grouped by (pipeline variant, pool block)
                        → CmdDrawIndexedIndirect(offset, drawNum) per group
Shaders
 ├─ Bindless.glslh (NEW): LUX_TEXTURE2D(i), LUX_TEXTURECUBE(i), LUX_SAMPLER(i)… for either backend
 ├─ gl_InstanceIndex (= firstInstance + i) → ObjectIndexes → GPUSceneInstances → GPUMaterials
 └─ buffers in heap pipelines: device addresses passed in a per-pass root constant / buffer
```

**Threads.**
- **Main thread:** records slot requests (as `BindlessTextureTable::SetSlot` does today) and all ECS work.
- **Render thread:** heap writes, pool block uploads (through the batched upload path,
  `Rendering.md § Batched resource uploads`), indirect argument writes.
- **Asset thread:** decodes meshes. Pool suballocation happens when a mesh first syncs on the main
  thread, under the pool's mutex.
- Correct under both threading policies, since every GPU write goes through `Renderer::Submit`.

**Ownership.**
- Pool ranges belong to their `MeshSource`. Freeing goes through `Renderer::SubmitResourceFree`, so
  in-flight frames finish first.
- Blocks live until shutdown.
- Heap slots belong to their owner (each `SceneRenderer` keeps its own index space, as today) and
  retire after `FramesInFlight` frames.
- Scene switches already drain the GPU (`scene-switch` rule); the heap adds no new teardown.

**Renderer invariants** (`Rendering.md`):
- New sets 5–7 (fallback only) are grepped for collisions in Phase 1.
- Pipelines are created once in `Init()`.
- The pass `Reads`/`Writes` lists stay accurate.
- `ComputeStructureHash()` folds in the backend choice.
- Render targets sampled through the heap get explicit state transitions, because the RHI tracks
  only bound resources.

**Compatibility.**
- No scene, material or project format change.
- Exports must be re-exported. The editor's Dist `Editor/Resources/ShaderPack.lsp` is refreshed (its
  stamp catches the stale sources).

## Part 2 — Phases

Every phase:
- builds Debug, Release and Dist (Editor and `Lux-Runtime`);
- regenerates projects when it adds files;
- clears `Editor/Resources/Cache/Shader` after shared GLSL header edits;
- runs `Benchmark` and `AudioDemo` under validation, on the heap path **and** with
  `LUX_BINDLESS_FALLBACK=1`, with zero validation errors;
- captures goldens with `tests/rendering/golden_run.py` against `lm8`.

### Phase 0 — Baseline and the heap spike
**Goal:** know the starting numbers, and whether the descriptor heap works in this engine, before
building on it.
**Changes (throwaway branch):**
- `/profile` protocol on `Benchmark` and `AudioDemo` (Release, 1280×720): CPU submission ms and draw
  counts for pre-depth, G-buffer and directional/spot shadows; GPU time per pass.
- Spike: compile one GLSL fragment shader with `GL_EXT_descriptor_heap` through the engine's shaderc
  path; create an NRI descriptor heap; draw a test quad that samples a heap texture; bind heap
  pipelines and set-based pipelines in one command buffer; run under validation.
**Exit:**
- Baseline numbers are recorded in this file.
- The spike renders with no validation errors, **or** the plan switches the heap backend to the
  mutable-set path and records why. That decision is shown to the user before Phase 1.
- If submission time is already small, the plan's numbers target is restated before going on.
**Rollback:** nothing merged.

### Phase 1 — One heap for every texture kind
**Goal:** `BindlessResourceHeap` holds 2D, cube, 2D-array and 3D textures in one slot space on both
backends. Material textures move to it with identical images.
**Changes:**
- `VulkanDevice.cpp`: enable `VK_EXT_descriptor_heap` (and `VK_KHR_shader_untyped_pointers`) and
  mutable descriptors when present; log the chosen backend.
- `Renderer/BindlessTextureTable.{h,cpp}` → **NEW** `Renderer/BindlessResourceHeap.{h,cpp}`: slot
  allocator, `SetSlot(slot, texture, kind)`, retire queue, both backends.
- **NEW** `Editor/Resources/Shaders/Include/GLSL/Bindless.glslh`; `MaterialScene.glslh` uses it.
- `Renderer::Init`: `LUX_BINDLESS_HEAP` global macro.
- CI: Vulkan SDK 1.4.335.0 → 1.4.341.1 in `main.yml`, so CI matches `scripts/Vulkan.py`.
- Grep the shader corpus for sets 5–7 before using them in the fallback.
**Playbook:** none (no component, asset type or pass).
**Verification:**
- Goldens identical.
- A cube-map and a 3D texture sampled by slot in a debug view on both backends.
- The Renderer Debugger shows the backend and slot use.
**Docs:** `Rendering.md` § Bindless textures (backends, slot lifetime); `Building.md` (SDK version).

### Phase 2 — Shared geometry pool
**Goal:** static meshes live in pooled position, attribute and index blocks. Draws still go one per
mesh, but bind the block with `baseVertex`/`baseIndex` offsets.
**Changes:**
- **NEW** `Renderer/GeometryPool.{h,cpp}`: fixed blocks (sizes from Phase 0's totals), a free-list
  per block under a mutex, `Allocate(vertex, attribute, index bytes)`.
- `MeshSource`: pool ranges replace its own buffers. `GetVertexBuffer`/`GetAttributeBuffer`/
  `GetIndexBuffer` return the block, plus offsets.
- `RT_DrawStaticMesh` and the meshlet path apply the offsets.
**Verification:** goldens identical; mesh hot-reload and project switch free ranges (no VMA leak
assert at exit, `18514538`); memory per block shown in the Renderer Debugger.
**Docs:** `Rendering.md` (geometry pool, ownership); Architecture § Mesh.

### Phase 3 — Multi-draw for pre-depth and G-buffer
**Goal:** the opaque passes issue one indirect multi-draw per (pipeline variant, block).
**Changes:**
- `VulkanDevice.cpp`: enable `multiDrawIndirect`, `drawIndirectFirstInstance`.
- `SceneRenderer`: build indirect args contiguously per (variant, block) with `firstInstance` =
  object index base; issue `CmdDrawIndexedIndirect(offset, drawNum)` (a new
  `Renderer::RT_DrawIndexedIndirectMulti`).
- `PreDepth*.glsl`, `GBuffer_Static.glsl`: drop `ObjectIndexBase`; `objectIndex = gl_InstanceIndex`.
  Heap backend: converted pipelines take heap layouts and reach `GPUMaterials` and scene buffers by
  device address.
- Stop binding material set 0 for passes whose shader declares none.
**Verification:** draw counts drop to the group count (Renderer Debugger); goldens identical; CPU
submission ms against Phase 0.
**Docs:** `Rendering.md` (multi-draw rule, per-draw data through `firstInstance`).

### Phase 4 — Shadows on the same path
**Goal:** directional cascades and spot shadows (including the `_Masked` variants) multi-draw.
**Changes:** `DirShadowMap*.glsl`, `SpotShadowMap*.glsl`, the shadow cull groups and shadow pass
recording in `SceneRenderer`.
**Verification:** shadow goldens identical, including cut-out and two-sided casters; moving-light
captures match.

### Phase 5 — Every remaining texture through the heap
**Goal:** per-pass textures (environment maps, LUTs, shadow maps sampled by lighting) and samplers
come from the heap. Lighting passes stop binding per-pass texture sets.
**Changes:** the lighting and post passes that sample those textures; the sampler heap (heap backend)
or set 3 (fallback); explicit state transitions for render targets sampled by index.
**Verification:** goldens identical; validation clean with render targets reached only by index.

### Phase 6 — GPU-written draw count
**Goal:** culling compacts visible draws and writes the count, so empty draws vanish.
**Changes:** `MeshCulling.glsl`/`ShadowCulling.glsl` compaction; `countBuffer` passed where
`features.drawIndirectCount` is true; the CPU count stays as the fallback.
**Verification:** identical images; fewer GPU draws in a capture; fallback still correct.

### Phase 7 — Mesh-shader G-buffer
**Goal:** on mesh-shader GPUs, the G-buffer uses task and mesh shaders reading meshlets through
device addresses and materials through the heap. Other GPUs keep Phase 3's path.
**Changes:** **NEW** `GBuffer_Meshlet.glsl` (task/mesh/fragment); meshlet data in the pool or in its
own block addressed by device address (replacing `RT_GetOrCreateMeshletSet`); per-meshlet culling in
the task stage.
**Verification:** RTX 4070 Ti goldens identical; G-buffer GPU time against Phase 3; non-mesh-shader
path unchanged.
**Docs:** `Rendering.md` (mesh-shader G-buffer), Architecture § Renderer.

## Part 3 — Risks

| Risk | Likelihood | Early detection |
|---|---|---|
| GLSL descriptor heaps aren't supported by the bundled shaderc, or heap and set pipelines can't mix in one command buffer | Medium | Phase 0 spike; fall back to the mutable-set backend for heap passes. |
| CI's SDK bump changes other shader output | Low | Phase 1 goldens on CI artifacts; the shader-pack stamp flags it. |
| The laptop's driver lacks mutable descriptors | Medium | Per-kind fallback (sets 4–7); forced fallback runs every phase. |
| Multi-draw gains are small because submission is already cheap (retained plan cut main-thread time) | Medium | Phase 0 baseline; restate the target before Phase 3. |
| Render targets read by index without tracked state transitions | Medium | Phase 5 validation; explicit transitions around those passes. |
| Pool fragmentation after many reloads | Low | Free-list stats in the Renderer Debugger; block size tuning. |

## Part 4 — Open questions

- Does `VK_EXT_descriptor_heap` work on the laptop's AMD Mesa (RADV) driver? Unknown. The fallback
  covers it either way; checking would let the laptop use the heap.
- In heap pipelines, should per-pass constants (camera, scene buffer addresses) be root constants or
  one small address table? Decide in Phase 3 from the spike.
- Is the D3D12 future backend (`LUX_HAS_DX12` scaffolding) a reason to keep heap-mode shaders
  portable to HLSL SM 6.6 `ResourceDescriptorHeap`? Out of scope; note it if the macros allow both.

## Part 5 — Research notes

- **Descriptor heap concept:** NRI's "descriptor heap" mode means no descriptor sets plus
  directly indexed resource and sampler heaps, mirroring HLSL SM 6.6 dynamic resources
  (`NRIDescs.h:1003-1009`, `NRIDescriptorHeap.h`).
- **Vulkan extension:** `VK_EXT_descriptor_heap`, Vulkan 1.4.340, multi-vendor. It needs buffer
  device address and `VK_KHR_shader_untyped_pointers`
  ([proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_descriptor_heap.html),
  [guide](https://docs.vulkan.org/guide/latest/descriptor_heap.html)).
- **Driver support:** NVIDIA supports it from driver 610, with samples and tooling
  ([NVIDIA blog](https://developer.nvidia.com/blog/streamlining-resource-binding-with-end-to-end-support-for-vulkan-descriptor-heaps/));
  Sascha Willems has heap samples ([blog](https://saschawillems.de/blog/2026/06/13/new-vulkan-samples-for-using-descriptor-heaps/)).
- **GLSL:** glslang has `GL_EXT_descriptor_heap` tests, and 16.4.0 notes a structured descriptor
  heap extension. The exact name and shaderc support are unverified, which is why Phase 0 exists.
- **Rejected:**
  - temporal anything (standing rule);
  - `VK_EXT_descriptor_buffer` (superseded by the heap extension, and a non-goal);
  - resuming the 2026-09-25 NVRHI phases (their layout mechanics no longer apply).

## Handoff

Start with Phase 0. Implement each phase with `/dev`, review with `/cr`, and ship with `/send-pr`.
