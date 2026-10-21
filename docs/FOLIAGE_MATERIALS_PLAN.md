# LuxEngine Foliage Materials Plan

Finishes alpha-tested and two-sided materials across the renderer, so foliage (Sponza's plants
first) keeps its shape at a distance, glows when back-lit, and every alpha-tested or two-sided
surface renders the same in every pass. It is a planning document, not a description of what
exists. `.claude/docs/Architecture-LuxEngine.md` (the Cutout and Two-sided bullets) and
`.claude/docs/Rendering.md` (the raster-variant rule) describe what is built.

**Goal card**

- **Goal:** alpha-tested and two-sided materials render correctly and look good in every pass,
  starting from the work in `fbe55045`, `cc89434f` and `9a231480` (glTF import, two-sided culling,
  alpha-tested shadows).
- **Success:** Sponza's plants keep their leaf coverage as the camera backs away; back-lit leaves
  transmit light; the prepass, G-buffer and shadows agree on every alpha-tested pixel; double-sided
  transparent surfaces show their back faces in a stable order; with mesh shaders on, cut-out and
  two-sided surfaces match the classic prepass; the prepass cost is measured before and after.
- **Non-goals:** skinned meshes (the engine has no skeletal animation); importing glTF
  `KHR_materials_diffuse_transmission` (follow-up); a transmission colour texture (follow-up);
  alpha-to-coverage; anything temporal.
- **Constraints:** existing `.lmat` / `.luxscene` / `.luxproj` files load and render unchanged; no
  new installs; correct under both threading policies; works in Lux-Runtime, Dist and exported
  games; one PR for all of it (the user's choice).

**Decisions this plan is built on**

| Decision | Choice | Consequence |
|---|---|---|
| Coverage at distance | Coverage-preserving mips (Castaño), user's choice | One extra sRGB texture per (Cutout albedo, cutoff); no per-pixel cost |
| Coverage solve | 256-bin alpha histogram per mip on the GPU | Exact for 8-bit alpha, no readback, no binary search |
| Translucency model | glTF `KHR_materials_diffuse_transmission`, user's choice | Energy-conserving; a later importer maps 1:1 |
| Translucency storage | Factor in G-buffer `normal.w` (`Reserved0`, read by nothing today); colour from the material row via `MaterialID` | No new G-buffer target; no colour texture in v1 |
| Two-sided transparency | Back faces, then front faces, per draw (HDRP's "Back Then Front") | Two draws per two-sided transparent; new front-face-culling pipeline option |
| Mesh-shader prepass | Parity with today's classic prepass | The depth-only split (Phase 6) then applies to both |
| Depth-only prepass | Measured with `/profile` first; editor launch allowed for it | Kept only if it wins |
| PRs | One branch, one PR into `dev`, a commit per phase | Large review; phases stay separable by commit |

---

## Part 0 — Where we are

| Capability | State | Evidence |
|---|---|---|
| glTF MASK / BLEND / doubleSided import | ✅ built, user saw Sponza render correctly | `AssimpMeshImporter.cpp` `ImportAssimpMaterials` |
| Cutout alpha test in prepass + G-buffer | ⚠️ built, **UV bug** | `PreDepth.glsl` samples `a_TexCoord`; `GBuffer_Static.glsl` samples `(u, 1 − v)` |
| Alpha-tested shadows | ⚠️ built, **same UV bug** | `DirShadowMap_Masked.glsl` / `SpotShadowMap_Masked.glsl` copy PreDepth's UVs |
| Two-sided opaque (prepass, G-buffer) | ✅ built, user confirmed | `MeshKey::TwoSided`, `m_PreDepthTwoSidedPipeline`, `m_GeometryTwoSidedPipeline` |
| TwoSided in the material editor | ❌ missing | `MaterialEditorPanel.cpp` "Rendering" section shows only Cast Shadows |
| Mid-pass pipeline switch | ✅ built | `Renderer::SwitchRenderPassPipeline`, `RenderCommandBuffer::RT_SwitchNRIRenderPassPipeline` |
| Albedo mip chain | ✅ working, coverage not preserved | `Texture2D::CreateFromSRGB` → `GenerateMips` (SRGBA path, scratch RGBA8) → `LinearSampleSRGB.glsl` box-averages alpha |
| Albedo slot → texture | ✅ working | `SceneRenderer` `resolveMaterialTexture`: SRGB slots → `MaterialAsset::GetSRGBTexture`; `GPUTextureSlot{Handle, ColorSpace}` |
| Runtime texture mips | ✅ generated at load | `TextureRuntimeSerializer` stores mip 0 (`Mips = 1`), loads with `GenerateMips = true` |
| Material ID in the G-buffer | ✅ working | `GBuffer_Static`: `gbuffer.MaterialID = GetInstanceMaterialIndex(...)`; `DeferredLighting` binds `PassInputMaterialScene` |
| Spare G-buffer channel | ✅ unused | `LuxGBufferData::Reserved0` → `normal.w` (RGBA16F); no shader reads it |
| Direct diffuse | ✅ working | `PBR.glslh` `LuxEvaluateDirectBRDF` (Frostbite diffuse); lights in `Lighting.glslh` / `ShadowMapping.glslh` |
| Two-sided transparent | ❌ missing | `TransparentForwardPass` uses one culling pipeline; transparent keys exclude `TwoSided` |
| Face culling options | ⚠️ back or none only | `PipelineSpecification::BackfaceCulling`; `Pipeline.cpp` maps it to `BACK` / `NONE` |
| Mesh-shader prepass | ⚠️ experimental, off; no alpha test, one culling pipeline | `PreDepth_Meshlet.glsl` (empty fragment stage; task stage skips the cone test) |
| Skeletal animation | ❌ does not exist | `MeshDrawPushConstants::BoneTransformBase` "unused (no animation)"; skeleton code commented out in `Mesh.h` |
| Shader pack (Dist) | ✅ built from the whole library | `ShaderPack::CreateFromLibrary`, called by `EditorLayer.cpp` |

Nothing above was profiled; "working" means read in source and, where noted, seen by the user.

---

## Part 1 — Design

### Coverage-preserving mips

- **Slot key.** `GPUTextureSlot` gains `uint8_t AlphaCutoff` (cutoff × 255; 0 = none).
  `MaterialScene::BuildGPUMaterialData` sets it on the albedo slot when the row resolves Masked.
- **Variant.** NEW `MaterialAsset::GetAlphaCoverageTexture(handle, texture, cutoff)` caches one
  texture per (handle, cutoff), next to the sRGB cache. It is created when the material's albedo
  map, alpha mode or cutoff is set, the point where `MaterialAsset` already makes the sRGB copy.
  So it is eager. `resolveMaterialTexture` returns it for slots with a cutoff.
- **Generation.** NEW `Texture2D::CreateFromSRGB(texture, alphaCutoff)` runs the existing SRGBA path:
  copy mip 0, box-downsample every mip into the scratch RGBA8 image, then run two NEW compute
  shaders before the copy back.
  - `AlphaCoverageHistogram.glsl` counts each mip's alpha into 256 bins (shared-memory atomics,
    one storage buffer of `mips × 256` uints).
  - `AlphaCoverageScale.glsl` takes mip 0's coverage at the cutoff, finds the cutoff that gives mip
    *m* the same coverage (a suffix sum over its histogram), and rescales that mip's alpha in place
    by `cutoff / cutoff'`.
  - Mips downsample from the unscaled level above (Castaño), so the scaling never compounds.
- Every alpha-tested pass samples the same bindless slot, so prepass, G-buffer and shadows stay in
  agreement.

### Diffuse transmission

- **Inputs** (NEW, `MaterialSurfaceParameters`): `DiffuseTransmission` (0 = off; the default
  reproduces today's shading), `DiffuseTransmissionColor` (1, 1, 1), `DiffuseTransmissionMap`
  (factor from alpha, as in glTF). Opaque and Cutout materials only.
- **GPU row.** `GPUMaterialData` grows from 9 to 11 `vec4` rows: `Transmission` (linear colour,
  w = factor) and `TransmissionTextureIndices` (x = factor map). The C++ struct, the GLSL struct
  and the `static_assert` change together.
- **G-buffer.** `GBuffer_Static` writes factor × map into `Reserved0`.
- **Deferred lighting.** Skipped for pixels with factor 0. For the rest, the material row is read
  through `MaterialID`, then per glTF:
  - each light's diffuse becomes `(1 − t)·diffuse(N·L) + t·colour/π·max(−N·L, 0)`;
  - the IBL diffuse mixes in the irradiance from `−N`;
  - specular is unchanged.
- **Transmission shadows.** The back lobe needs its own shadow lookup: today's normal offset points
  toward the camera, so a back-lit leaf would shadow itself. The back lobe's offset points to the
  light's side, and it is evaluated only when `t > 0`.

### Two-sided transparency

- Transparent keys carry `TwoSided` too.
- A two-sided transparent draw records twice in `TransparentForwardPass`: first through NEW
  `m_TransparentBackFacesPipeline` (front faces culled), then through the default pipeline.
- NEW `PipelineSpecification::FrontFaceCulling`; `Pipeline.cpp` maps it to `nri::CullMode::FRONT`.
- `HazelPBR_Transparent` flips the normal on back faces, as `GBuffer_Static` does.

### Mesh-shader prepass parity

- The mesh stage outputs UVs (raw vertex floats 12–13, V flipped like the G-buffer) and a flat
  object index.
- The fragment stage gets PreDepth's alpha test, with `GetMaterialMipBias` copied verbatim.
- `m_PreDepthMeshletPass` binds `GPUMaterials`, `r_MaterialSampler`, `RendererData` and the
  bindless table.
- NEW `m_PreDepthMeshletTwoSidedPipeline`, switched like the classic prepass.

### Depth-only prepass (measured)

- `PreDepth` and the meshlet prepass split into a depth-only shader (no material reads, no discard)
  and the alpha-test shader.
- The main key carries `AlphaMasked`, and the prepass switches between them.
- `precise invariant gl_Position` and the vertex math stay identical.

### Threads and lifetime

- Material and slot work runs on the main thread (`MaterialAsset` setters,
  `MaterialScene::BuildGPUMaterialData`).
- Texture generation is recorded through `Renderer::Submit`, like `CreateFromSRGB` today.
- The variant cache holds `Ref<Texture2D>`s. Replacing an entry frees the image through its
  destructor's deferred release.
- No new threads. Correct under both policies, because every recording goes through `Submit`.

---

## Part 2 — Phases

Each phase builds (Debug, Core then Editor), passes the offline shader check (`glslc` +
`spirv-val` per stage) and leaves the engine shippable. Every phase that touches a shader deletes
`Resources/Cache/Shader/` before it is looked at. Each phase is one commit on
`feature/foliage-materials`.

### Phase 0 — Alpha masks match what is drawn

- **Changes:**
  - `PreDepth.glsl`, `DirShadowMap_Masked.glsl`, `SpotShadowMap_Masked.glsl`: output
    `vec2(a_TexCoord.x, 1.0 - a_TexCoord.y)`, as `GBuffer_Static` does.
  - `MaterialEditorPanel.cpp` "Rendering": a "Two Sided" checkbox on `surface.TwoSided`.
- **Verify:** offline shader check. In Sponza, cut-out shadows match their leaves, and leaf edges
  have no dark specks. The checkbox toggles culling live, with unique ImGui IDs and the scope
  closed on every path.
- **Docs:** `Architecture-LuxEngine.md` Cutout bullet ("the same UVs as the G-buffer").

### Phase 1 — Leaves keep their coverage at a distance

- **Changes:**
  - `TextureScene.h` `GPUTextureSlot`: + `AlphaCutoff`, folded into `==` and the hasher.
  - `MaterialScene.cpp`: set it on Masked albedo slots.
  - `MaterialAsset.{h,cpp}`: the variant cache and its eager creation.
  - `Texture.{h,cpp}`: the cutoff overload of `CreateFromSRGB`.
  - `SceneRenderer.cpp` `resolveMaterialTexture`: return the variant for slots with a cutoff.
  - NEW `AlphaCoverageHistogram.glsl` and `AlphaCoverageScale.glsl`, loaded in `Renderer::Init`.
- **Verify:** offline shader check; Sponza plants stay full from far away; a non-Cutout material
  using the same texture keeps the plain chain; dragging the cutoff regenerates the chain without
  growing the cache without bound.
- **Docs:** `Rendering.md` § GPU material table (slot key, coverage variant).

### Phase 2 — Transmission inputs

- **Changes:**
  - `MaterialAsset.h` surface parameters.
  - `MaterialSerializer.cpp`: write the keys only when not default; missing keys read the default.
  - `MaterialScene.{h,cpp}` and `MaterialScene.glslh`: the two new rows, in step.
  - `MaterialEditorPanel.cpp`: a "Translucency" section, opaque materials only.
- **Verify:** an `.lmat` without the keys loads and renders unchanged. Save and reload round-trips
  the values. ImGui scopes and IDs are checked, and every new state is driven in the editor.
- **Docs:** `Rendering.md` § GPU material table (row layout).

### Phase 3 — Light through leaves

- **Changes:**
  - `GBuffer_Static.glsl`: writes `Reserved0`.
  - `DeferredLighting.glsl`, `Lighting.glslh`, `ShadowMapping.glslh`, `PBR.glslh`: the
    transmission lobe for directional, point and spot lights, plus IBL, with light-side shadows.
- **Verify:** the offline check, plus `tests/rendering/run_shadow_shader.py` (no PCSS local
  arrays). A back-lit Sponza leaf glows, and materials with factor 0 are pixel-identical to
  before.
- **Docs:** `Rendering.md` (the G-buffer `normal.w` channel); `Architecture-LuxEngine.md`.

### Phase 4 — Double-sided transparents

- **Changes:**
  - `PipelineSpecification.h` / `Pipeline.cpp`: `FrontFaceCulling`.
  - `SceneRenderer.{h,cpp}`: transparent keys carry `TwoSided`; the back-faces pipeline; the
    two-pass draw.
  - `HazelPBR_Transparent.glsl`: back-face normal flip.
- **Verify:** a BLEND + doubleSided glTF (or a material toggled two-sided) shows its far side
  through its near side; single-sided transparents are unchanged.
- **Docs:** `Rendering.md` (the raster-variant rule).

### Phase 5 — Mesh-shader prepass parity

- **Changes:**
  - `PreDepth_Meshlet.glsl` (UV and object-index outputs, alpha test).
  - `SceneRenderer.{h,cpp}`: meshlet pass inputs, the no-cull meshlet pipeline, and switching in
    `PreDepthMeshletPass`.
- **Verify:** offline check for the task, mesh and frag stages. With Mesh Shaders on, Sponza
  matches mesh shaders off. On a GPU without mesh shaders, nothing changes.
- **Docs:** `Rendering.md` / `Architecture-LuxEngine.md`: remove "the mesh-shader prepass has no
  variants".

### Phase 6 — Depth-only opaque prepass, if it wins

- **Steps:**
  1. `/profile` baseline: Sponza, fixed camera, VSync off; record `PreDepthPass` GPU time
     (in-editor Profiler / Tracy GPU zone).
  2. Implement the split.
  3. Measure again with the same protocol.
- **Keep it only if** `PreDepthPass` improves beyond run-to-run noise; otherwise drop the commit
  and record the numbers here.
- **Docs:** `Rendering.md` (prepass variants), if kept.

---

## Part 3 — Risks

- **Variant memory.** Each Cutout albedo adds a texture of the same size (about 5.3 MB for
  1024² RGBA8 with mips). The variants are visible in the editor's memory stats.
- **Translucency shadow cost.** A second shadow lookup for translucent pixels. It is bounded by
  how many there are, and checked in Phase 3 against the `ShadowKernel` rules.
- **GPU material row growth** (144 → 176 bytes). Storage sizes use `sizeof`; grep confirms no
  other user of the size.
- **Back-then-front** doubles draws for two-sided transparents only. Order between different
  objects is still the existing material sort.

## Part 4 — Open questions

- Does anything outside the material table read `GPUMaterialData`'s size or layout? Grep in
  Phase 2.
- glTF diffuse-transmission import: the vendored Assimp knows only `KHR_materials_transmission`
  (refraction), and the extension is a Release Candidate. Follow-up.
- A transmission colour texture needs a G-buffer channel. Follow-up, if leaves need vein masks.
- The YAML oracle (`tests/yaml/run.py`) does not build on GCC 16 (pre-existing), so Phase 2
  round-trips are checked by load and save in the editor.

## Part 5 — Research notes

- **Coverage-preserving mips:** keep the fraction of texels above the cutoff equal at every mip by
  solving a new cutoff and rescaling alpha. The industry standard, shipped as Unity's
  "Mip Maps Preserve Coverage".
  [Castaño, Computing Alpha Mipmaps](http://the-witness.net/news/2010/09/computing-alpha-mipmaps/)
- **Runtime coverage:** Castaño's 2026 runtime-mip article notes the reduction is the cost, and
  precomputes scales offline. Lux's 8-bit alpha makes one histogram per mip exact, which removes
  the iteration.
  [Runtime Mipmap Generation](https://www.ludicon.com/castano/blog/2026/02/runtime-mipmap-generation/)
- **Alternatives compared:** shader mip-scaling, max-filter and SDF mips. SDF keeps shapes best,
  but leaks colour and needs a new texture format, so it is rejected for now.
  [lisyarus](https://lisyarus.github.io/blog/posts/exploring-ways-to-mipmap-alpha-tested-textures.html)
- **Rejected: alpha-to-coverage.** It needs multisampled targets, and the scene renders single
  sample.
  [Golus](https://bgolus-medium-com.translate.goog/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f?_x_tr_sl=en&_x_tr_tl=es&_x_tr_hl=es&_x_tr_pto=tc)
- **Rejected: hashed alpha testing.** It relies on temporal accumulation to resolve its noise,
  against the standing no-temporal rule.
  [Wyman & McGuire 2017](https://research.nvidia.com/publication/2017-02_hashed-alpha-testing)
- **Diffuse transmission:** `diffuseTransmissionFactor` (factor map in alpha) and
  `diffuseTransmissionColorFactor` move energy from diffuse reflection to a thin-walled diffuse
  transmission lobe; specular is unaffected. The status is Release Candidate.
  [KHR_materials_diffuse_transmission](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_diffuse_transmission/README.md),
  [DiffuseTransmissionPlant sample](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/DiffuseTransmissionPlant/README.md)
- **Prior art for translucency:** Unreal's Two Sided Foliage (a subsurface colour for back-lit
  leaves); Frostbite's cheap translucency (wrap and distortion, the artist model the user did not
  pick).
  [Unreal shading models](https://dev.epicgames.com/documentation/en-us/unreal-engine/shading-models-in-unreal-engine),
  [Barré-Brisebois & Bouchard, GDC 2011](https://colinbarrebrisebois.com/2011/03/07/gdc-2011-approximating-translucency-for-a-fast-cheap-and-convincing-subsurface-scattering-look/)
- **Two-sided transparency:** HDRP renders a double-sided transparent's back faces in one draw and
  its front faces in a second.
  [HDRP Lit Shader](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Lit-Shader.html)
