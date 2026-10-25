# LuxEngine Area Light Shadows Plan

Adds shadows to the area lights that `docs/RENDER_FEATURES_PLAN.md` phases 1–2 shipped unshadowed
(`AreaLightComponent`: rect, disk, sphere, tube; LTC in the "Area Lights" pass and in
`HazelPBR_Transparent.glsl`). Each shadowed area light renders one or more tiles of the existing
spot/point shadow atlas from a representative point, and PCSS sizes the penumbra from the light's real
extent. This is a planning document, not a description of what exists. `.claude/docs/Rendering.md`
("Area lights are a separate pass", "Point lights shadow through the spot atlas", the PCSS paragraphs,
"Shadow caches", "Shadow cull groups") describes what is built.

**Goal card**

- **Goal:** area lights cast shadows, with soft edges whose width follows the light's size, in the
  editor, Lux-Runtime and exported games.
- **Success:** an object under a rect, disk, sphere or tube light shadows the floor. The shadow is
  sharp where the object touches the floor and widens with distance, more for a bigger light. A rect
  light's penumbra is wider along its long side. Glass and other transparents receive the same shadow.
  The light's own fixture mesh does not shadow it. Moving a light or a caster updates the shadow, and
  static scenes reuse the static cache. Scenes without shadowed area lights render pixel-identical to
  today (Benchmark golden) and cost the same.
- **Non-goals:** ray-traced or SMRT shadows; anything temporal (per-frame kernel rotation,
  accumulation, TAA-resolved noise); contact shadows; shadows from emissive meshes; a C# light API
  (none exists for any light); clustered area-light culling; resuming any deleted RT/GI work.
- **Constraints:** no temporal techniques (hard rule). Existing `.luxscene` / `.luxproj` load
  unchanged, and old scenes look the same until a light opts in. Correct under both threading policies.
  Works in Lux-Runtime, Dist and exported games. No new installs, no new dependency. Spot and point
  shadows stay bit-identical.
- **User's words:** earlier: area lights "all shapes, unshadowed" (shadows "a follow-up"). Standing:
  follow Lucky Engine where it has the feature; never temporal; a changed golden image may be an
  improvement to accept.

**Decisions this plan is built on** (D1–D4 are the user's; the plan proceeds on the recommendation
until they answer. The rest are technical and reasoned in Part 2.)

| # | Decision | Choice (recommended) | Consequence |
|---|---|---|---|
| D1 | Default for `CastsShadows` on area lights | **Off.** New components and old files (missing key) both read `false`, like `SpotLightComponent` | Old scenes and RenderFeatures render unchanged; users opt in per light. Alternative: on for new components only (like point lights), which costs atlas tiles silently |
| D2 | Atlas budget for area tiles | **Leftover tiles:** spots first (≤16), then points (≤8 × 6), then area lights (≤`MaxAreaShadows` = 8 lights) from what is left of the 64 tiles | No existing scene changes. In a scene that fills the atlas with spots and points, area lights are the first to lose shadows. Alternatives: one unified score across all three types (changes which spot/point lights shadow today), or raising `MaxShadowAtlasTiles` (the grid is capped at 8×8 in code) |
| D3 | Rect/disk projection | **One perspective tile, a fixed 120° cone, apex pulled back behind the emitter** (HDRP's method, Part 7) | 1 tile per one-sided light. Receivers more than 60° off the light's axis are lit unshadowed (faded at the tile edge). Alternative: a 5-face hemicube from the centre covers the whole hemisphere at 5× the tiles and draw cost |
| D4 | Scope | **All four shapes and two-sided lights.** Sphere and tube take six cube tiles like point lights, a two-sided rect/disk takes two | Phase 3 can be dropped if 6 tiles per sphere/tube is too costly; rect/disk alone are useful |
| T1 | Penumbra | PCSS from the existing kernel, generalized with an **emitter depth** (the emitter is not at the tile's apex) and a **2D light radius** (rect: half-width, half-height along the tile axes) | Spot/point call it with emitter depth 0 and equal radii: identical maths, checked by the Benchmark golden |
| T2 | Bias | Constant `SPOT_SHADOW_BIAS` plus a normal offset of ~1.5 texels at the receiver's depth (the directional path's rule) | A 120° tile spreads texels thin at its edges; spot tiles have no normal offset today |
| T3 | Fixture self-shadow | Near plane at the emitter plane (rect/disk) or at the radius (sphere/tube), plus a small margin | A lamp housing behind or around the light never shadows it. A mesh placed inside the emitter still can; the inspector tooltip says to turn off its caster |
| T4 | Filter mode | Area soft shadows follow `ShadowFilterMode` like spots (PCSS mode → PCSS, otherwise Tuned PCF) | One rule for every local light. The working-copy `LuxSample.luxproj` has used mode 0 (Tuned PCF), so the golden runs force `PCSS=1` (Part 4) |
| T5 | EVSM / moment maps | Not now | HDRP filters area shadows with EVSM because area penumbrae are wide; Lux has only depth atlases and PCSS. Revisited only if Phase 2 measures banding that more taps cannot fix (open question 1) |

---

## Part 0 — Where we are

Verified by reading the source on 2026-10-24 on `feature/render-features` @ `90102c5c`, not by running
it, unless a row says so.

| Capability | State | Evidence |
|---|---|---|
| Area lights, all shapes | ✅ working (render features phases 1–2; golden `rf1`/`rf2`, validation clean per that plan) | `Components.h` `AreaLightComponent` (Shape, Radiance, Intensity, Unit, ColorTemperature, Width, Height, Radius, Length, TwoSided, Range; no shadow fields); `Scene::CollectLightEnvironment` fills `LightEnvironment::AreaLights` |
| Area GPU data | ✅ 64 B, fully packed, no spare field | `SceneRenderer.h` `AreaLight` {Position, Intensity, Radiance, Range, HalfRight, Shape, HalfUp, Flags}, `static_assert(sizeof(AreaLight) == 64)`; mirrored in `LTC.glslh` `struct AreaLight`, `AreaLightData` UBO at `(1, 18)` |
| Area upload order | ⚠️ uploads **before** shadow selection | `SceneRenderer.cpp` "Area lights: the nearest MaxAreaLights" block (~5315) submits `m_UBSAreaLights` before the spot/point block (~5355); point lights already defer their upload until after the atlas assigns tiles (~5633) |
| Area pass | ✅ full-screen additive, in the graph only with area lights | `SceneRenderer::AreaLightPass`; graph `addPass("Area Lights", …)` reads G-buffer, pre-depth, scene colour, **not** `shadowOutputs` (~4326); inputs `PassInputCommonScene \| PassInputDepth \| PassInputGBuffer`, so `SpotShadowData` is bound but `u_SpotShadowTexture` / `r_ShadowSampler` (`PassInputShadowMaps`) are not |
| Area shader | ✅ | `DeferredLighting_Area.glsl` includes `Buffers`, `PBR`, `LuxGBuffer`, `LTC`, not `ShadowMapping.glslh`; `LTC.glslh` `LuxEvaluateAreaLights(worldPosition, N, V, albedo, metalness, roughness, F0)` loops all lights |
| Transparent area lighting | ✅ | `HazelPBR_Transparent.glsl` calls `LuxEvaluateAreaLights` and already samples `u_SpotShadowTexture` for spot/point shadows |
| Spot/point atlas | ✅ working | `SceneRenderer.h` `MaxSpotShadows` 16, `MaxPointShadows` 8, `MaxShadowAtlasTiles` 64, `static_assert(MaxSpotShadows + 6 * MaxPointShadows <= MaxShadowAtlasTiles)`, so **today's caps can fill all 64 tiles**; selection, grid sizing (`m_SpotShadowAtlasGridSize` ≤ 8, one tile size for all), `m_SpotShadowUB.ViewProjection[]`, `m_SpotShadowFrustums[]` and `spotShadowStateHash` in the spot block; point faces `k_PointShadowFaceFOV` (≈96°), `k_PointShadowNearPlane` 0.1 |
| Atlas consumers are tile-generic | ✅ | `SpotShadowMapPass` loops `shadowIndex < m_SpotShadowCount` (viewport per tile); `IsSpotShadowCasterVisible` and the shadow cull's plane packing loop `m_SpotShadowFrustumCount`; so new tiles are drawn, culled and cached without changing those functions |
| Static/dynamic cache | ✅ | `SpotShadowMapPass` with `m_StaticSpotShadowMapCacheValid`, `m_StaticSpotShadowCacheImageStale`, `m_SpotShadowMapHoldsStaticOnly`; a changed `spotShadowStateHash` invalidates the static cache |
| PCSS | ✅ for spot/point | `ShadowMapping.glslh` `RawPCSS_SpotLight` (linear-depth blocker search, radius `lightRadius·(zr − zb)/zb`, search capped at `SPOT_PCSS_MAX_SEARCH_UV` 0.05), `PCF_SpotLight`, `PCSS_SpotLight` (mode switch), `SamplePointLightShadow` (face by `PointShadowFace`), taps from `RendererData.ShadowKernel` |
| `ShadowMapping.glslh` dependencies | ⚠️ unproven in the area shader | it includes `Lighting.glslh` and `GetDirShadowBias` reads `m_Params`; it has an include guard; whether the area shader can include it without the cluster and `m_Params` globals is a Phase 1 compile check |
| Editor | ✅ area section exists | `SceneHierarchyPanel.cpp` `DrawComponentSection<AreaLightComponent>` with `ApplyToSelection`; spot/point "Cast Shadows" / "Soft Shadows" rows are the pattern |
| Serializer | ✅ | `SceneSerializer.cpp` `AreaLightComponent` both directions with per-key defaults; spot uses keys `CastsShadows`/`SoftShadows` defaulting to `false` |
| Copy / prefab | ✅ whole-struct copy | `AreaLightComponent` is in the component lists in `Scene.cpp` (copy/duplicate) and `Prefab.cpp`; new fields copy with it |
| Test scene | ✅ | `scripts/GenerateBenchmarkScenes.py --scene features`: four area lights over three floor strips, a cylinder "Slot Stand" behind each slot (a fixture-like caster behind the rect and disk); no caster in front of any light |
| Golden harness | ✅ | `tests/rendering/golden_run.py` (`--scenes`, `--features` applies variants such as `PCSS=1` to the **first** scene only); `tests/rendering/run_shadow_shader.py` validates `DeferredLighting.glsl` only |
| Lucky Engine | ➖ no raster method to port | `M:\LuckyEngine\MAC-Source`, read 2026-10-24. Raster: unshadowed. `Lighting.glslh` `CalculateAreaLights` (used by `DeferredLighting.glsl`, `DeferredLighting_Area.glsl`, `HazelPBR.glslh`) has no visibility term, and `AreaLightComponent` has no shadow field. Path tracer: area lights are next-event-estimated (`LightBaker.cpp` adds them as `LightTypeRect/Disk/…`; rects use uniform spherical-rectangle sampling in `PathTracingSampling.hlslh`) with a shadow ray per sample that stops at 0.999 × the distance so the emitter does not occlude itself (`PathTracer.hlsl`). Area NEE samples always trace visibility (`result.CastsShadows = true`; only punctual lights read their flag), so Lucky's two paths disagree: shadowed when path-traced, unshadowed in raster. See Part 7 for the comparison |

---

## Part 1 — Goals and non-goals

See the Goal card. The plan is done when Phase 4's exit passes: every success criterion seen in
RenderFeatures in the editor and in an exported runtime, Benchmark identical to the Phase 0 label, and
the cost recorded.

---

## Part 2 — Design

### Data

- `AreaLightComponent` — **NEW** `bool CastsShadows = false` (Phase 1), `bool SoftShadows = true`
  (Phase 2). No "Light Size": the penumbra comes from the shape, which is the point of an area light.
- `AreaLight` (GPU) grows from 64 to **80** bytes with one `vec4`-sized block: `ShadowIndex` (first
  atlas tile), `AtlasScale`, `ShadowFlags` (**NEW** `AreaLightShadowCasts`, `AreaLightShadowSoft`,
  set by the renderer), `ShadowEmitterDepth` (the emitter's linear depth from the tile apex, rect/disk
  only). `static_assert` 80 and the `LTC.glslh` mirror change together. The UBO becomes
  16 + 32 × 80 = 2576 bytes.
- `SceneRenderer::MaxAreaShadows` — **NEW**, 8 lights.

### Tiles per shape

| Shape | Tiles | View | Projection | Near | Emitter depth |
|---|---|---|---|---|---|
| Rect / disk, one-sided | 1 | apex at `Position − n·d0`, looking along the emit direction `n`, up = the shorter of `HalfRight`/`HalfUp`, so the tile's u/v axes follow the shape's sides | perspective, vertical FOV = cone (120°) across the short side, aspect = long/short (disk 1) | `d0 + margin` (the emitter plane) | `d0 = min(halfW, halfH) · cot(cone/2)` (HDRP's offset; Part 7) |
| Rect / disk, two-sided | 2 | the same, and mirrored through the emitter plane | same | same | same |
| Sphere | 6 | cube faces from the centre, as point lights (`k_PointShadowFaceDirections/Ups`, ≈96° faces) | as point lights | `max(radius·1.05, k_PointShadowNearPlane)` | 0 (centre) |
| Tube | 6 | cube faces from the centre | as point lights | `max(radius·1.05, k_PointShadowNearPlane)` | 0 (centre) |

Far plane = the light's `Range`. Rect FOV note: the long side's FOV is `2·atan(aspect·tan(60°))`,
which reaches 159° for the RenderFeatures 1.6 × 0.5 m rect (aspect 3.2) and tends to 180° for thinner
ones. Phase 1 caps it (aspect limit, ~160°) so the projection stays finite; past the cap the long ends
rely on the edge fade. The square tile then holds a non-square frustum, so texels are anisotropic
along the long side; acceptable, checked in `as1`.

### Selection and upload (main thread, `SceneRenderer` lighting setup)

1. The area block keeps choosing the nearest 32, but **no longer submits**; it fills `m_AreaLightsUB`.
2. The spot/point block runs as today. After points, **NEW** area candidates: uploaded lights with
   `CastsShadows`, within `MaxShadowDistance` of the camera (sphere test with `Range`, as points),
   scored `Intensity · luminance · extent · Range / max(1, d²)`, sorted, and taken while
   `MaxAreaShadows` and the leftover tiles allow (D2). Tile counts per shape from the table above.
3. Each selected light gets `ShadowIndex = m_SpotShadowCount`, `AtlasScale`, flags and its tiles'
   `ViewProjection` and `Frustum`, exactly like the point loop. Its pose, shape extents, range and
   flags join `spotShadowStateHash`, and the selected area count joins the grid sizing
   (`selectedTileCount`) before `m_SpotShadowAtlasGridSize` is computed.
4. The area UBO is submitted after selection, next to the point-light upload. Unselected lights get
   `ShadowFlags = 0`.

No new thread, no new GPU resource: the atlas, its static cache, its passes, culling and the shadow
cull's plane records already handle any tile index below 64.

### Shading

- **NEW** `SampleAreaLightShadow(AreaLight light, vec3 worldPos, vec3 N, texture2D atlas)` in
  `ShadowMapping.glslh`, next to `SamplePointLightShadow`: picks the tile (rect/disk: front, or back
  for two-sided seen from behind; sphere/tube: `PointShadowFace`), applies the normal offset (T2),
  projects, and calls hard or PCSS sampling.
- `LuxEvaluateAreaLights` multiplies each light's diffuse + specular by that visibility when
  `ShadowFlags != 0`. Its signature gains the atlas (`texture2D shadowAtlas`); both callers pass
  `u_SpotShadowTexture`. Diffuse and specular share one visibility (Part 5, risk 5).
- **PCSS generalization (T1).** With the apex at depth 0, the emitter at `d0`, receiver `zr`, blocker
  `zb`, light half-extents `r = (ru, rv)` metres along the tile axes:
  - search radius (tile UV) = `UVPerMetre · r · (zr − near) / ((zr − d0) · near)`;
  - penumbra half-width at the receiver = `r · (zr − zb) / (zb − d0)` metres, converted with
    `UVPerMetre / zr`.
  With `d0 = 0` and `ru = rv` both reduce to the existing expressions, so spot and point keep their
  code path: the generalized function is **NEW** (`RawPCSS_AreaLight`) and spot/point stay on
  `RawPCSS_SpotLight` untouched, which makes the bit-identical claim trivially true. Taps scale per
  axis (`ShadowBlockerTap(i) * searchRadius.xy`), an elliptical kernel; a rect's corners are
  approximated by the ellipse. Sphere and disk use equal radii; a tube uses
  `(halfLength + radius, radius)` on the faces looking across its axis and `radius` on the two faces
  along it (the face's axes are known from `k_PointShadowFaceUps`).
  The search cap is `SPOT_PCSS_MAX_SEARCH_UV` (0.05) on cube faces (seams) and **NEW**
  `AREA_PCSS_MAX_SEARCH_UV` on rect/disk tiles, where taps outside the tile are already rejected by
  `IsSpotShadowSampleValid`; its value is measured in Phase 2 (Part 5, risk 2).

### Engine fit

| Concern | Answer |
|---|---|
| System | Renderer owns all of it; Scene adds two component fields and copies them into `AreaLight` in `CollectLightEnvironment` (the frame packet path area lights already use) |
| Thread | Component, serializer, inspector: main. Selection and matrices: main, in the lighting setup that already builds spot/point tiles. Uploads: `Renderer::Submit` lambdas as today. Shadow drawing and sampling: render thread through the graph. Same under `MultiThreaded` and `SingleThreaded` (no new ordering) |
| Ownership | No new GPU resource. The UBO grows inside the existing `UniformBufferSet` (`sizeof(UBAreaLights)` at creation) |
| Renderer data flow | Through `FrameRenderPacket` → `LightEnvironment::AreaLights` (unchanged route) |
| Invariants | No new `(set, binding)`: `(1, 18)`, `(1, 8)`, `(1, 3)`, `(3, 8)` are reused with the names they already have. The area pass's reads gain `shadowOutputs` (it now samples the atlas). The pass is still only in the graph with area lights; whether it carries shadow code is a runtime branch on `ShadowFlags`, so no permutation and no structure-hash change |
| Serialization | Scene only: keys `CastsShadows`, `SoftShadows` on `AreaLightComponent`, missing → `false` / `true`. Older builds ignore unknown keys (the deserializer reads known keys only, per the render-features plan) |
| Editor | Two rows in the area section, the spot rows' pattern (`ApplyToSelection`, mixed-value flag for multi-edit), "Soft Shadows" disabled while "Cast Shadows" is off. Nothing goes to `LuxSample.luxproj` |
| Scripting | None (no light has a C# API) |
| Runtime / Dist / export | No asset, no build flag; the shader pack is built from the shader library (`ShaderPack::CreateFromLibrary`), so the changed shaders ship |
| Linux | Portable code only; no platform file changes |
| Build | No new C++ files. A split shader include (if Phase 1 needs one) is a new file the Editor project globs (`Editor/premake5.lua`, `Resources/Shaders/**.glslh`): run `scripts\Win-GenProjects.bat` so it shows in the IDE (the engine loads shaders from disk, so it is not a link issue). No new dependency |
| Docs | `Rendering.md`: the area-light paragraph ("Area lights are unshadowed" goes), the atlas paragraph (budget order, area tiles), the PCSS paragraph (emitter depth, elliptical kernel). `Architecture-LuxEngine.md`: component field list if it lists them (line ~412 only names the component today). Comment on `AreaLightComponent` ("unshadowed") |

---

## Part 3 — Phases

Every phase: Release + Debug build; golden run against the previous label; a Debug sync-validation run
on RenderFeatures; `/cr` before commit; one commit per phase on a branch off `dev` (memory: PRs target
`dev`). Each golden or editor run needs the user's OK in that turn (memory: editor launch budget).

### Phase 0 — Casters under the area lights and baselines

**Goal:** a scene that shows area shadows, and a recorded "before".

**Changes**
- `scripts/GenerateBenchmarkScenes.py` — `--scene features` gains one caster per light: a small cube
  and a sphere on each strip between the light and the floor, at two heights (contact and ~1 m), a
  thin vertical bar under the tube, and keeps the "Slot Stand" cylinders (fixture test). Lights keep
  `CastsShadows` unset. Benchmark output unchanged (check the generated files are byte-identical).
- No engine code.

**Verification**
- `golden_run.py --label as0 --scenes RenderFeatures,Benchmark --features` (RenderFeatures first, so
  the `PCSS=1` variant is captured for it). Record the "Area Lights" and "SpotShadowMapPass" timings
  from RenderFeatures' `perf.json` in Part 4.
- Diff `Editor/LuxSampleProject/LuxSample.luxproj` first and note its `ShadowFilterMode` (memory:
  luxproj regression trap).

**Exit:** label `as0` exists, RenderFeatures shows the casters unshadowed by the area lights (lit by
the sun's shadow only), numbers recorded. **Rollback:** revert the script.

**Status 2026-10-24: scene done, capture folded into Phase 1.** `--scene features` appends three
"Shadow Cube" / "Shadow Sphere" pairs on the strips and a "Shadow Post" under the tube, after every
existing entity so their UUIDs are unchanged; the RF materials and `AssetRegistry.lzr` are untouched.
`as0` was not captured separately: Phase 1 compares Benchmark against `rf9` (the reference for
`90102c5c`, the commit this work starts from).

### Phase 1 — Hard shadows from one-sided rect and disk lights (vertical slice)

**Goal:** a rect or disk light with "Cast Shadows" on shadows opaque and transparent receivers with a
hard (2×2 hardware PCF) edge, saved with the scene, cached when static.

**Changes**
- `Components.h` — `AreaLightComponent::CastsShadows` (D1); comment updated.
- `SceneSerializer.cpp` — write `CastsShadows`; read with default `false`.
- `SceneHierarchyPanel.cpp` — "Cast Shadows" row in the area section, tooltip about fixture meshes.
- `Scene.cpp` `CollectLightEnvironment` — set `AreaLightShadowCasts` in the light's flags request
  (renderer clears it if not selected).
- `SceneRenderer.h` — `AreaLight` 80 B (+ `static_assert`), **NEW** `AreaLightShadowFlags`,
  `MaxAreaShadows`; `static_assert` that the caps stay ≤ 64 is kept (area tiles are leftovers).
- `SceneRenderer.cpp` — move the area UBO submit after selection; **NEW** area candidate loop for
  rect/disk (one tile, front only; two-sided lights shadow their front face only in this phase);
  apex-offset view/projection; state hash; grid sizing counts area tiles; area pass gets
  `PassInputShadowMaps` and `shadowOutputs` in its graph reads.
- `LTC.glslh` — struct mirror; `LuxEvaluateAreaLights(..., texture2D shadowAtlas)` multiplies
  visibility. `DeferredLighting_Area.glsl` and `HazelPBR_Transparent.glsl` pass `u_SpotShadowTexture`.
- `ShadowMapping.glslh` — **NEW** `SampleAreaLightShadow` (hard path + normal offset, T2).
  If including it in the area shader drags in globals the pass does not bind, split the spot/point
  sampling helpers into **NEW** `Include/GLSL/LocalLightShadows.glslh` included by both (then run
  `scripts\Win-GenProjects.bat`). Settle this first, by compiling.
- `tests/rendering/run_shadow_shader.py` — also compile/validate `DeferredLighting_Area.glsl` and run
  the local-array check on it.

**Playbook:** component (struct field, serializer both directions, inspector; copy and prefab are
whole-struct through the component lists in `Scene.cpp` and `Prefab.cpp`); render pass (reads updated; no new pipeline).

**Thread and lifetime:** as Part 2; nothing new to free.

**Verification**
- Build Release, Debug, Lux-Runtime. `run_shadow_shader.py` passes.
- Golden: Benchmark and every variant identical to `as0`; RenderFeatures (lights still off) identical
  to `as0`. Then turn shadows on for the rect and disk in the generator and capture `as1`: the casters
  shadow their strips, the slot stands behind the lights cast nothing onto them, receivers beyond 60°
  fade to unshadowed without a hard line. The user looks at `as1` and accepts it as the reference.
- Editor (with the user's OK): toggle "Cast Shadows" on one and on a multi-selection (mixed value),
  move the light (shadow follows, static cache re-renders once), move a caster (dynamic path), stop it
  (settling: still shadowed), save/reload, undo. Check `SpotShadowMapPass` is not re-rendering every
  frame in a static scene (Renderer Debugger / perf.json).
- Acne/peter-panning check on the floor and on a sloped caster at grazing angles; tune the
  normal-offset factor.
- Log: Debug + sync validation clean; no binding collision; no new warnings.
- ImGui: the new row inside the existing section scope, `ApplyToSelection` per pattern, ID unique in
  the section; driven in the editor.

**Docs:** `Rendering.md` area and atlas paragraphs; `Components.h` comment.

**Exit:** the checks above. **Rollback:** revert the commit; scenes saved with `CastsShadows` load in
older builds with the key ignored.

**Status 2026-10-24: code written, not yet run.** `AreaLightComponent::CastsShadows` (key
`CastsShadows`, default `false`, inspector row with the fixture tooltip); `AreaLight` 80 B with
`ShadowIndex` / `AtlasScale` / `ShadowFlags` / `ShadowEmitterDepth`; `MaxAreaShadows` 8 from the
leftover tiles; `ComputeAreaShadowFrustum` (constants `k_AreaShadow*` in `SceneRenderer.cpp`); the area
UBO upload moved after the selection; the area pass binds `PassInputShadowMaps` and reads
`shadowOutputs`. One change from the design: the sampling function is `LTC_AreaLightShadow` in
`LTC.glslh` (the owner of `struct AreaLight`), which now includes `ShadowMapping.glslh`; the area
shader declares `u_SpotShadowTexture` at `(1, 3)` as `DeferredLighting.glsl` does. No include split
was needed. `run_shadow_shader.py` now checks `DeferredLighting_Area.glsl` too (passes), and the
transparent fragment stage compiles offline. RenderFeatures turns shadows on for the rect and disk.
Release and Debug (Editor, Lux-Runtime) build with no new warnings. Pending: golden `as1` (Benchmark
against `rf9`), Debug sync validation, the editor checks.

### Phase 2 — Soft shadows sized by the emitter

**Goal:** "Soft Shadows" gives a contact-hardening penumbra whose width follows the light's size, wider
along a rect's long side.

**Changes**
- `AreaLightComponent::SoftShadows` (+ serializer key, default `true`; inspector row, disabled while
  shadows are off).
- `ShadowMapping.glslh` — **NEW** `RawPCSS_AreaLight` (emitter depth, `vec2` radius, elliptical taps,
  own search cap) and the `ShadowFilterMode` switch (T4); `SampleAreaLightShadow` uses it when soft.
  `RawPCSS_SpotLight` / `PCF_SpotLight` are not edited.
- `SceneRenderer.cpp` — `ShadowEmitterDepth` and soft flag filled per light; half-extents reach the
  shader through `HalfRight` / `HalfUp` (already uploaded).

**Verification**
- Golden: Benchmark identical to `as1` (spot/point untouched). RenderFeatures `PCSS=1` variant: contact
  sharp, widening with height; rect penumbra visibly anisotropic. Accept as `as2`.
- Search-cap sweep (0.05 / 0.1 / 0.15 / 0.25) on the 1.6 × 0.5 m rect at 2.2 m: pick the smallest
  without a visible penumbra clamp, record banding at each shadow quality tier (blocker/filter sample
  counts from the tier). If banding is unacceptable even at the top tier, stop and raise open
  question 1 with the user before Phase 3.
- Numbers: "Area Lights" pass GPU time, A-B-A in one session against Phase 1 (`/profile` protocol),
  with 2 shadowed lights on RenderFeatures.
- RADV/local-array check (`run_shadow_shader.py`) passes: taps come from `RendererData.ShadowKernel`.
- ImGui: as Phase 1.

**Docs:** `Rendering.md` PCSS paragraph (emitter depth, elliptical kernel, area search cap).

**Exit:** checks above, cost recorded. **Rollback:** revert; hard shadows from Phase 1 remain.

**Status 2026-10-24: code written, not yet run.** `AreaLightComponent::SoftShadows` (key `SoftShadows`,
default `true`; inspector row disabled while shadows are off, tooltip names Shadows > Filter);
`AreaLightShadowSoft`; `RawPCSS_AreaLight` in `LTC.glslh` as designed, search cap 0.15 to start (the
sweep is still to do). The tile's per-axis UV-per-metre comes from rows 0 and 1 of its matrix. Spot and
point PCSS untouched. `ComputeAreaLightShadowFrustum` is now public (`SceneRenderer.h`) for the editor.

Added with it, at the user's request (editor only): the viewport settings popup has "Camera Frustums"
and "Light Volumes" (Off / Selected / All, default Selected, saved as `Editor.CameraFrustums` /
`Editor.LightVolumes`, suspended in Play like the other overlays). Camera frustums are drawn from the
camera's FOV or ortho size, its last-rendered aspect and its near plane, cut at 50 m, with an up
marker. Light volumes: point light range spheres, spot light cones to their range, a shadowed
rect/disk area light's actual shadow frustum, and a range sphere for other area lights.
Pending: builds, golden, the cap sweep, the editor checks.

### Phase 3 — Sphere and tube lights, two-sided rect and disk

**Goal:** every shape shadows; a two-sided light shadows both sides.

**Changes**
- `SceneRenderer.cpp` — sphere/tube take six cube tiles (point-light face directions, near at
  1.05 × radius); two-sided rect/disk take a second, mirrored tile; tile counting in the leftover
  budget per shape.
- `ShadowMapping.glslh` — face selection for sphere/tube (`PointShadowFace`) with the 0.05 cap, tube
  radii per face, back-tile choice for two-sided lights.

**Verification**
- Golden: Benchmark identical to `as2`. RenderFeatures with sphere and tube shadows on (and the rect
  made two-sided with a caster behind it): sphere shadows soft all around, no seams at cube-face
  boundaries, the tube's bar shadow stretched along its axis, both sides of the two-sided rect
  shadowed. Accept as `as3`.
- Budget: a test scene (or generator flag) with 16 shadowed spots + 8 shadowed points + area lights
  drops the area shadows without errors and logs one warning; with 2 spots and 4 area lights the
  atlas grid grows as expected.
- Editor: shape switches with shadows on (rect → sphere → tube) re-select tiles without stale tiles.

**Docs:** `Rendering.md` tile table for area lights.

**Exit:** checks above. **Rollback:** revert; rect/disk one-sided shadows remain (two-sided falls back
to front-only).

**Status 2026-10-25: code written, not yet run.** `GetAreaLightShadowTileCount` (1 / 2 / 6 tiles);
selection takes lights in score order while their tiles fit (a light that does not fit is skipped, a
smaller one may still fit). Two-sided back tile: `ComputeAreaLightShadowFrustum` with `HalfUp` negated.
Sphere/tube: point-light faces, near at `k_AreaShadowCubeNearScale` (1.05) × radius. Shader: the shadow
is now read before `LTC_FacingShape` replaces a sphere or tube (it would have pointed at the wrong
tiles); `LTC_AreaLightTileRadius` gives the per-tile radius generically from the tile matrix's rows, which
also replaces the Phase 2 long/short assumption for rects; cube faces keep the 0.05 cap. Editor: Light
Volumes draws both frustums of a two-sided light; the Shadows tab labels area cube faces (`A2 +X`) and
back tiles (`A0 back`) and counts lights, not tiles. RenderFeatures now shadows all four lights (the
two-sided case is still tested by toggling a rect in the editor). Pending: builds, golden `as3`, the
budget test, the editor checks.

### Phase 4 — Hardening, runtime parity, budget visibility

**Goal:** the feature is shippable and its cost and limits are visible.

**Changes**
- Renderer Debugger / `Statistics`: shadowed area lights and their tile count beside the spot/point
  counts; the one-time warning when area lights lose shadows to the budget.
- Docs pass over `Rendering.md` and `Architecture-LuxEngine.md`.

**Verification**
- Export the sample project and run Lux-Runtime: RenderFeatures shows the same area shadows (golden
  capture of the runtime if the harness supports it, otherwise a screenshot compared by eye).
- Linux: the changed C++ and shaders are portable; build on Linux if a machine is available,
  otherwise record "not built here".
- Threading: `threading_ab.py` on RenderFeatures with shadows on, Single vs Multi identical.
- Final numbers in Part 4: Benchmark unchanged; RenderFeatures with all four lights shadowed (frame,
  GPU, "Area Lights", "SpotShadowMapPass").

**Exit:** all Goal-card success criteria. **Rollback:** not needed; this phase adds no behaviour.

**Status 2026-10-24: budget visibility done early (user's request).** The Renderer Debugger has a
Shadows tab: the atlas image with every tile outlined and labelled by light (S spot, P point and cube
face, A area), a hover tooltip with the light's position and tile size, the atlas/grid/tile sizes, and
"N of M shadowed" per light type with a warning when lights got no tile. Data from
`SceneRenderer::GetShadowAtlasDebugInfo()`.

**Status 2026-10-25: code done; the runtime checks are the user's.** One-time `Renderer` warning when
shadowed area lights get no tile (`m_AreaShadowBudgetWarned`). Release and Debug build Editor and
Lux-Runtime; Dist builds Lux-Runtime (the solution builds no Dist Editor). Threading by reading: every new piece of state (`m_ShadowAtlasDebugInfo`, the candidate
lists, the area UBO contents) is written and read on the main thread, and the GPU sees it only through
the `Renderer::Submit` copies made after the selection, the same path the point lights use, so both
policies see one frame's data. Not run here (each needs the editor, launched only with the user's OK):
the golden sets `as1`–`as3`, the search-cap sweep, `threading_ab.py`, a runtime export of
RenderFeatures, Debug sync validation. Linux: no platform code changed; not built here.

---

## Part 4 — Verification

- **Golden sets:** `as0` … `as3`, Release, 1280×720, `--scenes RenderFeatures,Benchmark --features`
  (RenderFeatures first for its `PCSS=1` variant). Benchmark is identical across every label; any
  difference there is a regression. RenderFeatures changes are shown to the user and accepted as the
  new reference (memory: golden changes aren't rejections). Known noise: a 60 px max-1 difference
  after a hitch (memory: golden settle residual); re-run before blaming a change.
- **Before every compare:** diff `LuxSample.luxproj`; a changed `ShadowFilterMode` changes every
  shadowed pixel.
- **Validation:** Debug + `--sync-validation` on RenderFeatures each phase.
- **Perf:** A-B-A, medians of 3, one session. Baselines from Phase 0 go here:

  | Label | Scene | Frame | GPU | Area Lights | SpotShadowMapPass |
  |---|---|---|---|---|---|
  | as0 | RenderFeatures | | | | |

---

## Part 5 — Risks

1. **Atlas dilution (likely).** All tiles share one size; every area tile can grow the grid and halve
   the resolution of every spot and point shadow (2 tiles → 2×2 grid). Existing behaviour for point
   lights, but now an area light can soften a spot light's shadow. Detected in Phase 3's budget test
   and shown in Phase 4's statistics.
2. **Wide penumbrae vs. a fixed tap count (likely).** A 1.6 m rect 1 m above a caster gives a penumbra
   of ~0.1 tile UV, far past the spot cap of 0.05 and spread over hundreds of texels with ≤ 32 taps:
   banding. Spatial remedies only: a larger cap, more taps at higher tiers, the hardware 2×2 PCF per
   tap. Measured in Phase 2; if it fails, open question 1.
3. **Single-viewpoint error (certain, accepted).** A shadow map from one point cannot see around an
   occluder the way the light's edges do: umbrae are slightly too large and light that leaks around a
   caster's side is missed. Standard for raster area shadows (Godot, HDRP); documented.
4. **Off-cone leaks (certain, accepted).** Receivers outside the 120° cone are unshadowed. Faint (they
   see the rect at a grazing angle), faded at the edge. The hemicube alternative (D3) exists.
5. **Specular under partial shadow (accepted).** Diffuse and specular share one visibility, so a
   mirror-like floor reflecting a large, half-occluded light dims the whole reflection instead of
   clipping it. Same compromise as every analytic-light + shadow-map engine.
6. **Shader include tangle (possible).** `ShadowMapping.glslh` pulls `Lighting.glslh` and `m_Params`;
   Phase 1 compiles first and splits a small local-light shadow include if needed.
7. **Fixture meshes inside the emitter (possible).** An emissive panel mesh slightly in front of the
   light plane will shadow everything. Near-plane margin plus the inspector tooltip; a "do not shadow
   my own light" link between a mesh and a light is out of scope.
8. **Static-cache churn.** Moving any shadowed local light re-renders the whole atlas static cache (one
   state hash). Existing behaviour; an animated area light costs what an animated spot light does.

## Part 6 — Open questions

1. If Phase 2 shows banding no tap count fixes, is a filtered shadow format for area tiles wanted
   (EVSM or moment shadow maps: a moment atlas plus a spatial blur, still per frame, no history)? It
   is a new atlas format and cache path, so it would be its own plan. Resolve with the user after the
   Phase 2 sweep.
2. D1–D4 above: the plan runs on the recommendations until the user answers.
3. Is a 120° cone right for the RenderFeatures layout, or should the cone be a per-light advanced
   property (HDRP exposes it, 10–179°)? Start fixed; revisit after `as1`.

**What would invalidate the plan:** if the atlas can't take a frustum this wide without unusable
texel density (seen in `as1` as acne no normal offset fixes), rect/disk move to the hemicube (D3's
alternative), which changes the tile counts and Phase 1's selection code.

---

## Part 7 — Research notes

### Research brief — area light shadows without ray tracing
**Goal:** soft, size-correct area-light shadows from the existing atlas and PCSS, nothing temporal.
- **Prior art (HDRP):** a rectangle light's shadow is a spot-light shadow map with the rect's aspect,
  a cone angle (`areaLightShadowCone`, default 120°, clamped 10–179°), and the apex pulled back along
  the light's axis by `min(w, h)/2 · cot(cone/2)` so the whole shape sits inside the cone
  (`GetAreaLightOffsetForShadows`, `ExtractRectangleAreaLightData` → `ExtractSpotLightMatrix`). Taken
  for D3/T3 (ideas only; HDRP is under the Unity Companion License).
  [HDShadowUtils.cs](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.high-definition/Runtime/Lighting/Shadow/HDShadowUtils.cs),
  [HDAdditionalLightData.cs](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.high-definition/Runtime/Lighting/Light/HDAdditionalLightData.cs)
- **Prior art (HDRP filtering):** area-light shadows are EVSM-filtered (exponent default 15, 0–8
  blur passes), PCSS at the High quality level; i.e. Unity also found plain PCF too narrow for area
  penumbrae. Informs T5 and open question 1.
  [feature comparison](https://docs.unity3d.com/cn/2023.2/Manual/render-pipelines-feature-comparison.html),
  [HDAdditionalLightData API](https://docs.unity3d.com/cn/Packages/com.unity.render-pipelines.high-definition@7.4/api/UnityEngine.Rendering.HighDefinition.HDAdditionalLightData.html)
- **Prior art (Godot 4.x AreaLight3D):** rectangular LTC area light; the shadow map is drawn from the
  light's centre with PCSS softness from `light_size`; warns that coarse casters very close to the
  light look wrong (the omni dual-paraboloid limitation). Confirms the representative-point + PCSS
  approach and risk 3/7. [AreaLight3D](https://docs.godotengine.org/en/latest/classes/class_arealight3d.html)
- **Prior art (Unreal):** a rect light's softness comes from its width/height; Epic warns that light
  shapes intersecting shadow-casting geometry cause artifacts (T3). Its VSM path softens local lights
  with SMRT, a few rays into the virtual shadow map per pixel, whose noise is resolved temporally.
  [Rect lights](https://dev.epicgames.com/documentation/en-us/unreal-engine/rectangular-area-lights-in-unreal-engine),
  [Virtual shadow maps](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine)
- **Prior art (Lucky Engine):** raster area lights unshadowed; the path tracer shadows them with one
  ray per light sample, accumulated over frames. Comparison below.
- **Concept (PCSS, Fernando 2005):** penumbra = light size × (receiver − blocker) / blocker, from an
  averaged blocker depth. Lux already uses it in linear depth; the generalization here only moves the
  emitter off the apex (`zb − d0`) and makes the size two-dimensional.
- **Rejected — SMRT / stochastic shadow rays:** noise needs temporal resolve (hard rule).
- **Rejected — Heitz et al. 2018 ratio estimator (analytic LTC × stochastic shadow ratio):** needs ray
  tracing and denoising (out of scope, temporal denoisers).
- **Rejected — per-pixel rotated PCSS kernels:** turn banding into static grain that only TAA hides;
  Lux's kernel stays fixed (`ShadowKernel`).
- **Deferred — EVSM / moment shadow maps:** spatial and per-frame, so allowed, but a new atlas format
  and cache path (open question 1).

**Informs decisions:** D3, T1, T3, T5. **Still unknown:** the banding level at Lux's tap counts
(Phase 2 measures it).

### Lucky Engine's approach against this plan

| | Lucky raster | Lucky path tracer | This plan |
|---|---|---|---|
| Area shadows | none | exact visibility per sample | shadow map from one point + PCSS sized by the shape |
| Penumbra | – | correct (each ray aims at a point on the emitter) | approximate: umbra slightly too large, no light around a caster's sides (risk 3) |
| Specular under shadow | – | correct per sample | one visibility for diffuse and specular (risk 5) |
| Noise | – | converges by accumulating frames, plus a denoiser | none; fixed kernel, per frame |
| Needs | – | RT hardware, TLAS/BLAS, accumulation | the existing atlas, PCSS, caches |
| Allowed in Lux | – | no: ray tracing is out of scope and starts from scratch, and accumulation is temporal | yes |

**Verdict:** for Lux this plan is the better choice, because Lucky has no raster method and its path
tracer's method breaks two standing rules. It is not the more accurate method. Traced visibility is
ground truth; this plan is the standard raster approximation (HDRP, Godot), with its errors listed in
Part 5. Three things are taken from Lucky:
- the emitter must not occlude itself: its rays stop at 0.999 × the distance, this plan's near plane
  sits at the emitter (T3);
- one `CastsShadows` meaning on every path: Lucky's raster and path tracer disagree on area lights.
  A future Lux ray-traced path must honour the same flag the raster path reads;
- spherical-rectangle sampling is the reference to compare against if Lux ever builds a path-traced
  ground truth. That is not part of this plan.
