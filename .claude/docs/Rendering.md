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
- `Core/Source/Lux/Platform/Vulkan/**` — `VulkanDevice`, `VulkanGPUProfiler`, `ShaderCompiler/**`,
  Aftermath `Debug/`.
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

Below that sits **NRI** (`Core/vendor/NRI`: GPU memory, descriptors, pipelines, command buffers,
all recording, submission and presentation), on the Vulkan device Lux creates (`§ NRI device`).
NVRHI, which Lux ran on before the migration (`docs/NRI_MIGRATION_PLAN.md`), is gone.
`LUX_HAS_VULKAN` is always defined; the `Platform/DX11` / `DX12` engine sources are
`removefiles`'d and the D3D branches in `RHI/RHIDevice.h` are reserved — that scaffolding is
intentional, not dead code.

Renderer types (`Texture2D`, `Image2D`, `Pipeline`, `Framebuffer`, the buffers) are concrete
classes over NRI objects; `GetHandle()` on images and textures is the NRI texture.
`Shader` is the one abstract type, implemented by `RHIShader`.
`Platform/Vulkan/` holds only what NRI does not provide: `VulkanDevice` (instance, device, queues,
validation callback), `VulkanGPUProfiler` (Tracy's GPU context), `ShaderCompiler/` (the SPIR-V
toolchain) and the Aftermath `Debug/` sources (`§ Raw Vulkan outside Platform/Vulkan/`). The shader
object is `Renderer/RHI/RHIShader`, descriptor sets `Renderer/DescriptorSetManager`, the swapchain
`Renderer/RHI/RHISwapChain`.

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
- **Descriptor sets** are managed by `DescriptorSetManager` (`Renderer/DescriptorSetManager.h`),
  which covers sets `StartSet = 0 … EndSet = 3`. `IsDynamic` (default true) makes it re-check
  resources for change; `Validate()` then `Bake()` is the commit path. Bind by **name** via
  `SetInput("Camera", …)`, not by raw slot.

If you need a pipeline variant per frame, you actually need a **permutation** — see below.

---

## Invariant 3 — GPU resources are freed through the release queue

A resource still referenced by an in-flight command buffer must not be destroyed when the CPU drops
its last `Ref`. NRI command buffers keep nothing alive: textures, buffers, descriptor sets and
pipelines are NRI-owned, and their owners free them through this queue.

Use `Renderer::SubmitResourceFree(lambda)` (the lambda must be copyable). It is queued on the
render thread into the `GPUDeletionQueue` (`Renderer/RHI/`), tagged with the render frame number
current at that point in the command stream; it mirrors `Renderer::Submit`'s thread branching.
`Renderer::RT_BeginFrame()` runs first in every render frame (`Application::Run`'s `BeginFrame`
lambda, before the swapchain acquire): it closes the ending frame at the graphics queue's latest
timeline value (`RHIQueueTimeline`, so it also covers work queued after `Present`), advances the
frame number, waits for frame N − `FramesInFlight`, and retires every release queued up to that
frame. Shutdown waits for the device to idle and drains the rest.

The same wait is what makes per-frame slots safe: `RT_GetCurrentFrameIndex()` is
`RT_GetFrameNumber() % FramesInFlight`, so a slot (query slots, `UniformBufferSet`/
`StorageBufferSet::RT_Get`, `DescriptorSetManager` descriptor sets, `BindlessTextureTable` tables) is
reused only after the GPU has finished the frame that last used it. It is **not** the swapchain
image; use `RHISwapChain::GetCurrentBackBufferIndex()` for anything per-swapchain-image.
`FramesInFlight` is clamped to `RendererConfig::MaxFramesInFlight` (3), the capacity of the fixed
per-frame arrays.

Never call `delete`, an NRI destroy, or a Vulkan destroy directly from main-thread code that could
still be referenced this frame.

---

## The RenderGraph

`Renderer/RenderGraph.h`. A declarative pass graph with transient-resource aliasing, culling, and
validation. `SceneRenderer` owns one (`m_RenderGraph`) and rebuilds its description each frame.

### Structure

- `AddTransientTexture(TextureDesc)` → `ResourceHandle`. `Transient` and `AllowAlias` default true;
  aliasing reuses memory between resources with disjoint lifetimes.
- `AddExternalBuffer(BufferDesc{ Name, Set | Buffer })` → `ResourceHandle` for a storage buffer the
  graph orders passes around. Buffers are always external: never transient, never aliased. Buffer
  handles carry `BufferHandleBit`; texture handles stay equal to their index. Per-resource arrays in
  `CompileResult` hold textures first (slot == texture handle), then buffers
  (`GetResourceSlot(handle)`).
- `AddPass(PassDesc)` where `PassDesc` is `{ Name, Reads, Writes, Flags, Execute, DebugName,
  Accesses }`. `Accesses` lists `{ Resource, AccessKind, Range }`: `SampledRead`, `StorageRead`
  (UAV for a texture, SRV for a read-only buffer), `StorageWrite`, `StorageReadWrite`, `ColorWrite`,
  `ColorReadWrite`, `DepthWrite`, `DepthReadWrite`, `DepthReadOnly`, `CopySource`, `CopyDest`,
  `IndirectArgs`, `UniformRead`. `AddPass` adds every access to `Reads`/`Writes` by kind.
  `PassFlags`: `Graphics`, `Compute`, `Transfer`, `SideEffect`, `NeverCull`, `UntrackedResources`
  (now unused by `SceneRenderer`: the cluster passes declare their buffers).
- `Compile()` → `CompileResult` with execution order, culled passes, resource lifetimes, alias
  groups, typed `Diagnostic`s, and per-pass **entry requirements** (the state of every resource the
  pass uses in a single state).
- `Execute(compileResult, commandBuffer)` runs the surviving passes' callbacks. It first requires
  each pass's entry requirements in one batch (`§ Resource states`); a resource the pass uses in
  several states (an in-place mip chain) is left to its dispatches.

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

`ComputeStructureHash()` folds **every field `Compile()`/`Execute()` depend on** — pass topology
(including `Accesses`), texture metadata and buffer presence. Equal hashes therefore imply an equivalent `CompileResult`.

**If you add a field to `PassDesc` or `TextureDesc` that affects compilation, you must fold it into
`ComputeStructureHash()`.** Forgetting to is the classic RenderGraph bug: the graph silently reuses a
stale compile, and the new resource is never allocated or the new pass never runs. It will look
like "my pass does nothing" — not like a hash bug.

### Diagnostics

`DiagnosticCode` covers `ReadBeforeWrite`, `UnwrittenExternalRead`, `DeadWrite`,
`ReadWriteSameResource`, `DuplicatePassName`, `DuplicateTextureName`, `InvalidPassFlags`,
`EmptyExecutablePass`, `AliasLifetimeConflict`, `AliasIncompatibleResource`, `NullBuffer`,
`UndeclaredAccess`, and more. `UndeclaredAccess` (Info) is found while executing, in Debug: a pass
required a graph resource it did not declare — the case that can turn
into a use-after-alias. `GetRuntimeDiagnostics()` returns them; the snapshot lists them per pass. They surface
in the Renderer Debugger panel via `SceneRenderer::RenderGraphDebugSnapshot`.

A resource in both `Reads` and `Writes` is a load-and-store (drawing on top of an attachment,
in-place compute) and is the normal way to declare it. `ReadWriteSameResource` is therefore `Info`,
not a warning; only warnings and errors reach the log.

`RenderGraph::RunValidationSelfTests(&failures)` exists — run it when changing compile/alias logic.

### Adding a pass

1. Write the shader in `Editor/Resources/Shaders/` (see the shader section below).
2. Create the `RenderPass` / `ComputePass`, its pipeline, and its `Material` in
   `SceneRenderer::Init()`. Store them in members.
3. Register shader dependencies (`Renderer::RegisterShaderDependency`).
4. Declare transient targets with `AddTransientTexture`.
5. Add the pass in the graph-building block of `SceneRenderer.cpp` (search for
   `addPass("Composite"` to find it) with **accurate** `Reads` / `Writes` — the graph culls and
   aliases based on them, so a lie here produces a use-after-alias, not a warning. `addPass`
   derives the textures' access kinds (graphics: written = attachments, color/depth by format,
   read + write = load and store; compute: written = storage write; read-only = sampled) and
   takes the storage buffers the pass uses as its last argument, e.g.
   `{ { visibleObjectIndexes, AccessKind::StorageRead } }`; register a buffer with `addBuffer`.
6. Gate optional passes on their feature flag so they are skipped at zero cost when off.

Existing pass order, for orientation: shadow mesh culling (cull groups only) → shadow maps → early mesh
culling (cull groups only) → PreDepth →
HZB → mesh culling → cluster build →
cluster light culling → skybox → sky atmosphere → selected geometry → GBuffer → GTAO (+ denoise,
temporal) → AO composite → deferred lighting → pre-convolution → SSR (+ temporal, composite) →
volumetric clouds (+ temporal, composite) → atmospheric fog → transparent forward → wireframe →
jump flood → TAA → auto exposure → bloom → composite → SMAA → DOF.

**AO is indirect-only.** The AO composite resolves GTAO into its own `RED8UN` image, and deferred
lighting multiplies it into the IBL term only (with material AO, which also drives specular
occlusion). Direct light already has shadow-map visibility, so AO must never multiply the lit scene
color. `SceneRenderer::BeginScene` binds `u_AmbientOcclusion` every frame, inside its render-thread
submit: the resolved image when GTAO is on, the white texture when it is off.

**GTAO has two estimators and up to three noise passes, all per frame.** `CBGTAOData` (push constants,
mirrored by `GTAOConstants` in `GTAO.hlsl`; 88 bytes, asserted) carries:
- **`VisibilityBitmask`**: switches XeGTAO's horizon integral for a 32-sector visibility bitmask
  (Therrien et al. 2023). Each sample occludes only the sectors between its front surface and a point
  `Thickness` metres behind it, so thin objects stop darkening everything under their horizon. Sectors
  are spaced in `sin` of the angle from the normal, so each covers equal cosine-weighted area and the
  result stays comparable to GTAO's integral. This mode does not bend normals.
- **`NoisePassCount`** (1–3): re-runs the slices with the noise shifted along R2 and averages them. This
  is spatial supersampling, not accumulation.

Sky pixels (reverse-Z device depth 0) exit early. The options are
`SceneRendererOptions::GTAOVisibilityBitmask` / `GTAOThickness` / `GTAONoisePasses`. The AO quality
tier sets the noise passes (Ultra 2, Cinematic 3); the bitmask is opt-in.

**Direct lighting is physical.** Every analytic light goes through `LuxEvaluateDirectBRDF`
(`PBR.glslh`): Frostbite diffuse with Lambert's 1/π, GGX with height-correlated Smith visibility,
Turquin multiscatter compensation. Point and spot lights fall off as inverse-square, windowed to zero
at their range (`LuxPunctualLightAttenuation`). Intensities therefore mean what `PhysicalLight.h`
says: a directional light's radiance × multiplier is illuminance, a point or spot light's is
intensity. Point lights' `Falloff` and `MinRadius` are no longer read (still serialized). Spot
`Falloff` is the emitter diameter in metres for PCSS: the editor labels it "Light Size", and the GPU
`SpotLight` calls it `LightSize`.

**Shadow maps are sampled through `r_ShadowSampler`** (`(set 3, binding 8)`,
`Renderer::GetShadowSampler()`): a linear depth-comparison sampler, so each hard or PCF tap is a
hardware 2x2 PCF (`sampler2DShadow` / `sampler2DArrayShadow`, reference ≤ stored, standard-Z shadow
maps). PCSS blocker searches still read raw depth. `DescriptorSetManager` defaults the name to it.

**PCSS taps come from a CPU-built kernel.** `RendererData.ShadowKernel` holds `ShadowKernelSize` (32,
`SHADOW_KERNEL_SIZE` in `Buffers.glslh`; change both) unit-disk offsets: `xy` a Vogel disk over
`ShadowFilterSampleCount`, `zw` one over `ShadowBlockerSampleCount`
(`SceneRenderer::UpdateShadowKernel`). The counts are `SceneRendererOptions::ShadowBlockerSamples` /
`ShadowFilterSamples`, set by the shadow quality tier and saved with the project. A blocker search
that finds every valid tap occluded returns fully shadowed without filtering. That is exact because
the filter radius is clamped to the search radius.

**Spot and point PCSS work in linear depth.** A spot tile or point face is `P * V` with a
perspective, [0, 1]-depth `P`. So the receiver's linear depth is its `clip.w`, and a stored depth `d`
linearizes as `B / (d + A)`, with `A` and `B` read from rows 2 and 3 of the tile matrix
(`GetSpotShadowProjection`); no near/far data is uploaded. The blocker search covers the emitter's
projection at the near plane, and the filter radius is the penumbra half-width at the receiver,
`lightRadius · (receiver − blocker) / blocker`. Both are converted to tile UV with `0.5 · P[0][0] /
depth`. The search is capped at 0.05 tile UV, the margin point-light faces keep, so taps never read
a neighbouring tile.

**Point lights shadow through the spot atlas.** A shadowed point light takes six consecutive atlas
tiles, one per cube face (+X, −X, +Y, −Y, +Z, −Z); `PointLight::ShadowIndex` is the first tile and
`AtlasScale` the tile size. Each face renders at 2·atan(1/0.9) ≈ 96°, so the face the shader picks
by the major axis (`PointShadowFace`) always lands in [0.05, 0.95] of its tile and filter taps
never cross a seam. The atlas holds `MaxShadowAtlasTiles` (64, `SHADOW_ATLAS_MAX_TILES` in
`Buffers.glslh`; change both) in up to an 8x8 grid: up to 16 spot lights first, then up to
`MaxPointShadows` (8) point lights scored by intensity and distance. Spot and point tiles share the
spot shadow pass, its per-tile frustum culling and its static/dynamic cache. The point-light buffer
is uploaded after this selection, because the selection writes the tile fields.

**Orthographic cameras are handled in every depth- and view-dependent shader.** `UBCamera` ends
with `IsOrthographic` (set when `Projection[3][3] == 1`); the GLSL `Camera` block in `Buffers.glslh`,
its inline copy in `ClusterBuild.glsl` and the HLSL `Camera` in `Include/HLSL/Buffers.hlslh` all
declare it, so edit the three together. For an orthographic camera `DepthUnpackConsts` is affine and
`NDCToView` is not multiplied by depth. `NDCToViewMul`/`NDCToViewAdd` are XeGTAO's: a [0, 1]
screen position (row 0 at the top) maps to view-space x right, y **up**, i.e.
`Mul = (2·tanX, −2·tanY)`, `Add = (−tanX, tanY)`. GTAO's slice stepping assumes that convention, and
SSR works in the renderer's view space with G-buffer normals as they are (no y flip), reprojecting
with `v = 0.5 − 0.5·ndc.y`. Linearize depth with `LinearizeDeviceDepth` and get the view
vector with `GetViewDirection` (both `Buffers.glslh`) rather than `u_Scene.CameraPosition -
position`. Cluster AABBs use parallel rays, and the cluster near plane is clamped to 0.01 because
an orthographic near plane can be zero or negative.

**The sky light's `Rotation`** (degrees about +Y) reaches the renderer through the frame packet
(`EnvironmentRotation`) into `SceneData.EnvironmentMapRotation`. Deferred and forward IBL add it to
any per-material rotation, and the skybox rotates by the same amount (`u_Uniforms.Rotation`), so
the sky and its lighting stay aligned. `LuxEvaluateImageBasedLighting` rotates the irradiance lookup
as well as the reflection lookup.

**The editor floor grid** (`Grid.glsl`, `SceneRenderer::GridPass`) draws a camera-centred square
on y = 0 over the composited image. Lines keep a constant pixel width and fade to their average
coverage below a pixel, the cell size steps by powers of ten with a crossfade, and the X and Z axes
have their own colours. Colours, widths, cell size and fade distance are
`SceneRenderer::GridSettings` (`SetGridSettings`, main thread). The editor keeps them as
`Editor.Grid.*` preferences (Application Settings → Viewport → Grid).

**Directional shadows use a normal-offset bias** in deferred lighting: the receiver moves along its
normal by 1.5 texels of its cascade (texel size from the cascade's orthographic matrix,
`2 / (|row 0| × resolution)`), scaled up at grazing angles. Forward passes compute cascade
coordinates in the vertex stage and keep the depth bias only.

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

**Cube maps are written as `image2DArray`, never `imageCube`.** A storage view of a cube is a 2D
array (NRI has no storage cube view, as D3D12 has none), and Vulkan requires the shader image to
match the view type. The face is the array layer, so `imageStore(img, ivec3(x, y, face), ...)` is
unchanged; `imageSize` returns an `ivec3`, so take `.xy`. Sampled cubes stay `textureCube`.
A compute pass that writes a cube face maps each invocation to its texel **centre**,
`(gl_GlobalInvocationID.xy + 0.5) / size` (`GetCubeMapTexCoord`, `PreethamSky.glsl`). The corner
is half a texel off, which on the small mips rough surfaces read aims a 1x1 face at a cube corner.

**Lookup tables are read at an explicit mip 0 and created without mips.** The BRDF LUT is indexed
by `(NdotV, roughness)`, not a screen UV, so derivative-based LOD picks arbitrary mips wherever
neighbouring pixels differ (silhouettes, normal-mapped detail) and bands there. Use
`SampleLinearLOD(lut, uv, 0.0)`, and `GenerateMips = false` on the texture. Any new LUT (LTC area
lights, for example) follows the same rule.

**sRGB colour mips are averaged in linear space.** `Texture2D::GenerateMips` on an `SRGBA` texture
(sRGB images cannot be storage images) copies mip 0 into an RGBA8 scratch image, builds the chain
with `LinearSampleSRGB.glsl` (decode, 2x2 average, encode), and copies it back.
`Texture2D::CreateFromSRGB` copies only mip 0 and regenerates this way, because the RGBA source's
chain was averaged on the encoded bytes, which darkens it. Mip sizes are `max(size >> mip, 1)` and
dispatches round up. A truncated `size / 8` leaves the edge texels of odd-sized levels unwritten.

**When a shader stops working after an edit, suspect the cache first.** Delete
`Resources/Cache/Shader/` and relaunch before debugging the shader itself. A stale registry entry
looks exactly like a broken shader.

**Uniform/storage buffer layout rules:** respect std140/std430 alignment. A `vec3` is 16-byte
aligned; a mismatch between the C++ `UB*` struct in `SceneRenderer.h` and the GLSL block is not a
compile error in either language — it is garbage in the shader. When you edit a `UBCamera`,
`UBScene`, `UBShadow`, `UBRendererData`, `UBScreenData`, `UBAtmosphere`, `CBGTAOData`, … struct,
edit the matching GLSL block in the same change.

---

## Screen-space Y convention — the viewport is flipped

Lux's viewports use NRI's default top-left origin (`originBottomLeft = false`), which NRI records
as a **negative-height** `VkViewport`, i.e. D3D convention: NDC `y = +1` is pixel row 0
(`gl_FragCoord.y = 0`, top of the image). The projection matrices are plain GLM (no Y flip), so the
rasterized image is upright. (NVRHI recorded the same viewport, so the convention predates NRI.)

Any code that converts a **pixel/UV coordinate to NDC by hand** must therefore use
`ndc.y = 1 - uv.y * 2`, not `uv * 2 - 1`. Getting this wrong mirrors the mapping vertically and
does not look like a flip — it looks like data from the wrong screen region (the clustered light
grid did exactly this: `ClusterBuild.glsl` built rows bottom-up while `SetupClusterLights` looked
them up top-down via `gl_FragCoord`, so far geometry near the top of the screen lost its lights).
Fullscreen passes that pass the quad's `a_Position.xy` straight through as clip position are
unaffected.

---

## Golden image capture

A renderer-critical change is checked against reference images, not by eye. The editor has an
environment-driven capture (`Editor/Source/Tools/GoldenCapture.{h,cpp}`, inert unless
`LUX_GOLDEN_DIR` is set). It opens one scene and pins everything that would otherwise change
between runs:

- render size (`LUX_GOLDEN_SIZE`, default 1280x720)
- camera: the scene's primary camera pose, re-applied every frame
- native resolution, because dynamic resolution follows GPU time
- no grid, colliders or editor overlays
- uncapped present mode

It waits until `LUX_GOLDEN_FRAME` frames have passed **and** asset streaming has been idle for
`LUX_GOLDEN_IDLE_FRAMES`. It then reads back `SceneRenderer::GetFinalPassImage()` inside a
`Renderer::Submit` lambda and writes `<name>.lximg` (a 32-byte header plus raw texels). After that
it averages `<name>.perf.json` over `LUX_GOLDEN_PERF_FRAMES`, writes `<name>.status`, and closes
the editor. `LUX_GOLDEN_OPTIONS` (e.g. `SSR=0,Bloom=0`) overrides individual renderer options.
`MoveEntities=N` moves the first N static-mesh entities (by UUID) around a 16-frame loop every frame
and captures only on a frame at the loop's start, so the image must match the unmoved capture while
the perf frames exercise the changed-mesh path. Besides the passes, `perf.json` lists three CPU
stages recorded with `SceneRenderer::RecordStageCPUProfile`: `RenderSceneSync` (the mirror sync,
carried in `FrameRenderPacket::MeshSyncMs`), `SubmitRenderScene` and `FlushDrawList`. Stages enclose
pass zones, so they are not added to `sceneRendererCpuMs`.

Drive it with the scripts, not by hand:

```bash
python3 tests/rendering/golden_run.py --label before --features          # capture (Release by default)
python3 tests/rendering/golden_run.py --label after --features
python3 tests/rendering/golden_run.py --compare before after              # per-capture PASS/FAIL + diff PNGs
python3 tests/rendering/golden_compare.py a.lximg --to-png a.png          # look at one capture
python3 tests/rendering/threading_ab.py                                   # build, then Single vs Multi
```

- **Captures live in `bin/golden/<label>/`** and are never committed.
- **Validation counts only exist in Debug captures** (`--config debug`, plus `--sync-validation`
  for synchronization validation). `summary.json` and `validation.txt` list them, and a comparison
  fails on any validation ID the reference set did not have.
- **Check determinism first.** Two captures of the same build must compare clean before a
  comparison across builds means anything. If they don't, find what still varies (automatic
  exposure is warned about, for example) and pin it in the capture. Never loosen the tolerance to
  hide it. Two captures can each be stable and still differ from each other. That pattern usually
  means an ordering decided by heap addresses. The transparent pass draws in the order of its sort
  keys, without a depth sort. Until 2026-10-16 its material key was the `Material` pointer, so
  Benchmark's two glass materials swapped blend order between runs. A sort key that changes what is
  drawn must come from a stable ID such as an asset handle.
- **A TIMEOUT with the editor sitting on its startup scene means it never saw `LUX_GOLDEN_DIR`.**
  The capture and `LUX_THREADING_POLICY` read the process environment through
  `FileSystem::HasEnvironmentVariable`. On Windows that helper used to read only the persistent user
  variables in `HKCU\Environment`, so until 2026-10-16 every scripted capture there timed out and
  `--threading` was ignored. A working run logs `[GoldenCapture] scene '…'` right after startup.
- **Perf numbers come from script-launched windows, so they are unfocused.** Compare them only with
  other script-launched runs (see "Present mode" below). `<name>.perf.json` averages the whole
  main-loop iteration (`frameMs`), GPU, main- and render-thread work, and every pass.
- **`--threading single|multi` forces the editor's threading policy** for those launches through
  `LUX_THREADING_POLICY`, without touching the user's setting. `threading_ab.py` builds (Windows:
  `Win-GenProjects.py --last`, then MSBuild), captures both policies alternately (`--runs`, default
  3), and prints per-scene medians plus an image comparison between the two.

---

## Resource states

Every GPU access needs its resource in the right state (layout + access). NVRHI's automatic
barriers are off for every `RenderCommandBuffer`, and the renderer's `ResourceStateTracker`
(`Renderer/RHI/`) places the barriers: it works out each transition (before and after state, per
run of subresources) and `NRIBarrierEmitter` records them as NRI barriers, batched until the
tracker commits. Each state's access, layout and stages are NVRHI's Vulkan ones, except that the
copy states wait on all commands (NRI's `COPY` stage covers copies only, not clears and fills).

The model:

- **Resting states.** Every resource has one, the state it is in whenever no command buffer is
  open, fixed when it is created: the desc's `keepInitialState` initial state (`ImageUsage::Texture`
  → `ShaderResource`, `Attachment` → `RenderTarget`/`DepthWrite`, `Storage` → `UnorderedAccess`,
  vertex/index buffers → `VertexBuffer`/`IndexBuffer`, GPU-only storage → `UnorderedAccess`,
  swapchain images → `Present`). A texture is created undefined: `NRITexture::Create` and
  `RHISwapChain::Create` record its first transition into its resting state in the upload batch
  (`UploadContext::InitializeTexture`), which is submitted before any command buffer that could use
  it (NVRHI did the same from `Common` on a texture's first command list). `RHISwapChain::Destroy`
  flushes the batch before it destroys the images, which would otherwise invalidate the batch and
  every upload in it. The tracker names a resource by its tracked description
  (`NRITexture`/`NRIBuffer::GetTracked`, `ImageInfo::Tracked`,
  `RHISwapChain::GetCurrentTrackedBackBuffer`), whose handle is the NRI texture or buffer.
  CPU-visible buffers are untracked. Each `RenderCommandBuffer` owns a tracker; resources enter it at
  their resting state and `RT_End` returns them there.
- **Where requirements come from.** NRI passes, draws and dispatches require what they bind:
  `RT_BeginNRIRenderPass` the attachments (`RenderTarget`, `DepthWrite`) and the pass's sets,
  `RT_BeginNRIDraw` the draw's set 0 and its vertex, index and indirect buffers, and
  `RT_BeginNRIDispatch` the dispatch's sets. A set's resources are its `DescriptorSetUses`, recorded
  by its owner while it writes the set (SRV → `ShaderResource`, UAV → `UnorderedAccess`, constant
  buffers), with the subresources NVRHI's binding sets required. Change detection is NVRHI's: a
  binding is re-required when it changes, after a copy/clear/write, when the bind point switches
  between graphics and compute, and always for sets with storage uses (which is what places UAV
  barriers between dispatches). Code that touches a resource outside a pass, draw or dispatch
  requires it first: `RT_RequireTextureState`/`RT_RequireBufferState` for copies, clears and
  writes, and `RT_TransitionTextureState`/`RT_TransitionBufferState` + `RT_CommitBarriers` for
  transitions the code always needed (mip chains, compute → indirect).
- **Readbacks** (`ReadbackTexture`, `Renderer/TextureReadback.h`: `Image2D`/`TextureCube::
  CopyToHostBuffer`) require the texture as `CopySource`, record `CmdReadbackTextureToBuffer` into a
  HOST_READBACK buffer, add a global barrier into host reads (the tracker has no host state, and a
  fence wait alone does not make device writes visible to the host), submit, and wait with
  `RenderCommandBuffer::RT_WaitForCompletion` (an NRI fence signalled after the submission). Rows
  come back tightly packed.
- **Bindless descriptor tables** are untracked, as in NVRHI (their group records no uses); material
  textures are in their resting `ShaderResource` state whenever a command buffer starts or ends.

**Never write a barrier by hand — ask the command buffer's tracker.** Copies and clears record
through NRI after their requirements, inside `RT_BeginNRITransfer` (`§ NRI device`), which commits
them. The one exception is the readbacks' barrier into host reads, a state the tracker does not
have. NRI validation (Debug) checks every barrier's access against the resource's usage.

---

## NRI device

Lux creates the `VkDevice` (`VulkanDevice`), and `RHIDevice::Init`
(`Renderer/RHI/RHIDevice.h`) wraps it with `nriCreateDeviceFromVKDevice`, so Aftermath and the Tracy
context see the same device NRI drives.

- **NRI reads feature support from the physical device, not from what Lux enabled** (Vulkan has no
  query for enabled features). Extension-backed features are safe: NRI only queries an extension's
  feature struct when the extension is in the list Lux passes. Core features are not: NRI uses
  Vulkan 1.3 `synchronization2` and 1.4 `maintenance5`/`maintenance6`/push descriptors on its own
  whenever the GPU has them. So `VulkanDevice::createDevice` enables every core feature NRI
  can use implicitly (and the 1.1/1.2 bits the descriptor work will need) whenever supported, and
  the KHR extensions on 1.3 devices. **Do not use an NRI `DeviceDesc` feature that
  `VulkanDevice` does not enable**; add it there first.
- **Extensions NRI expects in pairs.** NRI creates its own devices with some extensions together
  and loads their functions as if they always were: with `VK_KHR_ray_tracing_pipeline` it loads
  `vkCmdTraceRaysIndirect2KHR` from `VK_KHR_ray_tracing_maintenance1`, and `nriCreateDeviceFromVKDevice`
  fails when that is missing. `VulkanDevice` enables maintenance1 (and its features) with the
  ray-tracing extensions, and leaves the pipeline extension off on a device without it (Lux uses no
  ray-tracing pipelines). Mesh-shader statistics need `meshShaderQueries` (enabled when supported;
  fork patch LUX-2). Check `DeviceVK::ResolveDispatchTable` before enabling a new extension.
- **Surface extensions.** The fork loads a platform's surface function only when its instance
  extension is enabled, and NRI's swapchain calls the `get_surface_capabilities2` queries without
  checking that they loaded. `VulkanDevice` therefore requires
  `VK_KHR_get_surface_capabilities2` with a window, and `VK_KHR_xlib_surface` on X11, where GLFW
  enables only `VK_KHR_xcb_surface`.
- **One queue lock.** Every NRI `QueueSubmit` and `QueuePresent` holds
  `RenderCommandBuffer::LockQueue`, which also orders them against the cross-queue waits added for
  the next submission.
- **Swapchain** (`Renderer/RHI/RHISwapChain`, NRI migration Phase 14). Each window's swapchain (the
  main window, every ImGui platform window, the runtime player) is an NRI `SwapChain`, which owns
  the surface. `GetNRIWindow` (`Core/Platform/<OS>/<OS>NativeWindow.cpp`) gives NRI the HWND, or
  the Wayland or X11 handles, and only those files include the window-system headers.
  - **Size and format:** the window's framebuffer size, skipped at 0x0. NRI snaps it to the
    surface's extent. The format is NRI's `BT709_G22_8BIT`: 8-bit UNORM in the sRGB color space,
    RGBA before BGRA, so anything drawn to the back buffer takes its format from
    `GetNRIColorFormat`.
  - **Semaphores:** a ring of acquire semaphores, `max(images, maxFramesInFlight + 1)` long, so
    one is reused only after the frame that waited for it completed; one present semaphore per
    image.
  - **Out of date:** NRI returns `OUT_OF_DATE` from an acquire or present without logging it (fork
    patch LUX-3), and the swapchain flags `NeedsRecreate`. `Window::ProcessEvents` rebuilds the
    main window's; `ImGuiLayer::End` rebuilds a platform window's. NRI reports a suboptimal
    swapchain as success, so a resize is caught by the size callbacks.
  - **Presenting queue:** NRI presents from the queue that created the swapchain, the graphics
    queue, so with a window `FindQueueFamilies` picks a graphics family that can present.
- **Errors.** NRI messages go to the log as `[NRI] …` under the `Renderer` tag. An NRI error breaks
  into the debugger in Debug, like a failed assert, and only logs otherwise. NRI validation is on in
  Debug.
- **NRI owns GPU memory** (Phase 8). Every texture and buffer is an `NRITexture` / `NRIBuffer`
  (`Renderer/RHI/RHIResources.h`): an NRI committed resource described by an `RHITextureDesc` /
  `RHIBufferDesc` (Phase 15): NRI formats (`Utils::NRIFormat` for image formats), usage flags and
  a resting state. Attachments are EXCLUSIVE (DCC), other textures CONCURRENT, as NRI validation
  requires; a buffer's memory comes from `Access` (Write → HOST_UPLOAD, Read → HOST_READBACK, else
  DEVICE). Rules:
  - **Create GPU resources only with `NRITexture::Create` / `NRIBuffer::Create`.**
  - **CPU writes go through `NRIBuffer::Map`/`Unmap`** (CPU-visible buffers only).
    GPU-side writes are NRI copies from staging memory (`§ Batched resource uploads`). `Map` never waits for
    the GPU, unlike NVRHI's `mapBuffer`, which waited for copies that used the buffer. A readback
    buffer must be mapped only after the fence of the copy into it.
  - **Destruction is the owner's job**: destroying or reassigning an `NRITexture`/`NRIBuffer` frees
    the NRI resource and its views through `Renderer::SubmitResourceFree`. A raw `nri::Texture*`
    or `nri::Buffer*` keeps nothing alive, so nothing may use one after its owner is gone.
- **Descriptors** (NRI migration Phases 9 and 12):
  - **Pipeline layouts**: one per `RHIShader`, built from its reflection when its input
    declarations are (`CreateDescriptors` → `GetNRIPipelineLayout`):
    - register space = set number;
    - one NRI set per non-empty set (`GetNRISetIndex(set)`, `k_NoNRISet` for an empty one);
    - all stages;
    - set 4 is the shared bindless range;
    - push constants are root constant 0.

    A material's set can be bound in a pipeline's layout when `RHIShader::IsNRISetCompatible`:
    the same ranges (and, for set 0, the same root constants).
  - **Descriptor sets**: a `DescriptorSetGroup` (`RHI/DescriptorSetGroup.h`) is a pool holding
    one set per frame in flight, and what each set accesses (`DescriptorSetUses`, for the barrier
    tracker). It is written while it is built and **never rewritten**.
    `DescriptorSetManager::BakeSet` replaces the set's group on every bake, and the old group is
    freed through the GPU deletion queue. `InvalidateAndUpdate` rebakes a set when an input's NRI
    resource (buffer, texture or sampler descriptor) differs from the one it was baked with.
    Each `BindlessTextureTable` keeps one group and writes a frame's set only while no frame in
    flight reads it (`RT_GetDescriptorSet`).
  - **Views**: `NRITexture::GetNRITextureView(key)` and `NRIBuffer::GetNRIBufferView` are an
    on-demand cache keyed per resource, like NVRHI's own view cache. They are destroyed with the
    resource. A `Sampler` owns its NRI descriptor (`GetRHIDescriptor`).
  - **No combined image samplers.** NRI has none, so shaders declare `texture2D` plus a `sampler`
    and combine them at the sample (`sampler2D(u_Tex, r_Sampler)`). Reflection logs an error for a
    combined `sampler2D` uniform.
- **Command buffers and submission** (NRI migration Phase 13). A `RenderCommandBuffer` records into
  NRI command buffers from its `CommandBufferPool` (`Renderer/RHI/`): one per frame-in-flight slot
  to start with, each with its own allocator, reused once the GPU has finished its last submission
  and grown, like NVRHI's command lists, when all are in flight. `RHIQueueTimeline` submits under
  `RenderCommandBuffer::LockQueue` and keeps one NRI timeline fence per queue: a submission's value
  is what `GetLastExecutionInstance` returns, what `Renderer::QueueWaitForCommandList` makes another
  queue wait for (added waits go with that queue's next command buffer submission, not with the
  upload batch flushed ahead of it), what `RT_WaitForCompletion` blocks on, and what frame pacing
  (`Renderer::RT_BeginFrame`, `RHISwapChain::Present`) waits for. A binary semaphore wait is
  passed with the submission that needs it: the swapchain hands out its acquire semaphore once per
  frame (`TakeAcquireSemaphore`), to the first command buffer that draws to the back buffer, and
  `Present` waits for it itself when nothing did, in the signal-only submission that also signals
  the present semaphore. A queue the device does not have (no compute family) records on graphics.
- **Recording.** Every draw, dispatch, barrier, copy, clear, upload, readback, query and marker is
  recorded with NRI (NRI migration Phases 9-13).
  - **Segments.** `RenderCommandBuffer::RT_BeginNRISegment` / `RT_EndNRISegment` hand out the open
    command buffer for raw NRI recording. A segment starts with the pending barriers committed.
  - **The NRI objects.**
    - Every `Pipeline` has an NRI pipeline (`GetNRIPipeline`), built from its specification for the
      target framebuffer's attachment formats and sample count; a shader with a mesh stage makes a
      mesh-shader pipeline. `PipelineCompute` has a compute one.
    - Every framebuffer has NRI attachment views (`GetNRIColorAttachment`/`GetNRIDepthAttachment`,
      rebuilt by `RT_Invalidate`); there is no NVRHI framebuffer, and a sampler is only its NRI
      descriptor.
    - Meshes have meshlet sets (`MeshSource::RT_GetOrCreateMeshletSet`).
  - **Passes.** `Renderer::BeginRenderPass` (and the meshlet pre-depth pass) open an **NRI render
    pass** (`RenderCommandBuffer::RT_BeginNRIRenderPass`):
    - its clears become load ops;
    - the pipeline, layout, viewport, line width, shading rate and the pass's sets are bound once
      per rendering scope;
    - the draws (`Renderer::RT_DrawIndexed` / `RT_DrawMeshTasks`, through `RT_BeginNRIDraw`) bind
      only set 0, the vertex and index buffers and the root constants, and repeats are skipped.
  - **Dispatches.** `Renderer::DispatchCompute` binds the pass's sets and the material's at set 0
    through `RT_BeginNRIDispatch`, inside a segment.
  - **Copies and clears** (NRI Phase 13) record inside `RT_BeginNRITransfer`, which closes an open
    rendering scope and opens a segment: `Renderer::CopyImage` and `Texture2D::CreateFromSRGB` are
    `CmdCopyTexture` (`CopySource` → `CopyDest`), and buffer zero-fills are `CmdZeroBuffer`
    (`CopyDest`). `Renderer::ClearImage` clears one mip of a storage image through a
    `StorageImageClear`: NRI clears storage only through a view held by a bound descriptor set
    (`CmdClearStorage`), so the clear owns a one-set group with that view, built once, and binds
    it with a shared one-set compute layout (`StorageImageClear::Init`). The image is cleared in
    `UnorderedAccess`. A clear keeps the texture it was built for: owners rebuild it with the
    image's other views (resize, re-aliasing). Non-storage images have no clear; attachments clear
    through their pass's load ops.
  - **Rendering scopes split for work that cannot record inside them.** These close the scope
    first: dispatches, copies and clears (`RT_BeginNRITransfer`), and `RT_CommitBarriers` with
    barriers pending. The next draw reopens it, with the attachments loaded and the pass state
    rebound. That is NVRHI's implicit render-pass splitting made explicit. Barriers a draw needs are
    committed between two scopes (`ResourceStateTracker::HasPendingBarriers`). Debug markers
    (`CmdBeginAnnotation`) and timer queries (timestamps) are legal inside a scope and do not close
    it. In Debug, every split is reported once per pass and reason (`NRI render pass
    '<pass>' (<command buffer>) is split by <reason> inside it`): the inside-rendering audit. Each
    split stores and reloads the attachments, so a reported command belongs before the pass or
    after it.
  - **Skipped work.** A pass, draw or dispatch NRI cannot record is skipped, logged once per
    pipeline or shader as `'<pass>' is not rendered: <reason>`; a pass that does not open skips its
    draws. Read-only depth, 3D attachments, triangle fans and stencil are among the reasons (Lux
    uses none of them).
  - **The line-width exception.** NRI has no line-width state. The fork's LUX-1 patch makes it
    dynamic on line pipelines, and `RenderCommandBuffer::RT_OpenNRIRendering` sets it with the one
    raw `vkCmdSetLineWidth` in the renderer.
- **ImGui** (`ImGuiRenderer::RenderToSwapchain`). The UI and the platform windows draw into their
  swapchains through an NRI render pass.
  - **Textures are `Image2D`s**, the font atlas included, registered with a resolved subresource
    range. Each draw samples a one-layer 2D view of that range.
  - **Per frame slot:** host-visible vertex and index buffers written through `NRIBuffer::Map`, and
    descriptor pools reset when the slot comes round again. A full pool is skipped (counted, so
    NRI never reports it exhausted), and the next one doubles in size.
  - **Every drawn image is required before the pass opens**, so the pass never splits; its sets
    record no uses.
  - **Pipelines are per swapchain format.** The main window's is made in `Init`; a platform window
    of another format gets its pipeline when first drawn.
  - **A frame the UI cannot be drawn into** (no pipeline, view or set range, logged once; or a
    failed geometry upload, logged by the buffer's creation) still waits for the swapchain acquire,
    and presents what the back buffer holds.

---

## Raw Vulkan outside `Platform/Vulkan/`

`Platform/Vulkan/` is the only place that talks Vulkan: `VulkanDevice` (instance, device, queues,
the device NRI wraps), `VulkanGPUProfiler` (Tracy's context), `Debug/` (Aftermath) and
`ShaderCompiler/` (the SPIR-V toolchain). Everything else goes through NRI. The exceptions, each
with its reason; add to this list rather than growing it silently:

| Where | What | Why |
|---|---|---|
| `Core/Window.cpp` (`CreateRHIDevice`) | `VkInstance`/`VkPhysicalDevice`/`VkDevice` casts | Hands Lux's device to `nriCreateDeviceFromVKDevice` |
| `Core/Window.h` | `#include <vulkan/vulkan.h>` before GLFW | GLFW declares its Vulkan functions (`glfwGetPhysicalDevicePresentationSupport`) only when Vulkan is included first |
| `Renderer/RenderCommandBuffer.cpp` | `vkCmdSetLineWidth` on the native command buffer | NRI has no line-width state (fork patch LUX-1; D15) |
| `Renderer/RenderCommandBuffer.{h,cpp}` | `VkCommandBuffer` for `TracyVkCollect` / `tracy::VkCtxScope` and `Aftermath::SetCheckpoint` | Tracy's and Aftermath's APIs take the native command buffer |
| `Renderer/RHI/RHITypes.cpp` | `static_assert`s on `VkDrawIndexedIndirectCommand` and `VkDescriptorBufferInfo` | The GPU reads indirect draws in that layout; reflection files store the old descriptor-info layout |

## Validation errors are bugs

Vulkan validation output (`VulkanDevice::vulkanDebugCallback`) is not noise. A validation
error means a real object-lifetime, layout, or synchronisation mismatch that will manifest as a
crash or corruption on some driver even if it renders correctly on yours.

- Do not silence, filter, or `#ifdef` away a validation message to make a log quiet.
- Fix the underlying mismatch: image layout, descriptor lifetime, pipeline/renderpass compatibility,
  or missing barrier.
- For frame-indexed storage buffers, pass the `StorageBufferSet` to
  `PipelineCompute::BufferMemoryBarrier`; it resolves `RT_Get()` when recording, just like the
  descriptor sets. Main-thread `Get()` may select a different buffer. Indirect draw consumers need
  `ResourceAccessFlags::IndirectCommandRead` (`ResourceState::IndirectArgument`), not a shader-read
  state; the barrier is an `RT_TransitionBufferState` on the command buffer's tracker.
- GPU-written storage (including mesh-culling visible indices, indirect arguments and
  `ObjectIndexes`, which the early mesh cull writes) must use `GPUOnly = true`. CPU-visible buffers have no tracked state (the tracker's `DescribeBuffer` gives
  them no resting state), so no barrier can protect them; CPU initialization
  of GPU storage goes through `UploadContext::WriteBuffer` in the upload batch, or
  `RenderCommandBuffer::RT_WriteBuffer` in a command buffer.
- **Main-view cull groups (GPU LOD).** With GPU-driven rendering on and the mesh-shader prepass not
  in use (`SceneRenderer::CanUseGPUCullGroups`), every proxy instance joins a `CullGroup` keyed by
  mesh, submesh and (transparent only) material key, with `MeshKey::GPUCullGroup` set. The CPU does
  no main-view culling or LOD for them; groups persist across frames and are rebuilt only when the
  submitted scene's change list or the submit-cache epoch moved. `FlushDrawList` expands each group
  into one draw per LOD (`ExpandCullGroupDraws`) with its own visible range and indirect args, and
  `MeshCulling.glsl` picks each instance's LOD with `SelectStaticMeshLOD`'s metric. **Edit the LOD
  metric in both places**, or the GPU and CPU paths pick different LODs. An early dispatch of the same
  shader (frustum and LOD, no HZB, `EarlyPass`) writes the depth prepass's instances into
  `ObjectIndexes`; the prepass draws cull-group draws indirect from it, so prepass and G-buffer
  agree on every instance's LOD (the G-buffer depth-tests EQUAL). Selection outlines, shadows and
  non-proxy submissions keep the CPU path. `MeshCullDrawData` is three `uvec4` per cull draw and
  mirrors the shader's block. **The LOD metric lives in `Include/GLSL/MeshLOD.glslh`**, shared by both
  cull shaders.
- **Shadow cull groups.** A cull-group instance that casts shadows and is static (its proxy has not
  moved, `StaticMeshRenderProxy::LastMovedFrame`, for `StaticShadowCasterStableFrames` syncs) joins a
  `ShadowCullGroup` (mesh, submesh); the groups rebuild on the frame a caster settles. A caster still
  moving keeps the CPU shadow path (exact lists, `UpdateShadowCasterMotion`). `ShadowCulling.glsl`
  runs before the shadow passes only when a static cache re-renders, and appends each instance to its
  (cascade or spot atlas, LOD) slot, testing the CPU's own `Frustum` planes (packed after the cull
  records) and the tiny-caster cull of `ShouldCullTinyDirectionalShadowCaster`; the static caches
  draw those slots indirect. These casters are not in `CalculateShadowCasterHashes`: the static caches
  re-render when the static caster set changes or when what the GPU cull reads from the camera
  changes (`ShadowCullCameraState`: position, tiny-caster pixels per radian, LOD settings). **Edit the
  tiny-caster rule in both places.** On a frame with the groups kept, `SubmitRenderScene` skips every
  non-selected proxy they cover.
- PCSS reads its taps from the `RendererData` uniform buffer (`ShadowKernel`), never from a local
  array. Dynamic indexing of a local sample array expands into repeated per-fragment scratch arrays
  on RADV Renoir and can cause a GPU timeout.
  `python3 tests/rendering/run_shadow_shader.py` compiles/validates deferred lighting and checks
  its SPIR-V for these local copies (use `--sdk-bin` if the Vulkan tools are not bundled).
- Nvidia Aftermath GPU crash dumps (`Platform/Vulkan/Debug/Aftermath.{h,cpp}`) are active on NVIDIA
  in Debug and Release, and compiled out of Dist and `--no-aftermath`. The library is loaded at
  runtime, so the engine starts normally without it. A device loss (`DEVICE_LOST` from a queue
  submit, acquire or present) goes through
  `VulkanDevice::ReportDeviceLost`, which waits for the dump, logs its path and aborts.
  Dumps go to `<log directory>/GPUCrashDumps/`, with the last `RT_BeginMarker` label per command
  buffer in `.markers.txt` (Vulkan checkpoints store only a pointer, so names are resolved in-process).
  Shaders are registered for address mapping in `RHIShader::LoadAndCreateShaders`.

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

1. the engine's own NRI timestamp queries, read back by the Renderer Debugger's Pass Timings table, and
2. a **Tracy GPU zone**, so the same pass appears on Tracy's GPU timeline.

So wrapping a new pass in `BeginProfiledGPU` is all that is needed — do not add Tracy GPU zones by
hand. The `TracyVkCtx` is a `VulkanGPUProfiler` that `VulkanDevice` owns (created in
`CreateDevice`, destroyed in `DestroyDevice` before `vkDestroyDevice`) and reached via
`GetGPUProfilerContext()`, which may be
null if creation failed; every consumer must tolerate that.

Two rules if you touch this path:

- **A GPU zone may never outlive its command buffer's recording state.** `~VkCtxScope` issues a
  `vkCmdWriteTimestamp`, so a zone closed after `close()` is a Vulkan usage violation. `RT_End`
  therefore force-closes any zone still open before collecting.
- `TracyVkCollect` runs in `RT_End`, outside any render pass and before `close()`.

The queries (NRI migration Phase 13) live in one `QuerySlot` per command list slot: a TIMESTAMP pool
(2048), a PIPELINE_STATISTICS pool (graphics queues, one query per segment) and a HOST_READBACK
results buffer. The slot's first `RT_Begin` of a render frame publishes the slot's previous frame and
resets the pools; every `RT_End` copies what it wrote (`CmdCopyQueries`) and adds a barrier into host
reads. Reading the previous frame then is safe because frame pacing has retired it, which is why
queries need one command list per frame in flight (`Create(0, …)`; others get none, with a
warning). A timer name used several times in a frame reports its total.

The frame total (`GetExecutionGPUTime`, shown as "GPU" in the viewport HUD and read by dynamic
resolution) is a separate frame-level timestamp pair written in `RT_Begin` and `RT_End`. A frame
that is split mid-way — End, Submit, then Begin again, as the WorldOverlay2D pass does — must resume
with **`Begin(/*continueFrame*/ true)`**. Each segment then gets its own pair (and pipeline
statistics) and the published total is their sum. A plain `Begin()` there starts a new measurement
and the total covers only the tail.

The HUD's "CPU" figure is `max(MainThreadWorkTime, RenderThreadWorkTime)`, and the render-thread
timer includes `Present`'s frames-in-flight wait. On a GPU-bound frame it therefore reads high;
confirm CPU-bound with a sampler or Tracy before optimizing CPU code.

Tracy GPU zones only carry data while a profiler is connected (`TRACY_ON_DEMAND`), and the whole
path compiles out in Dist. Note that Tracy captures record **CPU zones only** unless this context
exists — a capture with zero GPU zones means the context failed to create, not that the GPU is idle.

---

## Present mode, and why frame rate is not a render-cost question

Measured on Windows/DWM, RTX 4070 Ti, 60 Hz display, windowed editor:

| Present mode | Window focused | Window unfocused |
|---|---|---|
| `FIFO` (VSync on) | refresh (60) | refresh |
| `MAILBOX` | **60 — refresh** | **120 — ~(images-1) x refresh** |
| `IMMEDIATE` | **uncapped** | uncapped |

**MAILBOX does not mean uncapped.** On a composited surface the presentation engine still releases
images on the compositor's schedule, so the frame rate tracks the display while the window is
focused. `IMMEDIATE` is the only mode the spec guarantees will not block on vblank, and is the only
way a windowed surface exceeds the refresh rate. Selected by
`DeviceCreationParameters::preferImmediatePresentMode` (editor: Application Settings → Present Mode),
applied through `Window::SetPreferImmediatePresentMode` on the deferred swapchain-recreate path.
`RHISwapChain` turns the settings into NRI flags: VSync is `VSYNC` (FIFO), IMMEDIATE is
`ALLOW_TEARING`, and MAILBOX is no flag. NRI falls back to FIFO when the surface lacks the mode
(the raw-Vulkan swapchain tried the other uncapped mode first). Never set `ALLOW_TEARING` with
`VSYNC`: NRI would pick FIFO_RELAXED.

Two consequences that cost real debugging time:

- **A frame rate pinned to the refresh rate, or to a clean multiple of it, is a presentation
  ceiling, not a GPU cost.** Lowering quality settings will not move it — that is the diagnostic. Do
  not go hunting in the pass timings until the ceiling is ruled out.
- **Window focus changes the frame rate by 2x.** Any benchmark of a script-launched (therefore
  unfocused) editor window is measuring a different regime than the one users see, and its absolute
  numbers are not comparable to a focused session. Present-mode A/B tests in particular *must* be
  run focused; an unfocused comparison inverted this result once already.

Frame pacing below the ceiling is `Application::SetTargetFrameRate` (editor: Frame Rate Limit), a
deadline-carried sleep-then-spin in the main loop. It can only ever slow the loop down, so a target
above the refresh rate additionally requires `IMMEDIATE`.

---

## Batched resource uploads and the async transfer queue

Buffer/texture constructors record initial-data uploads into **one shared command buffer** via
`Renderer::RecordResourceUpload(record)`, flushed by `Renderer::FlushResourceUploads()` before every
`RenderCommandBuffer` submission. The reason is in the header: a `vkQueueSubmit` per mesh/texture is
a load hitch and contends the graphics queue against the render thread.

The callback gets an `UploadContext` (`Renderer/RHI/UploadContext.h`) and runs synchronously under
the batch lock, so callers may free their CPU data on return, and must not start another upload from
inside it. `WriteBuffer` and `WriteTexture` stage the data, require the destination as `CopyDest`
through the batch's own tracker, and record `CmdCopyBuffer` / `CmdUploadBufferToTexture`
(texture rows are repacked at NRI's row alignment; mips and layers are written one at a time). The
flush returns every resource to its resting state, submits, and retires the staging memory.

**Staging memory** is a `StagingStream` (`Renderer/RHI/StagingMemory.h`): 4 MiB HOST_UPLOAD pages
handed out linearly (larger uploads get a page of their own), recycled through the deletion queue
once the frame that submitted their copies completes. The batch and every `RenderCommandBuffer`
own one; `RenderCommandBuffer::RT_WriteBuffer` (GPU-only `StorageBuffer::RT_SetData`,
`VertexBuffer::RT_SetData`) stages into the command buffer's stream, retired at `RT_Submit`.
`Renderer::Init` creates the pool's first pages.

When enabled *and* the device has a dedicated transfer queue, the batch is submitted on the copy
queue: `SetAsyncTransferQueueEnabled` / `IsAsyncTransferQueueEnabled` / `UseAsyncTransferQueue`
(runtime toggle `Renderer.AsyncTransferQueue`). Cross-queue ordering is then explicit —
`Renderer::ConsumePendingUpload(consumingQueue, outInstance)` and
`Renderer::QueueWaitForCommandList(waitQueue, executionQueue, instance)`.

**If you add a new consumer queue, you must wait on pending uploads.** Skipping the wait produces
reads of not-yet-uploaded memory — intermittent, GPU-dependent, and very hard to attribute.

The batch's emitter knows its queue (`NRIBarrierEmitter::SetQueue`). On the copy queue its
barriers keep only copy accesses and use ALL stages, which Vulkan expands to that queue's transfer
stages; the layout change to the resting state stays. A transfer-only queue cannot name a shader
read, so visibility to the reading queue comes from its timeline-semaphore wait, not the barrier.
The path is still not fully valid: attachment textures are EXCLUSIVE with no queue-ownership
transfer (other textures and all buffers are CONCURRENT). That is why the toggle is off by default.

---

## Feeding the renderer from the scene

Renderer submission must go through the frame packet, not the live ECS:

- `Scene::BuildRenderPacketEditor` / `BuildRenderPacketRuntime` capture everything the renderer needs
  into a `FrameRenderPacket` (`Renderer/FrameRenderPacket.h`).
- `Scene::SubmitRenderPacket` replays that packet without touching the registry.
- `Scene::SyncRenderScene` maintains the persistent render-side `RenderScene` /
  `StaticMeshRenderProxy` / `GPUScene` state, with dirty flags. Each frame it passes every drawable
  entity's `StaticMeshSyncInputs` (mesh handle, component material table, world transform,
  visibility, selection) to `RenderScene::SyncStaticMesh`, which compares them with the retained
  proxy **in place**: an unchanged entity costs a few compares, and meshes, bounds and the
  material-table hash are only recomputed when their inputs changed (`MaterialTable::GetRevision`).
  The compare runs against a dense record of last frame's inputs, kept in sync order, so an
  unchanged entity never reads its proxy. Material tables are not read per entity either: an edit
  to any table bumps `MaterialTable::GetGlobalRevision()`, and that frame every entity takes the
  full path. **So a `MaterialTable` may only change through its setters.**
- `SceneRenderer::SubmitRenderScene` caches each proxy's camera-independent submit data (resolved
  material handle and asset, transparency, shadow casting, sort keys) in `m_ProxySubmitCache`,
  indexed by `RenderPrimitiveID`. An entry is reused while the proxy's `Revision` holds; every entry
  is dropped when the scene (`RenderScene::GetGeneration`, unique per instance and `Clear`), any
  material table (`MaterialTable::GetGlobalRevision`) or any material asset
  (`MaterialScene::GetAssetRevision`) changes. Anything new the submit reads from a material or mesh
  must be covered by one of those signals, or a cached proxy keeps the old value.
  Entities not synced in a frame are dropped in `EndSync`. `GetChangedPrimitives` /
  `GetRemovedPrimitives` list what changed this frame.
- **A `RenderScene` mirror feeds exactly one `SceneRenderer`.** Each renderer uploads only the
  `GPUScene` rows dirtied since the previous sync, so two renderers syncing one mirror in the same
  frame would each miss what the other's sync consumed (the second sees stale transforms). `Scene`
  therefore keeps one mirror for editor-camera views (`SyncRenderScene`) and one for the
  primary-camera path (`BuildRenderPacketRuntime`); a third view needs a third mirror.

**New renderer-visible state belongs in the packet.** Reading the ECS during submission works today
and is exactly what blocks the simulation-thread split later (see `.claude/docs/Threading.md`).

---

## The GPU material table

Every drawn material is one row of `GPUMaterialData` (`Renderer/MaterialScene.h`), uploaded to the
std430 storage buffer `GPUMaterials` at `(set 2, binding 7)` and read by `GPUMaterial` in
`Include/GLSL/MaterialScene.glslh`. The two structs are one layout: **edit both in the same change**,
keep every member 16-byte aligned (`vec4` / `uvec4` only), and keep the `static_assert` on the C++
size in step. Textures are bindless indices into `u_GPUMaterialTextures` `(set 4, binding 0)`.

### Bindless textures — set 4 is reserved

`u_GPUMaterialTextures[]` is an unsized array backed by NRI descriptor sets
(`Renderer/BindlessTextureTable.h`). **Descriptor set 4 is reserved for it: no other shader resource
may use set 4.** Its range is process-wide (`BindlessTextureTable::Init`, called by `Renderer::Init`
before any shader loads); every NRI pipeline layout that declares set 4 uses that range
(`BindlessTextureTable::GetNRIRange`) instead of reflecting one, and `DescriptorSetManager` (sets 0–3)
never sees it. Capacity is
`BindlessTextureTable::MaxCapacity` (16384, mirrored by `GPU_TEXTURE_SCENE_MAX_TEXTURES` in
`MaterialScene.glslh` — change both together), clamped to the device's sampled-image limits and
logged at startup.

Tables are per owner and per frame in flight: each `SceneRenderer` owns a `BindlessTextureTable`
(its own texture index space — the viewport, material preview and thumbnailer do not share one), and
that holds one NRI descriptor set per frame in flight (`§ NRI device`). The bindless range is
partially-bound but **not** update-after-bind, so a set may only be written while no in-flight
frame reads it. `SetSlot` (main thread) records a write; `Flush` hands the writes to the render
thread, which queues them for every frame's set and writes the current frame's; other sets catch
up when their frame index comes round. Writes are skipped when the slot's texture is unchanged,
and full sweeps resend every slot so hot-reloaded textures (same `Ref`, new image) update.
`RenderPass::SetBindlessTextures` gives a pass its table; `RenderPass::GetDescriptorSet` then
returns the current frame's set for set 4 whenever the pass's shader declares it. Unwritten
slots are legal because the owner writes every index it hands out (slot 0 is the fallback). The
shaders that include `MaterialScene.glslh` must enable `GL_EXT_nonuniform_qualifier` (they already do)
for the unsized array.

**Colour maps take sRGB slots.** A slot is a `GPUTextureSlot`, a texture asset plus a
`TextureColorSpace` (`Renderer/TextureScene.h`). Texture assets load as RGBA8 UNORM. The albedo and
emissive maps are resolved with `TextureColorSpace::SRGB`, and `SceneRenderer` binds those slots to
`MaterialAsset::GetSRGBTexture`, the shared sRGB copy that the per-material `u_AlbedoTexture` also
uses. The sampler decodes them, so shaders never call `ToLinear` on a material texture sample. A
texture used both as colour and as data takes two slots. Data maps (normal, metalness, roughness,
occlusion, height) stay `Linear`. Only RGBA8 sources get a copy: float and block-compressed textures
are bound as they are.

Rows are built by `MaterialScene::BuildGPUMaterialData` from the `MaterialAsset` (its own values and
`MaterialSurfaceParameters`), never from the shader push-constant block. A new material input must
default to the value that reproduces today's shading, so an older `.lmat` renders unchanged.

**Rows and texture slots are reference counted, not touched every frame.** Each GPU instance holds
a reference on its material row (`MaterialScene::AcquireMaterial` / `ReleaseMaterial`, the ID in
`Metadata.z`), and each row holds references on its texture slots (`TextureScene::AcquireTexture` /
`ReleaseTexture`). `EndSync` frees what nothing holds, so a material or slot ID stays stable while it
is used, including on invisible proxies. A row is rebuilt only when its `MaterialAsset` object
changes (a reload) or `MaterialAsset::GetRevision()` does, so **every `MaterialAsset` setter must bump
the revision** — a setter that does not leaves the GPU row stale until the next reload.
`UpsertOverrideMaterial` rows are unreferenced and live while they are upserted every frame.

**Emission does not go through the G-buffer.** `GBuffer_Static` writes emissive radiance straight
into scene color (the G-buffer framebuffer borrows the scene-color image as attachment 5, with
`AttachmentLoadOp::Load` so the sky drawn before it survives), and `DeferredLighting` blends its
result on top with `FramebufferBlendMode::Additive`. Consequences: anything that writes the lit
opaque color must stay additive over what is already there, and the G-buffer render-graph pass reads
and writes scene color.

`Renderer::BeginRenderPass` honours a per-attachment `AttachmentLoadOp` (`Load` keeps, `Clear`
clears, `Inherit` follows `ClearColorOnLoad`). Use it to share an image into a framebuffer without
clearing it.

**Sampling the table requires an extension the headers do not enable.** `Samplers.glslh` indexes
`u_GPUMaterialTextures` with `nonuniformEXT`, but the include does not declare the extension —
every shader that calls `SampleMaterialSceneTexture` declares it itself, before its includes:

```glsl
#extension GL_EXT_nonuniform_qualifier : enable
```

Omitting it fails that stage with `'nonuniformEXT' : no matching overloaded function found`. The
failure is worse than it looks: if only one stage fails, the other still compiles fresh while the
failed one falls back to its **cached binary**, and a new vertex stage feeding an old fragment
stage is an interface mismatch that takes the editor down after pipeline creation. A shader compile
error in the log is therefore never something to run past — and `Resources/Cache/Shader/` must be
deleted after editing a widely-included `.glslh`.

---

## Editor debug geometry — colliders and category views

Collider wireframes travel as `FrameRenderPacket::ColliderDebugItem`s and draw in
`SceneRenderer::GeometryWireframePass` from the `PhysicsCollider` mesh pass, one draw bucket per
material. All of it is editor-only (`EnableEditorRenderTargets`). An item's `DebugCategory` (an
index into `Renderer/DebugPalette.h`'s `DebugCategoryPalette`, -1 for the normal collider colours)
selects one of the `m_DebugCategoryMaterials` created once in `Init()`, so a category view never
creates a material or pipeline per frame. With `SceneRendererOptions::ShowDebugCategories` the
packet carries category items only, and each is drawn twice: filled and alpha-blended through
`m_DebugCategoryFillPass` (same `Wireframe` shader, solid fill, back-face culled, depth-tested with
the default `GreaterOrEqual` so a fill lying exactly on the scene surface still passes), then
outlined through the collider pass. The renderer knows only indices; which category means what
(the acoustic material view) is decided by the scene when it builds the packet.

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
