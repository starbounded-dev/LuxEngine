# LuxEngine — Renderer Integrity

The renderer is where LuxEngine's correctness is hardest to see and easiest to break. A mistake here
does not usually produce a clean crash — it produces a validation error nobody reads, a black
screen on one GPU vendor, a slow leak of GPU memory, or a binding collision that silently corrupts a
*different* pass's data. This doc covers the invariants that are not obvious from reading a single
header.

Read this before touching anything under `Core/Source/Lux/Renderer/` or
`Core/Source/Lux/Platform/Vulkan/`.

---

## What is renderer-critical

- `Core/Source/Lux/Renderer/**` — especially `Renderer.{h,cpp}`, `SceneRenderer.{h,cpp}`,
  `RenderGraph.{h,cpp}`, `RenderCommandQueue`, `RenderCommandBuffer`, `Material`, `Pipeline`,
  `Shader`, `StorageBufferSet` / `UniformBufferSet`.
- `Core/Source/Lux/Platform/Vulkan/**` — the nvrhi/Vulkan backend, `DescriptorSetManager`,
  `ShaderCompiler/**`, `VulkanSwapChain`, `VulkanAllocator`.
- `Editor/Resources/Shaders/**` — the shader corpus itself.

Changes in these paths are reviewed at higher rigour than ordinary engine code; `/cr` promotes
findings here to must-fix.

---

## Architecture in three layers

| Layer | File | Role |
|---|---|---|
| `Renderer` | `Renderer/Renderer.h` | Static facade: command queue, frame index, default textures/samplers, shader library, pass/dispatch entry points, resource-release queues |
| `SceneRenderer` | `Renderer/SceneRenderer.h` | Owns and drives the `RenderGraph`; the whole deferred pipeline lives here |
| `Renderer2D` / `DebugRenderer` | `Renderer/Renderer2D.h`, `DebugRenderer.h` | Batched 2D (quads, circles, lines, MSDF text) and debug primitives |

Below that sits **NVRHI** (`Core/vendor/nvrhi`) over Vulkan. `LUX_HAS_VULKAN` is always defined;
`NVRHI-D3D11` / `NVRHI-D3D12` are built but the `Platform/DX11` / `DX12` engine sources are
`removefiles`'d — that scaffolding is intentional, not dead code.

All renderer API types (`Shader`, `Texture`, `Pipeline`, `Image2D`, `Framebuffer`, …) are abstract;
their Vulkan implementations live in `Platform/Vulkan/`.

---

## Invariant 1 — `(set, binding)` is a GLOBAL namespace

**This is the highest-consequence rule in the renderer.**

`VulkanShaderCompiler`'s reflection keeps *process-global* registries of uniform buffers and storage
buffers keyed by `(descriptorSet, binding)`:

```cpp
static std::unordered_map<uint32_t, std::unordered_map<uint32_t, ShaderResource::UniformBuffer>>  s_UniformBuffers;
static std::unordered_map<uint32_t, std::unordered_map<uint32_t, ShaderResource::StorageBuffer>>  s_StorageBuffers; // set -> binding -> buffer
```

Every shader in the engine shares these maps. The semantics:

- Two shaders declaring the **same-named** buffer at the same `(set, binding)` are **intentionally
  merged** — that is how shared renderer UBOs (`Camera`, `ScreenData`, `Scene`, `RendererData`,
  shadow data, …) work.
- Two shaders declaring a **different-named** buffer at the same `(set, binding)` is an **accidental
  collision that silently corrupts the other shader's binding**. The compiler logs
  `LUX_CORE_ERROR_TAG("Renderer", "Uniform buffer binding collision at (set=…, binding=…) …")` and
  keeps going.

So: **picking an unused `(set, binding)` for a new buffer is not a local decision.** Before adding
one, grep the shader corpus for the slot you intend to use.

```bash
grep -rn "set = 1, binding = 17" Editor/Resources/Shaders/
```

Two further details that matter when you touch reflection:

- HLSL constant buffers are named `type.ConstantBuffer.<Type>` by SPIRV-Cross, so the collision
  check normalises that prefix away before comparing. An HLSL shader legitimately sharing a GLSL
  renderer UBO would otherwise false-positive. Keep that normalisation if you extend the check.
- The registries are guarded by `s_ShaderBuffersMutex` so reflection stays correct if shaders are
  ever compiled off the main thread. Any new reflected resource kind must take that lock.

**A binding-collision error in the log is never cosmetic.** Treat it as a build break.

---

## Invariant 2 — pipelines, shaders, and descriptor layouts are cached, never per-frame

Creating a graphics/compute pipeline, compiling a shader, or baking a descriptor set layout inside a
per-frame code path is a correctness *and* performance bug. The engine has caches for all three; use
them.

- **Pipelines** are created in `SceneRenderer::Init()` and held in members (`m_...Pass`,
  `m_...Pipeline`). Register shader dependencies so hot reload rebuilds them:
  `Renderer::RegisterShaderDependency(shader, pipeline | computePipeline | material | renderPass | computePass)`.
- **Process-wide shared pipelines** go through `Renderer::GetOrCreateMipGenPipeline(shader)`. That
  accessor exists because mip generation was building hundreds of identical pipelines at load —
  don't reintroduce that pattern with a new local cache.
- **Descriptor sets** are managed by `DescriptorSetManager` (`Platform/Vulkan/DescriptorSetManager.h`),
  which covers sets `StartSet = 0 … EndSet = 3`. `IsDynamic` (default true) makes it re-check
  resources for change; `Validate()` then `Bake()` is the commit path. Bind by **name** via
  `SetInput("Camera", …)`, not by raw slot.

If you need a pipeline variant per frame, you actually need a **permutation** — see below.

---

## Invariant 3 — GPU resources are freed through the frame-indexed release queue

`FramesInFlight` defaults to **3** (`Renderer/RendererConfig.h`). A resource still referenced by an
in-flight command buffer must not be destroyed when the CPU drops its last `Ref`.

Use `Renderer::SubmitResourceFree(lambda)`. It allocates into
`Renderer::GetRenderResourceReleaseQueue(frameIndex)` for the *current* frame index, so the
destruction runs only once the GPU has finished that frame. It mirrors `Renderer::Submit`'s
thread branching: inline-allocate when already on the render thread, otherwise defer via `Submit`.

Never call `delete`, `nvrhi` `Handle` reset, or a Vulkan destroy directly from main-thread code that
could still be referenced this frame.

`GetGraphicsDevice()->runGarbageCollection()` is called once per frame at present time in
`Application::Run` — do not sprinkle extra calls; it is not a fix for a lifetime bug.

---

## The RenderGraph

`Renderer/RenderGraph.h`. A declarative pass graph with transient-resource aliasing, culling, and
validation. `SceneRenderer` owns one (`m_RenderGraph`) and rebuilds its description each frame.

### Structure

- `AddTransientTexture(TextureDesc)` → `ResourceHandle`. `Transient` and `AllowAlias` default true;
  aliasing reuses memory between resources with disjoint lifetimes.
- `AddPass(PassDesc)` where `PassDesc` is `{ Name, Reads, Writes, Flags, Execute, DebugName }`.
  `PassFlags`: `Graphics`, `Compute`, `Transfer`, `SideEffect`, `NeverCull`.
- `Compile()` → `CompileResult` with execution order, culled passes, resource lifetimes, alias
  groups, and typed `Diagnostic`s.
- `Execute(compileResult)` runs the surviving passes' callbacks.

`DebugName` is an always-set pointer to the pass's string literal, kept **last** in the struct so the
positional aggregate initializers in the validation self-tests still map to
`Name`/`Reads`/`Writes`/`Flags`. If you add a field, add it after `DebugName` or fix the self-tests.

### Compile caching — do not compile per frame

`SceneRenderer` caches the compile:

```cpp
const uint64_t structureHash = m_RenderGraph.ComputeStructureHash();
if (!m_RenderGraphResultValid || structureHash != m_RenderGraphStructureHash)
{
    m_CachedRenderGraphResult = m_RenderGraph.Compile();
    m_RenderGraphStructureHash = structureHash;
}
…
m_RenderGraph.Execute(renderGraphResult);
```

`ComputeStructureHash()` folds **every field `Compile()`/`Execute()` depend on** — pass topology plus
texture metadata. Equal hashes therefore imply an equivalent `CompileResult`.

**If you add a field to `PassDesc` or `TextureDesc` that affects compilation, you must fold it into
`ComputeStructureHash()`.** Forgetting to is the classic RenderGraph bug: the graph silently reuses a
stale compile, and the new resource is never allocated or the new pass never runs. It will look
like "my pass does nothing" — not like a hash bug.

### Diagnostics

`DiagnosticCode` covers `ReadBeforeWrite`, `UnwrittenExternalRead`, `DeadWrite`,
`ReadWriteSameResource`, `DuplicatePassName`, `DuplicateTextureName`, `InvalidPassFlags`,
`EmptyExecutablePass`, `AliasLifetimeConflict`, `AliasIncompatibleResource`, and more. They surface
in the Renderer Debugger panel via `SceneRenderer::RenderGraphDebugSnapshot`.

`RenderGraph::RunValidationSelfTests(&failures)` exists — run it when changing compile/alias logic.

### Adding a pass

1. Write the shader in `Editor/Resources/Shaders/` (see the shader section below).
2. Create the `RenderPass` / `ComputePass`, its pipeline, and its `Material` in
   `SceneRenderer::Init()`. Store them in members.
3. Register shader dependencies (`Renderer::RegisterShaderDependency`).
4. Declare transient targets with `AddTransientTexture`.
5. Add the pass in the graph-building block of `SceneRenderer.cpp` (search for
   `addPass("Composite"` to find it) with **accurate** `Reads` / `Writes` — the graph culls and
   aliases based on them, so a lie here produces a use-after-alias, not a warning.
6. Gate optional passes on their feature flag so they are skipped at zero cost when off.

Existing pass order, for orientation: shadow maps → PreDepth → HZB → mesh culling → cluster build →
cluster light culling → skybox → sky atmosphere → selected geometry → GBuffer → deferred lighting →
GTAO (+ denoise, temporal) → AO composite → pre-convolution → SSR (+ temporal, composite) →
volumetric clouds (+ temporal, composite) → atmospheric fog → transparent forward → wireframe →
jump flood → TAA → auto exposure → bloom → composite → SMAA → DOF.

---

## Shaders

**LuxEngine is GLSL-first.** The corpus is ~68 `.glsl` + 15 `.glslh` includes, with a small number of
`.hlsl` / `.hlslh` / `.slh` files. This is the opposite of some Hazel-derived engines — do not assume
HLSL-only.

- Sources: `Editor/Resources/Shaders/`, shared includes under `Include/`, post-process under
  `PostProcessing/`.
- Preprocessing/includes: `Platform/Vulkan/ShaderCompiler/ShaderPreprocessing/` — `GlslIncluder`,
  `HlslIncluder`, `ShaderPreprocessor`. `HlslIncluder.cpp` calls `DxcCreateInstance` and is excluded
  from Linux builds.
- Compilation + reflection: `VulkanShaderCompiler` (SPIRV-Cross for reflection).
- Binary cache: `VulkanShaderCache`, registry at `Resources/Cache/Shader/ShaderRegistry.cache`
  (YAML, `ShaderRegistry` sequence). `HasChanged(shader)` returns which stages need recompiling.
- Permutations: `ShaderPermutationCache` keys on `{ ShaderName, sorted macro pairs }`. Macros come
  from `Renderer::SetMacroInShader` / `SetGlobalMacroInShaders`, with
  `AcknowledgeParsedGlobalMacros` recording what a shader actually declared.

Runtime entry points: `Renderer::ReloadShaders(forceCompile)`, `UpdateDirtyShaders()`,
`WarmUpShaderPipelines()`, `GetShaderPermutationCacheSize()`.

**When a shader stops working after an edit, suspect the cache first.** Delete
`Resources/Cache/Shader/` and relaunch before debugging the shader itself. A stale registry entry
looks exactly like a broken shader.

**Uniform/storage buffer layout rules:** respect std140/std430 alignment. A `vec3` is 16-byte
aligned; a mismatch between the C++ `UB*` struct in `SceneRenderer.h` and the GLSL block is not a
compile error in either language — it is garbage in the shader. When you edit a `UBCamera`,
`UBScene`, `UBShadow`, `UBRendererData`, `UBScreenData`, `UBAtmosphere`, `CBGTAOData`, … struct,
edit the matching GLSL block in the same change.

---

## Validation errors are bugs

Vulkan validation output (`Platform/Vulkan/VulkanDiagnostics.{h,cpp}`) is not noise. A validation
error means a real object-lifetime, layout, or synchronisation mismatch that will manifest as a
crash or corruption on some driver even if it renders correctly on yours.

- Do not silence, filter, or `#ifdef` away a validation message to make a log quiet.
- Fix the underlying mismatch: image layout, descriptor lifetime, pipeline/renderpass compatibility,
  or missing barrier.
- Nvidia Aftermath GPU crash dumps live in `Platform/Vulkan/Debug/` and are compiled out of Dist (and
  removed entirely with `--no-aftermath`). When chasing a device-lost, build with them in.

Known-good debugging recipe for device-lost on scene transitions: drain the GPU before tearing down
per-scene GPU state. Freeing in-flight descriptor pools on a scene switch is a real failure mode this
engine has hit before.

---

## Performance rules

Watch for, and reject in review:

- Pipeline / shader / descriptor-layout creation in a per-frame path.
- Recreating buffers or images every frame instead of resizing on viewport change.
- `std::string` construction or `std::format` per draw call.
- Unbounded growth in a per-frame vector that is never `clear()`ed (or `clear()`ed but never
  `reserve()`d, reallocating every frame).
- A GPU sync point (fence wait, `WaitIdle`) added inside the frame loop to "fix" a race — that is a
  barrier or lifetime bug wearing a disguise.
- Passes that run when their feature is disabled. Gate them so they cost zero.

Use `LUX_PROFILE_*` (CPU) and `Renderer::BeginGPUPerfMarker` / `EndGPUPerfMarker` (GPU) around new
work. `SceneRenderer::PassProfile` / `Statistics` already collect per-pass timings shown in the
Render Stats panel; a new pass should appear there.

### GPU timing has two consumers, from one call

`Renderer::BeginGPUPerfMarker` / `EndGPUPerfMarker` funnel into
`RenderCommandBuffer::RT_BeginTimerQuery` / `RT_EndTimerQuery`, and that pair feeds **both**:

1. the engine's own nvrhi timer queries, read back by the Renderer Debugger's Pass Timings table, and
2. a **Tracy GPU zone**, so the same pass appears on Tracy's GPU timeline.

So wrapping a new pass in `BeginProfiledGPU` is all that is needed — do not add Tracy GPU zones by
hand. The `TracyVkCtx` is owned by `VulkanDeviceManager` (created in `CreateDevice`, destroyed in
`DestroyDevice` before `vkDestroyDevice`) and reached via `GetGPUProfilerContext()`, which may be
null if creation failed; every consumer must tolerate that.

Two rules if you touch this path:

- **A GPU zone may never outlive its command buffer's recording state.** `~VkCtxScope` issues a
  `vkCmdWriteTimestamp`, so a zone closed after `close()` is a Vulkan usage violation. `RT_End`
  therefore force-closes any zone still open before collecting.
- `TracyVkCollect` runs in `RT_End`, outside any render pass and before `close()`.

Tracy GPU zones only carry data while a profiler is connected (`TRACY_ON_DEMAND`), and the whole
path compiles out in Dist. Note that Tracy captures record **CPU zones only** unless this context
exists — a capture with zero GPU zones means the context failed to create, not that the GPU is idle.

---

## Batched resource uploads and the async transfer queue

Buffer/texture constructors record initial-data uploads into **one shared command list** via
`Renderer::RecordResourceUpload(record)`, flushed by `Renderer::FlushResourceUploads()` before every
`RenderCommandBuffer` submission. The reason is in the header: a `vkQueueSubmit` per mesh/texture is
a load hitch and contends the graphics queue against the render thread.

The record callback runs synchronously, so callers may free their CPU data on return (nvrhi stages
it into the command list at record time).

When enabled *and* the device has a dedicated transfer queue, the batch is submitted on the copy
queue: `SetAsyncTransferQueueEnabled` / `IsAsyncTransferQueueEnabled` / `UseAsyncTransferQueue`
(runtime toggle `Renderer.AsyncTransferQueue`). Cross-queue ordering is then explicit —
`Renderer::ConsumePendingUpload(consumingQueue, outInstance)` and
`Renderer::QueueWaitForCommandList(waitQueue, executionQueue, instance)`.

**If you add a new consumer queue, you must wait on pending uploads.** Skipping the wait produces
reads of not-yet-uploaded memory — intermittent, GPU-dependent, and very hard to attribute.

---

## Feeding the renderer from the scene

Renderer submission must go through the frame packet, not the live ECS:

- `Scene::BuildRenderPacketEditor` / `BuildRenderPacketRuntime` capture everything the renderer needs
  into a `FrameRenderPacket` (`Renderer/FrameRenderPacket.h`).
- `Scene::SubmitRenderPacket` replays that packet without touching the registry.
- `Scene::SyncRenderScene` maintains the persistent render-side `RenderScene` /
  `StaticMeshRenderProxy` / `GPUScene` state, with dirty flags.

**New renderer-visible state belongs in the packet.** Reading the ECS during submission works today
and is exactly what blocks the simulation-thread split later (see `.claude/docs/Threading.md`).

---

## What `/cr` treats as must-fix in these paths

| Pattern | Why |
|---|---|
| New `(set, binding)` reused by a differently-named buffer | Silent cross-shader corruption (Invariant 1) |
| Pipeline / shader / descriptor-layout creation in a per-frame path | Hitching + churn (Invariant 2) |
| GPU resource destroyed without `SubmitResourceFree` | Use-after-free while frames are in flight (Invariant 3) |
| New `PassDesc` / `TextureDesc` field not folded into `ComputeStructureHash()` | Stale cached compile; pass silently never runs |
| `Reads` / `Writes` that don't match what the pass actually touches | Wrong culling and wrong aliasing |
| C++ `UB*` struct edited without the matching GLSL block (or vice versa) | Silent garbage uniforms |
| Validation error suppressed, filtered, or ignored | Latent driver-specific crash |
| `Renderer::Submit`'s three-way thread branch collapsed or bypassed | Corrupted single-producer command queue |
| New consumer queue without `ConsumePendingUpload` / `QueueWaitForCommandList` | Reads of un-uploaded memory |
| `ImGui::` call inside a `Renderer::Submit` lambda | ImGui is built on the main thread only |
