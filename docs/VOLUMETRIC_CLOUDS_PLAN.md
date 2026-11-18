# LuxEngine Volumetric Clouds and Sky Atmosphere Plan

Planning document, written 2026-11-17 on `feature/volumetric-clouds` (off `dev` at `6094574e`). It
describes work that has not been built. What exists today is described by
`.claude/docs/Architecture-LuxEngine.md`, which is the authority; `docs/` is history (see
`CLAUDE.md`). Every "exists" claim below was read in the source this session; anything unread is
marked **NEW** or sits under Open questions.

**Goal card**

- **Goal:** physically based volumetric clouds (Horizon / Nubis / Frostbite lineage) in the Vulkan
  renderer, on top of a rebuilt Hillaire-style sky atmosphere.
- **Success:** in the editor and in Runtime, a scene with clouds enabled shows lit, depth-correct
  clouds in front of and behind geometry; they react to the sun (sunset reddening, silver lining,
  milky multi-scatter); they cost **under 1 ms at 1080p on the RTX 4070 Ti**; with the feature off
  they cost nothing (no pass, no memory); existing scenes and projects load unchanged.
- **Full customization (added 2026-11-17):** every sky and cloud parameter is editable live in a
  **Cloud Debug Panel** (editor panel, tabs Debug View / March / Art / Gen / Sky), built **in the same
  phases as the features it controls**, not at the end. See "Cloud Debug Panel" below.
- **Non-goals:** no in-game/runtime UI (none exists to host it; see the panel section for the
  follow-up), no height fog, no local fog, no volumetric fog (all deleted 2026-08-16); no
  froxel/voxel Nubis Cubed renderer; no clouds in reflections (SSR) or refraction in v1; no VR
  stereo or orthographic support in v1; no C# API in v1.
- **Emphatic constraints (user's words):** *no temporal rendering, ghosting is a red line*; the
  brief's Agent 4 (ping-pong history, temporal reconstruction) is **dropped**. Quarter-res
  everywhere, spatial reconstruction only. Build the sky atmosphere **first**.

## Decisions this plan is built on

| Decision | Choice | Who | Consequence |
|---|---|---|---|
| Temporal reprojection / history | **None.** Spatial denoise + depth-aware upsample | user (standing rule) | Horizon's 20 ms raw march only fit via its 1/16 reprojection; we buy it back with quarter-res, cheap/full dual march, early exit, cheap light samples |
| Settings home | **Scene-level structs** (`SkyAtmosphereSettings`, `CloudSettings`) beside `PostProcessSettings` | user | One serialized block, one packet field, no component playbook, no hierarchy row |
| Sky coupling | **Rebuild the Hillaire sky atmosphere first** | user | Plan is two parts; A must land before B3 (lighting) and B4 (aerial perspective) |
| GPU budget | **< 1 ms**, quarter-res everywhere at 1080p | user | Fewer light samples, softer detail; measured in Phase 0 and again in B2, not assumed |
| Reuse of the deleted cloud code | **Fresh implementation.** `git show e878071a^` is a design reference only | assistant | It "had never executed" (commit message). Same posture as the RT/terrain/GI restart rule |
| Noise texture format | RGBA8 base 128³ (8 MB), RGBA8 detail 32³ (128 KB), 128² curl | assistant | The deleted bake used RGBA16F (16 MB); Horizon used 8-bit |
| Frame position | After Deferred Lighting / Area Lights / SSR composite, **before** Transparent Forward | assistant | Transparents then draw over clouds; SSR and refraction do not contain clouds (documented limit) |
| Per-pixel jitter | Interleaved gradient noise computed in the shader, no texture | assistant | The repo has no blue-noise asset; add one only if B4's comparison shows IGN patterning |
| Customization UI | One new `CloudDebugPanel` editor panel, grown phase by phase | user | Playbook "Add a new editor panel"; each phase lists the controls it adds; ImGui audit every phase |
| Earlier verdict superseded | `docs/FEATURE_CATALOG_2027.md` row R8 said "physical atmosphere and clouds: DEFER to 2028" | user (now wants it) | Update that row when Phase B5 lands |
| Quality category | New `Atmosphere` category | assistant | `ProjectSerializer` reads `QualityCategories` as a name-keyed map defaulting to `"Custom"`, so old projects load; verified in source |

## Part 0 — Where we are (ledger)

| Capability | State | Evidence |
|---|---|---|
| Volumetric clouds, sky atmosphere, height fog | ❌ Removed. The code **never executed**: passes declared, never assigned | `e878071a` (2026-08-16), `d98265cf` (2026-07-04); grep of `Core`/`Editor` finds no `VolumetricCloudComponent`, `SkyAtmosphereComponent`, `ExponentialHeightFog` |
| Docs still list them | ⚠️ **Stale** | `.claude/docs/Rendering.md:254`, `Architecture-LuxEngine.md:383` and `:525-526` name cloud, atmosphere and fog passes and components |
| Sky today | ✅ Preetham cubemap → skybox pass + IBL | `Renderer::CreatePreethamSky` (`Renderer.cpp:1511`), `Skybox.glsl`, `SceneRenderer::SkyboxPass`, `Scene::m_DynamicSkyEnvironment` |
| Scene-wide settings home | ✅ `PostProcessSettings` on `Scene` → packet → renderer | `Scene.h:79-81,319`, `FrameRenderPacket.h:77`, `SceneSerializer.cpp:1515-1593` |
| Sun | ✅ `LightEnvironment::DirectionalLights[0]`; shader `u_Scene.DirectionalLights` (direction **toward** light, radiance × multiplier = illuminance) | `SceneRenderer.h:119,220`, `Buffers.glslh:20-30`, `Rendering.md § The sun's direction` |
| Depth, camera helpers | ✅ reverse-Z (sky = 0), `LinearizeDeviceDepth`, `InverseViewProjectionMatrix`, `IsOrthographic` | `Buffers.glslh:105-137`, `Rendering.md` (GTAO note) |
| 3D images | ⚠️ **Built, unproven.** `ImageSpecification{Dimension=Texture3D, Depth}`, NRI volume creation, `StorageImage3DVolume` / `ImageSampler3DVolume` reflection types exist; **nothing live creates or binds one** (the old cloud bake did, and never ran). The class is named `Image2D` | `Image.h:96-102`, `Image.cpp:223,279`, `RHIResources.cpp:168`, `ShaderReflection.h:38-41` |
| Tiling sampler | ✅ `r_RepeatSampler` (set 3, binding 4) declared and bound | `Samplers.glslh:8`, `DescriptorSetManager.cpp:302-309`, `Renderer::GetRepeatSampler` |
| Noise primitives | ⚠️ `LuxNoise.glslh` has PCG3D, gradient noise, `LuxVoronoi`, but **not periodic**: cell hashes take no wrap period, so baked volumes would seam when tiled | `LuxNoise.glslh:23,36,147` |
| Half/quarter-res effect pattern | ✅ `EffectResolutionScale {Full, Half, Quarter}`; SSR = compute at scale + composite pass | `SceneRenderer.h:261`, `SSRCompute`/`SSRCompositePass` (`SceneRenderer.cpp:9949-9994`) |
| Upsample lesson | ⚠️ SSR's alpha-weighted bilateral upscale bled dark, high-confidence texels and was replaced by a plain bilinear tap | comment in `SSRCompositePass` |
| Graph insertion point | ✅ `addPass(...)` blocks; `sceneColorCurrent` threads colour between passes | `SceneRenderer.cpp:4379-4532` |
| Quality tiers | ✅ `QualityCategory` (7), `RendererQualityCategories`, `ProjectSceneRendererSettings::QualityCategories` name-keyed map | `SceneRenderer.h:77-101`, `Project.h:97-101`, `ProjectSerializer.cpp:226,337-340` |
| Blue-noise texture | ❌ none in the repo | grep |
| Runtime behaviour of any of the above | ❌ nothing was launched this session | — |

## Part 1 — Design

**Systems and data flow.** Settings are plain structs in a new header `Renderer/AtmosphereSettings.h`
(**NEW**), held by `Scene`, copied into `FrameRenderPacket::Atmosphere` on the main thread in
`BuildRenderPacket*`, and read by `SceneRenderer` on the render thread from the packet copy only
(`Rendering.md § Feeding the renderer from the scene`). Passes live in `SceneRenderer` and run in the
RenderGraph; nothing reads the ECS during submission.

**Threads.** All pass bodies and all image creation run on the render thread (Windows default) or
inline (Linux single-threaded). Lazy resource creation (noise volumes on first enable) is submitted
through `Renderer::Submit`; teardown through `Renderer::SubmitResourceFree`. Scene switch with
atmosphere resources alive must not free anything an in-flight frame references; there is no
dedicated drain helper, so the plan keeps the resources **renderer-owned, not scene-owned**, and
frees them only on renderer shutdown or an explicit disable (via `SubmitResourceFree`).

**Render passes (new, all feature-gated to zero cost when off):**

```
... PreDepth -> HZB -> ... -> cluster light culling
    -> Sky Atmosphere LUTs (transmittance, multi-scatter, sky-view: compute)   [Part A]
    -> Sky Atmosphere draw (in place of Skybox's colour when enabled)          [Part A]
    -> Selected Geometry -> GBuffer -> GTAO -> Deferred Lighting -> Area Lights
    -> Aerial Perspective (froxel volume compute + geometry apply)             [Part A]
    -> SSR -> SSR Composite
    -> Clouds (quarter-res compute) -> Cloud Denoise (quarter-res) -> Cloud Composite  [Part B]
    -> Transparent Forward -> ...
```

The LUT compute and the sky draw sit where `Skybox` is today, **before** the G-buffer
(`SceneRenderer.cpp:4380`), because G-buffer emission and lighting treat sky pixels as already
holding colour. Aerial Perspective, which needs depth and lit geometry, is the only Part A pass that
runs after lighting.

**LUT lifetime (one rule).** The LUTs and the aerial-perspective volume are **persistent
renderer-owned images**, created lazily on first enable and registered in the graph with `addTexture`
(the way `m_SSRImage` is), **not** transient. Every later reader (A2's apply, B3's sun transmittance,
B4's aerial perspective) declares its read, so the graph orders and keeps them alive. Transmittance
and multi-scatter are rebuilt only when the atmosphere parameters change; the sky-view is rebuilt
every frame from the current sun, which is parameterised recomputation, not history, so it is
allowed. Because they persist across frames, only the pass that writes them may write them (a stale
read is a graph `ReadBeforeWrite`, not a silent bug).

**Units.** Scene lighting is physical (sun radiance × multiplier is illuminance, `PhysicalLight.h`).
Cloud and sky luminance must use the same convention and the same `EnvironmentMapIntensity`, so that
auto-exposure and the composite treat them like any other lit surface.

**Cloud model (from the Horizon and Nubis talks, adapted).** Layer between two altitudes on a planet
sphere; density = remap(base-shape noise × height gradient × coverage, eroded by detail noise,
domain-warped by curl); dual march (cheap samples until a hit, step back, full samples, cheap again
after several empty samples, exit at transmittance ≈ 0); lighting per Hillaire (analytic in-step
integration, dual-lobe Henyey–Greenstein, octave multi-scatter), 6-sample cone toward the sun;
ambient from the sky-view/irradiance; sun transmittance from the transmittance LUT.

**Depth interaction.** The march ends at scene depth. Quarter-res uses a per-block depth choice
resolved in B2 (nearest geometry keeps clouds from leaking in front of near objects). The composite
reconstructs at full-res with a depth-aware, **non-alpha-weighted** upsample.

**Serialization.** A new `Atmosphere:` YAML block in the scene next to `PostProcess:` (absent ⇒
defaults, disabled), so every existing `.luxscene` loads unchanged. Quality knobs follow the
renderer-owned project path and therefore land in `LuxSample.luxproj`: diff that file first on any
visual regression (known trap).

## Part 2 — Phases

Each phase builds, runs in the editor, verifies on its own, and leaves the engine shippable. New
files need `scripts\Win-GenProjects.bat`. Implement through `/dev`, review with `/cr`.
**Do not launch the editor without explicit OK that turn** (standing rule).

### Phase 0 — Measure, spike, fix the docs

**Goal:** the two unproven assumptions are settled and the stale docs are corrected.

- Measure: `/profile` the Benchmark scene and the sample scene at 1080p for frame and per-pass GPU
  time (baseline for the 1 ms claim).
- Spike (throwaway, not committed): create one `Texture3D` storage image through
  `Image2D::Create`, write it from a compute shader, sample it as `texture3D` with
  `r_RepeatSampler` in another. Capture in RenderDoc (`renderdoc-scripted-capture` recipe) and read
  it back. Also confirm reflection assigns `ImageSampler3DVolume` / `StorageImage3DVolume`.
- Docs (docs-only, straight to `dev`): remove the cloud, atmosphere and fog passes and components
  from `Rendering.md:254` and `Architecture-LuxEngine.md:383,525-526`.

**Exit:** a 3D image round-trips on the GPU (or the exact failure is recorded and Phase B1 gains a
fix-up step); baseline numbers written down; docs match source.
**Rollback:** the spike is not committed; docs revert with git.

### Part A — Sky atmosphere (Hillaire 2020, MIT reference exists)

### Phase A1 — Sky atmosphere with LUTs, replacing the skybox colour when enabled

**Changes**
- `Renderer/AtmosphereSettings.h` — **NEW**: `SkyAtmosphereSettings` (planet radius, Rayleigh, Mie,
  ozone, ground albedo, sun disc), `Enabled=false`.
- `Scene.h/.cpp`, `SceneSerializer.cpp`, `FrameRenderPacket.h`, `Scene::BuildRenderPacket*` — carry
  the struct (serialize **and** deserialize; packet capture). **Play-mode trap, verified by grep:**
  `Scene::Copy` (`Scene.cpp:232`) and `Scene::CopyTo` (`:2242`) copy the name, viewport, sound banks
  and entities but **do not copy `m_PostProcessSettings`**, and the only other writers are the
  serializer and the editor panel. So a scene-level struct must be copied **explicitly** in both
  functions or it silently resets to defaults on Play. Whether Play today goes through `Copy`/`CopyTo`
  (which would mean scene post-process settings are already lost on Play) is **unconfirmed at
  runtime**; check it in Phase A1 and, if so, report it as an existing bug rather than fixing it here.
- `Editor/Resources/Shaders/SkyAtmosphereLUT.glsl`, `SkyAtmosphere.glsl`,
  `Include/GLSL/Atmosphere.glslh` — **NEW**: transmittance, multi-scatter and sky-view bakes,
  and the sky draw. LUT sizes follow the paper (typically 256×64, 32×32, 192×108: verify against
  the paper, not memory).
- `SceneRenderer.h/.cpp` — pipelines created once in `Init()`; LUT images persistent, created on
  first enable and registered with `addTexture` (see "LUT lifetime"); passes added with accurate
  `Reads`/`Writes`; skipped when disabled.
- `Editor/Source/Panels/CloudDebugPanel.{h,cpp}` — **NEW**: the panel, Sky tab only at this point
  (see "Cloud Debug Panel"; `SceneRendererPanel.cpp` is not touched). Register in `EditorLayer` with
  a menubar toggle (playbook "Add a new editor panel").

**Playbook:** "Add a new render pass" (all steps) and the scene-struct serialization steps above.
**Thread/lifetime:** render thread; LUT images created in `Init()` only when first enabled, freed
via `SubmitResourceFree`.

**Verification**
- Build Debug + Release (regenerate projects). Delete `Resources/Cache/Shader/` before judging a shader.
- Run (with OK): enable atmosphere on the sample scene, move the sun through a full day. Sky
  reddens at the horizon, ground-side fade is sane; disable ⇒ pre-change image.
- Log: **no** `Uniform buffer binding collision`, no validation errors, no RenderGraph
  diagnostics above Info. `RenderGraph::RunValidationSelfTests` clean if compile logic touched.
- Numbers: `/profile` before/after; target < 0.3 ms for LUTs + sky at 1080p.
- Golden: a new scene reference; with the feature **off** the existing golden (`lm8`) is bit-identical.

**Exit:** enabled sky is plausible and sun-driven; off = unchanged; budget met.
**Rollback:** `Enabled=false` default; revert the commit.

### Phase A2 — Aerial perspective on geometry

- `AerialPerspective.glsl` — **NEW**: camera-froxel inscatter + transmittance volume
  (`Texture3D`, from Phase 0's proof), applied by a fullscreen pass that reads depth.
- Graph: after Area Lights, before SSR composite. Orthographic cameras skip it.
- **Known v1 limits, stated up front:** the apply pass fogs **opaque** geometry only, so glass drawn
  by `Transparent Forward` (`HazelPBR_Transparent.glsl`) is unfogged against fogged distance. Fix by
  sampling the same volume in that shader if the mismatch is visible in review; not in v1 otherwise.
  The froxel volume has a finite far slice: distances beyond it **clamp to the last slice** for
  geometry, and B4 handles cloud hit distances past it by falling back to the sky-view transmittance
  toward the horizon (clouds at 30-60 km routinely exceed a froxel range).

**Verify:** a long corridor/terrain shot fades toward sky colour with distance and matches the sky
at the horizon; off = unchanged golden. **Exit:** budget < 0.3 ms. **Rollback:** feature flag.

### Phase A3 — Sky drives image-based lighting

Capture the atmosphere sky into the sky-light cubemap and re-run irradiance
(`CreateEnvironmentIrradianceMap`, `Renderer.cpp:1566`) when the sun moves beyond a threshold, so
ambient light matches the sky. Throttled, never per frame. **Verify:** sun elevation sweep changes
ambient tint with no one-frame pop; cost is paid only on threshold crossings. **Rollback:** keep the
Preetham environment path as the fallback and the default.

### Part B — Volumetric clouds

### Phase B1 — Noise volumes

- `Editor/Resources/Shaders/CloudNoiseBake.glsl` — **NEW**: one compute shader, one mode per dispatch:
  base 128³ RGBA8 (R Perlin–Worley, GBA Worley at rising frequency), detail 32³ RGBA8 (Worley),
  curl 128² RG8. Channel layout follows the Horizon talk (verified from the slides: 128³ ×4 ch,
  32³ ×3 ch, 128² curl ×3 ch).
- `Include/GLSL/LuxNoise.glslh` — add **periodic** variants (`period` parameter wrapping cell
  coordinates); leave the existing functions untouched for the material graph.
- `SceneRenderer` — lazy bake on first enable, kept in renderer-owned images; `SubmitResourceFree`
  on teardown.

**Verification:** read back the volumes; assert wrap continuity (texel 0 vs texel N−1 difference
no larger than interior neighbours) and identical hash across two runs. A debug slice view in the
Renderer Debugger. **Exit:** deterministic, seamless volumes; ~8.1 MB only when clouds are enabled.
**Rollback:** nothing consumes them yet.

### Phase B2 — Ray march, density, depth (unlit)

- `VolumetricClouds.glsl` — **NEW** compute at quarter-res: ray–sphere layer intersection (camera
  below / inside / above the layer), `LinearizeDeviceDepth` clamp, dual cheap/full march, 64→128
  samples by ray length, early exit, IGN start offset, wind by time offset. Output RGBA16F
  (transmittance + provisional white luminance) plus a mean-hit-distance R16F.
- Pixel → UV uses the engine's **Y flip** (NVRHI viewports are negative-height); a mirrored result
  is the symptom.
- `CloudSettings` (coverage, density, type blend, layer bottom/top, wind speed/direction, quality
  step counts), `Enabled=false`.
- A debug view that shows the cloud buffer directly.

**Verify:** silhouettes correct from below/inside/above the layer; clouds are occluded by a mountain
or building in front of them and **not** drawn over near geometry; no screen-space mirror. `/profile`
per-pass time: record it, and **stop and re-plan if the march alone exceeds 0.6 ms** (leaves room
for lighting + composite inside 1 ms). **Rollback:** feature flag.

### Phase B3 — Lighting

Hillaire analytic in-step integration, dual-lobe HG (forward g ≈ 0.8–0.85, back lobe for silver
and glow), Wrenninge octave multi-scatter (constants **not verified** here; start from the common
`a = b = c = 0.5` and check against the Oz/Wrenninge sources before treating them as correct), powder
term, 6-sample cone to the sun with the far shadow sample, ambient from sky irradiance, sun colour
from the transmittance LUT (Part A1). Shadow/ambient use physical units.

**Verify:** sunrise-to-sunset sweep reads milky white overhead, warm at the horizon, dark underside,
no negative or NaN luminance (clamp-check in a debug view); numbers within the B2 budget.
**Rollback:** previous phase output.

### Phase B4 — Spatial reconstruction and composite

- `CloudDenoise.glsl` — quarter-res edge-aware blur driven by the mean-hit-distance and
  transmittance, to hide the per-pixel jitter without history.
- `VolumetricCloudComposite.glsl` — graphics pass: depth-aware upsample to full-res
  (nearest/most-similar-depth taps; **never** weight by the sampled alpha and renormalise, which is
  exactly the SSR bleed), then `scene * T + L`; apply aerial perspective to cloud luminance by the
  mean hit distance (Part A2 volume); **past the volume's far slice** fall back to the sky-view
  transmittance toward the horizon rather than clamping, so distant clouds do not stop fading.
- Graph: before Transparent Forward; skipped for orthographic cameras; same `ApplyCoarseFragmentShadingRate`
  pattern as the SSR composite if appropriate.

**Verify (the red-line check):** pan the camera and spin it fast; **no** smear, trail or ghost
anywhere, because there is no history to produce one. Compare silhouette stability and noise
against the IGN baseline; add a blue-noise texture only if IGN shows visible structure.
Total clouds cost (march + denoise + composite) **< 1 ms at 1080p** (`/profile`, 4070 Ti).
**Rollback:** feature flag.

### Phase B5 — Settings, UI, tiers, serialization polish

- `CloudSettings` in the **Atmosphere** block (serialize + deserialize), panel presets and polish,
  `QualityCategory::Atmosphere` with Low/Medium/High/Ultra/Cinematic step counts and light-sample
  counts, `ProjectSceneRendererSettings` field, `s_QualityCategoryKeys` entry.
- Dist: new shaders in the engine shader pack; run `tests/shaders/check_engine_pack.py`.

**Verify:** save/load a scene both ways; open an old scene (no `Atmosphere:` block) and an old
`.luxproj` (7 category keys) with no changes and no warnings. **ImGui audit before hand-off:** every
`Push/Pop/Begin/End/Scoped*` balanced on every path, unique `##`/`###` IDs per row, scoped guards
inside popups die before `EndPopup`; drive every new control in the editor. **Docs:** update
`Rendering.md` (pass order, new invariants), `Architecture-LuxEngine.md` (renderer paragraph, scene
settings), `Conventions.md` only if a helper appears.
**Rollback:** feature defaults off.

### Phase B6 — Hardening

Linux shader compile (no `Platform/` code is added, so this is a shader-compile and
single-threaded check), Dist (no shader compiler in the player: pack must carry the shaders), runtime
export, golden-image set for a cloud scene, perf record in the golden `perf.json`, memory check that
disabling clouds releases the volumes, and a stress capture across quality tiers. Optional follow-ups
listed below.

## Cloud Debug Panel (built with the phases)

**What it is.** A dockable editor panel, `Editor/Source/Panels/CloudDebugPanel.{h,cpp}` (**NEW**,
`EditorPanel`, registered in `EditorLayer` with a menubar toggle; project regeneration), bound to the
active scene's `SkyAtmosphereSettings` and `CloudSettings`. It follows the layout of the panel you
showed from the Project_NervLand post (title, "Clouds rendering" checkbox, tabs, sliders, a
"Reload shaders" button), but the parameters are Lux's own, mapped below. The screenshots show a
third-party engine's UI; only the idea and the parameter vocabulary are borrowed.

**"In-game" means the editor.** No game-facing UI exists today. `Lux-Runtime` creates ImGui only in
non-Dist builds, for the accessibility overlay (`RuntimeApplication.cpp:146-151`, F10 toggle in
`RuntimeLayer::OnImGuiRender`), so a runtime copy of this panel would be possible later in
Debug/Release runtime builds but is **not** in this plan. A C# API for weather-by-script is likewise a
follow-up.

**Where edits go.**
- Artistic values (altitude, coverage, lighting, sky) edit the **scene struct**, so they are saved
  with the scene and survive Play (copied explicitly; see A1).
- Cost knobs (max steps, light steps, resolution, denoise strength) edit the renderer-owned
  `Atmosphere` quality category and flip it to `Custom` through `MarkCategoryCustom`, like the other
  categories; they land in `LuxSample.luxproj` (diff that file first on any visual regression).
- Every slider has a reset-to-default, a tooltip with units, and the same clamp the shader assumes.
- **Reload shaders** calls the existing `Renderer::ReloadShaders` (the same path the Renderer
  Debugger uses); remember that a stale shader cache looks like a broken shader.

**Tabs and controls, mapped to the plan's phases.** Names in the left column are the vocabulary of
the screenshots, in Lux terms in the right.

| Tab | Control (screenshot name) → Lux field | Added in |
|---|---|---|
| **Sky** | Enabled, sun disc, planet radius, Rayleigh/Mie/ozone strength, ground albedo, aerial-perspective strength and distance scale, "Sun" intensity multiplier | A1, A2 |
| **Gen** | `Base (m)` → layer bottom altitude; `Thickness` → layer thickness; `Billow amp`, `Gain` → base-shape amplitude and fBm gain; `Top rise`, `Base rise` → height-gradient shaping at the top and bottom of the layer; `Noise freq` → base-noise frequency; `Cov min` / `Cov max` → coverage remap; `Weather` → weather-noise influence; `Wthr cov` → weather coverage | B2 |
| **March** | `Max steps`, `Step min/max` → step-count and step-length bounds; `SDF step` → how aggressively the cheap march skips empty space (Lux's cheap/full dual march); `Detail` → erosion/detail-noise amount; `Light steps` → cone light samples (0-6); `Far start/blend/step/detail` → distance LOD (cheaper steps and detail for far clouds); `Orbit shade/relief`, `Near shade` → look when the camera is above or inside the layer | B2, B3, B4 |
| **Art** | `Ambient`, `Amb floor`, `Amb dirs` → sky-ambient strength, minimum floor, and number of ambient sample directions; `Extinction` → σ_t scale; `Sun` → sun-light multiplier; plus Lux's own: forward/back HG g, multi-scatter a/b/c and octaves, powder strength | B3 |
| **Dbg View** | `Debug view` dropdown: Off, Density, Transmittance, Luminance, Step-count heat map, Mean hit distance, Light-sample cost, Noise slice (base/detail/curl), Depth clamp, Upsample weights; plus pass GPU times (march / denoise / composite / sky LUTs / aerial perspective) from the existing GPU profiling | B2, B4 |

**What does not map in v1.** `Far use map` and `Weather` imply a painted weather map; v1 derives
coverage from a procedural low-frequency 2D noise driven by `Weather`/`Wthr cov`. A painted weather
texture asset is a follow-up. `Orbit shade/relief` only matters when the camera leaves the layer
(spacecraft views); expose them but mark them low-priority in B2.

**Phase hooks (each phase's exit criteria include these).**
- **A1:** panel exists with the Sky tab and the enable checkbox; Reload shaders works.
- **B2:** Gen and March tabs live; Dbg View shows density, step count and noise slices.
- **B3:** Art tab live (lighting, multi-scatter); a debug view for luminance and NaN.
- **B4:** denoise strength, upsample debug view, per-pass GPU times.
- **B5:** presets (Clear / Fair-weather cumulus / Overcast / Storm) as saved named parameter sets,
  reset-all, copy/paste of the whole set; the panel's scene edits are undoable only if the engine's
  undo system already covers scene-level structs (open question 7).
- **ImGui audit every phase** (`Conventions.md`): every `Push/Pop/Begin/End/Scoped*` closed on every
  path including early returns; unique IDs (`##` per control, `###` for the tab labels); each tab and
  popup driven in the running editor.

## Part 3 — Verification beyond the phases

Golden scenes: one with atmosphere only, one with clouds (fixed sun, fixed time offset) captured
through `LUX_GOLDEN_*`. A **changed** golden is shown to the owner and accepted as a new reference
if it is an improvement; it is not a rejection. A fast-pan capture protocol (`MoveEntities` or a
scripted camera) proves the absence of ghosting.

## Part 4 — Risks

| Risk | Likelihood | Early detection |
|---|---|---|
| 3D storage/sampled images do not work end to end | medium (never ran) | Phase 0 spike |
| < 1 ms is not reachable spatial-only (Horizon needed reprojection to hit its budget) | medium | B2 stop-rule at 0.6 ms for the march |
| Per-pixel jitter patterning without a temporal resolve | high | B4 comparison; fallback to a blue-noise texture or fewer jittered steps with a stronger blur |
| Quarter-res silhouettes against geometry edges | medium | B4 depth-aware upsample vs thin objects (fences, wires) |
| Atmosphere (Part A) triples the scope | high | A is its own mergeable stage; clouds can ship on a fixed-colour sky approximation if A stalls, but that is a decision to re-ask |
| (set, binding) collision for the new UBO | medium | `set = 2, binding = 3` was the old atmosphere slot but is **used now**; grep every slot at the time of use; a collision log line is a build break |
| Stale doc claims mislead future work | certain until Phase 0 | docs fix in Phase 0 |

## Part 5 — Open questions

0. **Budget wording:** the < 1 ms target is for **clouds only** (march + denoise + composite).
   Atmosphere (LUTs, sky, aerial perspective) has its own budget, < 0.3 ms each stage.

1. Does `Image2D` accept `Texture3D` end to end (Phase 0).
2. Which LUT sizes and multi-scatter octave constants are right for Lux: read Hillaire 2020 and
   Wrenninge 2013 directly; this session reached only secondary summaries for the constants.
3. Does VR stereo (`docs/VR_PLAN.md`, Phase 1 coded, untested) need per-eye clouds, or do clouds
   stay off in VR for v1.
4. Where VRS (`ApplyCoarseFragmentShadingRate`) helps the composite.
5. Does the sky-view/IBL recapture (A3) interact with `Scene::m_DynamicSkyEnvironment` caching.
6. Does disabling clouds mid-session need a GPU drain before freeing the volumes (see Part 1).
7. Does the editor's undo system cover scene-level structs (post-process today)? If not, panel edits
   are not undoable in v1 and the panel says so; do not invent a parallel undo path.

**What would invalidate the plan:** Phase 0 showing 3D images unusable (adds a backend fix phase),
or B2 measuring far above budget (re-plan resolution/steps or revisit the budget with the owner).

## Part 6 — Research notes (compacted)

**Goal:** physically based, spatial-only volumetric clouds under 1 ms.

- **Concept (verified from the slides):** Horizon uses two 3D textures and one 2D: a 128³ RGBA base
  (Perlin–Worley + three Worley), a 32³ detail (Worley), and a 128² curl texture for distortion; cheap
  march until a cloud hit, step back, full samples, cheap again after empty samples, early exit at
  alpha 1; 64 to 128 potential samples; **6 light samples in a cone** with the last far away. Beer's
  law plus a "powdered sugar" term and HG phase. [Schneider/Vos 2015](https://www.advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf)
- **Pitfall:** Horizon's raw approach cost "around 20 milliseconds"; Guerrilla made it fit with a
  1/16 per-frame update and reprojection. **That route is rejected here (no temporal); the budget is
  paid in resolution and sample count instead.** Same source.
- **Prior art:** Hillaire's 2020 sky paper and slides settle on **octaves of single scattering**
  after Wrenninge for clouds, and a LUT-based sky; reference implementation is MIT-licensed
  ([UnrealEngineSkyAtmosphere](https://github.com/sebh/UnrealEngineSkyAtmosphere)). Take ideas, not
  code, unless the licence notice is carried. [Hillaire 2020 slides](https://blog.selfshadow.com/publications/s2020-shading-course/hillaire/s2020_pbs_hillaire_slides.pdf);
  [Frostbite 2016](https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016_pbs_frostbite_sky_clouds.pdf)
  (**not fetched**: over the 10 MB limit; the brief's formula `L = (S − S·T)/σt` is the Frostbite
  volumetric integration and was not re-verified here).
- **Prior art:** Wrenninge's octave scheme has a, b, c (attenuation, contribution, eccentricity
  attenuation); constants not confirmed this session.
  [Wrenninge 2015](https://history.siggraph.org/wp-content/uploads/2022/10/2015-Talks-Wrenninge_Art-Directable-Multiple-Volumetric-Scattering.pdf)
- **Rejected:** *Nubis Evolved* "temporal upscaling for fast clouds" (conflicts with the standing
  rule; the Guerrilla abstract itself says the result is delivered without temporal upscaling, the
  GDC listing says otherwise, so treat the temporal part as unreliable either way).
  [Nubis Evolved](https://www.guerrilla-games.com/read/nubis-evolved)
- **Out of scope:** Nubis Cubed (2023) moved to a voxel renderer and away from the 2.5D approach
  used here. [Nubis Cubed](https://www.guerrilla-games.com/read/nubis-cubed)
- **Owner-supplied leads, unread:** the "Chlorophyll Engine" video ("10M Trees - 15km² Test",
  title only; the page text had no technical content) and the r/Project_NervLand subreddit (Reddit
  fetch is blocked here). Nothing in them is used by this plan. Unverified search leads on open
  Vulkan cloud work, for later reading: Project Marshmallow (Horizon clouds in Vulkan, cloud shadows
  on terrain), CIS 5650 "Frostnova" (Nubis³ in Vulkan), and a hybrid sparse voxel-noise cloud paper.
- **Owner-supplied UI reference (screenshots from the NervLand post, 2026-11-17):** a "Cloud Debug
  Panel" with Dbg View / March / Art / Gen tabs. It reveals parameter vocabulary only (e.g. an
  SDF-style skip step, ambient sampled over N directions, distance LOD with a far weather map,
  orbit shading); its frame-time readout and internals are not independently verified and none of
  its algorithms are adopted.
- **Brief vs reality:** the brief assumes an existing sky/aerial-perspective system and a blue-noise
  texture; neither exists in this repo, hence Part A and the IGN decision.
- **Still unknown:** exact multi-scatter constants, LUT sizes, whether a blue-noise asset is needed.

## Follow-ups (not in this plan)

Clouds in SSR and refraction; cirrus/high-altitude 2D layers (Horizon's last step); lightning and
internal lights (Nubis Evolved's non-temporal part); VR stereo; orthographic; a C# API; a Nubis
Cubed-style voxel path.
