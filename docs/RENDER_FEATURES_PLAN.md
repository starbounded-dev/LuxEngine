# LuxEngine Render Features Plan

Ports the rest of the Lucky Engine shader catalog (`C:\Users\sheaz\Documents\Shaders`, a Hazel + NRI
fork the user collected on 2026-10-14) into LuxEngine as new features: area lights, glass refraction,
FFT convolution bloom, frosted-glass editor overlays, then three performance items (a slimmer
G-buffer, a working meshlet path, QTangent vertices). It is a planning document, not a description of
what exists; `.claude/docs/Rendering.md` and `.claude/docs/Architecture-LuxEngine.md` describe what is
built. The Lucky shaders are reference only: their license is unknown, so everything is reimplemented,
never pasted.

**Goal card**

- **Goal:** the remaining techniques from the user's shader set become LuxEngine features, features
  first, then the performance items.
- **Success:** rect, disk, sphere and tube lights light the scene with LTC highlights in the editor
  and the runtime; glass bends and blurs what is behind it by roughness and IOR; bloom is FFT lens
  glare everywhere; viewport overlays and popups have a frosted-glass backdrop that an editor
  preference turns off; the G-buffer is measurably cheaper with an identical image; the meshlet path
  beats or matches the classic prepass on Benchmark (or stays off with the reason recorded);
  vertices are 24–28 bytes instead of 56 with an identical image.
- **Non-goals:** shadowed area lights (follow-up); skeletal animation and the `_Anim` shaders (their
  own plan, the user's choice); path tracing, `PTTaa`, DLSS, NIR/RecordPack, tiled light culling,
  exposure histogram (already have auto exposure); a C# API for lights (none exists for any light).
- **Constraints:** no temporal anything (hard rule); existing `.luxscene` / `.lmat` / `.luxproj`
  load unchanged; mesh caches migrate without user action; correct under both threading policies;
  works in Lux-Runtime, Dist and exported games; no new installs; reimplement, never paste.
- **User's words:** "features first"; area lights "all shapes, unshadowed"; FFT bloom "replace
  current bloom"; glass UI on "overlays and popups" behind a preference.

**Decisions this plan is built on**

| Decision | Choice | Consequence |
|---|---|---|
| Order | Features first, then perf (user) | Visible progress early; perf items land on the finished feature set |
| Area-light scope | Rect, disk, sphere, tube; no shadows (user) | Shadowed area lights are a follow-up |
| Area-light culling | Unculled loop over a capped list (32), a separate additive full-screen pass issued only when the scene has area lights | Zero cost without area lights; clustered culling only if a measurement asks for it |
| Area-light math | LTC (Heitz et al. 2016), fitted tables vendored from `selfshadow/ltc_code` (BSD-2-Clause, notice kept) | ~80 KB of LUT data, two RGBA16F 64×64 textures |
| Refraction model | glTF `KHR_materials_transmission` + `KHR_materials_ior` + `KHR_materials_volume` inputs, screen-space lookup into the existing pre-convolved scene chain | Matches the foliage plan's glTF-first choice; no new texture chain |
| Bloom | FFT convolution bloom replaces the mip-chain bloom (user) | Every scene's look changes once; cost is measured before the old bloom is removed |
| Glass UI | Frosted backdrop behind viewport overlays and popups, editor preference, default on (user) | Pure editor work; no game-runtime change |
| G-buffer slimming | Measure first; drop the unread velocity target; octahedral normals; 8-bit sRGB base colour | ~12 B/px less if all three land; each kept only if the image is identical |
| Meshlet path | Diagnose the 25.9 ms prepass first; meshlet G-buffer only after the prepass wins | May end as "stays off, reason recorded" |
| QTangent | Quaternion tangent frame (RGBA16 snorm) with the reflection sign, mesh cache version 2, old caches rebuilt from source | Smaller vertex buffers; one forced re-cook per mesh |
| PRs | One branch and PR per phase group (area lights, refraction, bloom, glass UI, each perf item) | Reviewable pieces; each group is shippable |

---

## Part 0 — Where we are

Verified by reading the source on 2026-10-22, not by running it, unless the row says so. The plan
branches from `dev` (`9a231480`); the foliage work in open PR #34 is not in it yet, and phases that
touch the G-buffer or materials rebase onto it once it merges.

| Capability | State | Evidence |
|---|---|---|
| Catalog items already ported | ✅ on dev | physical punctual lighting `ad5196f6`, hardware PCF + point shadows `d094e012`, ortho + sky rotation + grid `17d1cd9d`, sRGB mips `a058927b`/`ece01f54`, GTAO bitmask `22e222dd`, foliage glTF alpha import, two-sided, Cutout shadows `fbe55045`/`cc89434f`/`9a231480`; the rest of the foliage plan (coverage mips, diffuse transmission, two-sided transparents, prepass changes) is in open PR #34, not on dev |
| Light types | ✅ directional, point, spot, sky only | `SceneRenderer.h` — `DirectionalLight`, `PointLight` (48 B), `SpotLight` (96 B); `Components.h` — `DirectionalLightComponent`, `PointLightComponent`, `SpotLightComponent`, `SkyLightComponent` |
| Light gathering | ✅ main thread, into the frame packet | `Scene.cpp:1906` `Scene::CollectLightEnvironment()`; `Scene.cpp:2297` `packet.Lights = CollectLightEnvironment()` |
| Light component touch points | ✅ located | `SceneSerializer.cpp`, `Prefab.cpp`, `Scene.cpp`, `Editor/Source/Panels/LightSettingsPanel.cpp`, `EditorLayer.cpp`; `Components.h:660` `AllComponents` |
| Area lights | ❌ none | no `LTC`/`AreaLight` symbol in `Core/Source` or `Editor/Resources/Shaders` |
| Clustered light lists | ✅ point + spot only | bindings `(1,20)`–`(1,25)` in `ClusterLightCulling.glsl` |
| Transparent materials | ⚠️ alpha only, no transmission | `MaterialAsset.h` — `Transparency` only on dev; PR #34 adds diffuse transmission (`DiffuseTransmission*`) |
| Pre-convolved scene chain | ✅ exists, but only with SSR on | `SceneRenderer.cpp:4369` `if (m_Options.EnableSSR)` around `addPass("Pre-Convolution")`; it runs before `Transparent Forward` |
| Bloom | ✅ mip-chain bloom | `PostProcessing/Bloom.glsl`; `PostProcessSettings.h` — `BloomThreshold`, `BloomIntensity`, `BloomDirtIntensity`; `TieringSettings.h:54` `EnableBloom` |
| HLSL compute shaders | ✅ compile today | `PostProcessing/GTAO.hlsl` |
| ImGui custom draw callbacks | ✅ honoured | `ImGuiRenderer.cpp:420`–`470` — `drawCommand.UserCallback` |
| G-buffer | ⚠️ wide | `SceneRenderer.cpp:1908`–`1917`: base colour RGBA16F, normal RGBA16F (w = `Reserved0` on dev; PR #34 stores diffuse transmission there), metal/rough RGBA8, material/object RG32UI, velocity RG16F, emissive RGBA16F |
| Velocity target | ⚠️ written, never read | written in `GBuffer_Static.glsl:199`; no shader reads it (no temporal passes exist) |
| Mesh-shader prepass | ⚠️ works, very slow | `SceneRenderer.cpp:1718` `m_PreDepthMeshletPass`; golden `p7A` perf.json: prepass 25.9 ms with `MeshShaders=1` vs 0.16 ms classic, GPU 28.3 vs 2.8 ms (measured 2026-10-19) |
| Vertex format | ⚠️ 56 bytes | `Mesh.h:23` `Vertex` {Position, Normal, Tangent, Binormal: vec3; Texcoord: vec2}; layout at `SceneRenderer.cpp:1421` |
| Mesh cache | ✅ versioned | `MeshSourceFile.h` — `FileHeader::Version = 1`; written by `MeshRuntimeSerializer.cpp` |
| glTF material import | ✅ Assimp | `AssimpMeshImporter.cpp` (maps alphaMode and doubleSided on dev; PR #34 adds diffuse transmission) |
| C# light API | ❌ none | no light component in `ScriptGlue.cpp` |

---

## Part 1 — Goals and non-goals

See the Goal card. Each phase below states its own exit check; the plan is done when every
success criterion has passed in its phase and the final golden set (Part 4) is clean.

---

## Part 2 — Design

### Area lights

- **Data flow:** **NEW** `AreaLightComponent` (shape: Rect / Disk / Sphere / Tube; colour, intensity in
  physical units like the punctual lights, width, height or radius, length for tubes, two-sided,
  range). `Scene::CollectLightEnvironment()` fills **NEW** `LightEnvironment::AreaLights` (capped at
  **NEW** `MaxAreaLights = 32`, nearest-to-camera first when over the cap, with a one-time warning).
  The renderer uploads them to a **NEW** storage buffer and binds the two LTC LUT textures.
- **Pass:** a **NEW** additive full-screen pass after `Deferred Lighting`, added to the graph only when
  the frame has area lights, so its absence changes the structure hash and costs nothing. Transparent
  forward shading evaluates the same function so glass is lit by area lights too.
- **Threads:** gathering on the main thread (as today); upload and pass on the render thread through
  the graph. No new thread.
- **Ownership:** the LUT textures are `Ref<Texture2D>` created once in `SceneRenderer::Init()` from
  vendored data; freed with the renderer.
- **Bindings:** the SSBO slot and the two texture slots are global (`(set, binding)` is one namespace
  across all shaders); each is chosen by grepping `Editor/Resources/Shaders` at the start of Phase 1.

### Glass refraction

- **Material:** **NEW** `MaterialAsset` inputs `Transmission` (0–1), `IOR` (default 1.5), `Thickness`,
  `AttenuationColor`, `AttenuationDistance`, serialized with defaults so old `.lmat` files load as
  today. The importer maps the three glTF extensions.
- **Shading:** the transparent shader refracts the view ray by IOR and thickness, projects the exit
  point to the screen, samples the pre-convolved opaque scene at a mip chosen from roughness, applies
  Beer–Lambert attenuation, and blends with the surface's reflection by Fresnel. Off-screen lookups
  fall back to the sky/environment radiance. No history, no accumulation.
- **Graph:** `Pre-Convolution` runs when SSR is on **or** a refractive draw is in the frame; the
  condition is folded into the structure hash.

### FFT bloom

- **Chain:** quarter-resolution energy-conserving downsample → threshold into three real planes inside
  a padded transform grid → row/column FFT → multiply by the normalized kernel spectrum → inverse FFT
  → resolve into the bloom texture the composite already adds. The kernel spectrum is generated and
  normalized once, and again only when kernel settings change.
- **Settings:** `BloomThreshold`, `BloomIntensity` and `BloomDirtIntensity` keep their meaning; **NEW**
  kernel settings (size, streak count, chromatic spread) join `PostProcessSettings` with defaults
  chosen so the default look is a soft glare. `TieringSettings::EnableBloom = false` still skips it.

### Editor glass UI

- **Mechanism:** an `ImDrawCallback` before each overlay or popup background copies the region
  already drawn into a backdrop texture, blurs it (dual Kawase, a few passes), and draws the glass
  composite (tint, edge highlight, optional dispersion) as the background. Editor-only code in
  `Editor/` or `Core/Source/Lux/ImGui/`; nothing in the game runtime.
- **Preference:** **NEW** editor preference "Glass overlays" (default on). Off draws today's flat
  backgrounds.

### Performance items

- **G-buffer:** one encode/decode point, `Include/GLSL/LuxGBuffer.glslh`, so consumers (deferred
  lighting, SSR, GTAO, G-buffer debug, transparent passes) change together.
- **Meshlets:** diagnosis before design; the classic path stays the default until the meshlet path
  wins on Benchmark.
- **QTangent:** the importer orthogonalizes each frame and keeps its reflection sign; the shaders
  rebuild normal and tangent from the quaternion. The mesh cache version moves to 2.

---

## Part 3 — Phases

### Phase 0 — A feature test scene and baselines

**Goal:** every later phase has a scene that exercises its feature and a recorded "before".

**Changes**
- `scripts/GenerateBenchmarkScenes.py` — **NEW** `RenderFeatures.luxscene` output (Benchmark itself stays
  unchanged so its golden and perf history stays comparable): a stage with glossy and rough floor
  strips, slots for area lights, glass panes and spheres of several roughness values, bright
  emissive points for bloom. Until the components exist, the slots are empty markers.
- No engine code.

**Verification:** `golden_run.py --scenes Benchmark,RenderFeatures --features` captures a baseline label;
the Benchmark perf.json pass timings (Bloom, GBuffer, DeferredLighting, PreDepth) are recorded in this
file's Part 4.

**Exit criteria:** baseline label exists; numbers recorded. **Rollback:** delete the scene.

**Status 2026-10-22: done** on `feature/render-features`. `GenerateBenchmarkScenes.py --scene features`
writes `RenderFeatures.luxscene` (53 entities: floor strips of roughness 0.05/0.3/0.7 with rect,
disk, sphere and tube slots; clear, frosted, rough and tinted glass in front of a dark-grid wall;
three bright emitters and an emissive bar; chrome and satin spheres; dusk sun and sky) and 15
`RF_*` materials under handles `16000000000000001000`+; the default Benchmark output was checked
identical to the original script's (scene, materials, registry). Golden `rf0`: 18 captures, 0
validation ids. Numbers in Part 4.

### Phase 1 — Rect and disk area lights (vertical slice)

**Goal:** a rect or disk light placed in the editor lights opaque surfaces with LTC diffuse and
specular, saved and loaded with the scene.

**Changes**
- `Core/Source/Lux/Scene/Components.h` — **NEW** `AreaLightComponent`; add to `AllComponents`.
- `SceneSerializer.cpp` (both directions), `Prefab.cpp`, `Scene.cpp` (copy/duplicate and
  `CollectLightEnvironment`) — the component playbook.
- `Editor/Source/Panels/LightSettingsPanel.cpp` and the entity inspector — area-light UI; a gizmo
  outline of the shape in the viewport.
- `SceneRenderer.h` — **NEW** `AreaLight` GPU struct with a `static_assert` on its size, `MaxAreaLights`,
  `LightEnvironment::AreaLights`.
- `SceneRenderer.cpp` — LUT textures in `Init()`; SSBO upload; **NEW** "Area Lights" graph pass, added
  only when `AreaLights` is non-empty.
- `Editor/Resources/Shaders/Include/GLSL/LTC.glslh` — **NEW**: edge form factors, rect and disk
  integrals, LUT sampling. `DeferredLighting_Area.glsl` — **NEW** pass shader.
- `Core/vendor/ltc/` — **NEW** LUT data + BSD-2-Clause notice (from `selfshadow/ltc_code`).
- Project regeneration (new files).

**Playbook:** Architecture Part 4 — components (declaration, copy/duplicate/prefab, serializer both
directions, inspector, `FrameRenderPacket`) and render pass (pipeline once in `Init()`, shader
dependency registered, accurate reads/writes, structure hash).

**Thread and lifetime:** component and gathering on the main thread; buffer upload and pass on the
render thread via the graph; LUTs owned by `SceneRenderer`.

**Verification**
- Build Release and Debug; `scripts\Win-GenProjects.bat` first.
- Run: place a rect and a disk light in `RenderFeatures`; the highlight stretches with the shape on
  glossy floors, softens on rough ones; save, reload, values intact; delete/undo works.
- Log: Debug + sync validation golden run clean; no binding collision.
- Golden: Benchmark and every variant pixel-identical to Phase 0 (no area lights there);
  `RenderFeatures` with lights captured as the new reference after the user looks at it.
- ImGui: every new `Begin*`/`Push*`/`TreeNode*` closed on every path, unique IDs, each new widget
  driven in the editor.

**Docs:** `Rendering.md` (area-light pass, LUT bindings, cap), `Architecture-LuxEngine.md` (component).

**Exit criteria:** the checks above. **Rollback:** revert the branch; scenes saved with the component
would drop it on load in older builds (unknown components are skipped — verify in Phase 1).

**Status 2026-10-22: code done on `feature/render-features`, editor checks pending.** `AreaLightComponent`
(rect / disk, Unitless = luminance or Lumens) through every component touch point; `LightEnvironment::AreaLights`;
`AreaLightData` at `(1, 18)` (nearest 32, one-time warning past the cap); LTC tables at `(3, 6)`/`(3, 7)` from
`Core/vendor/ltc` (the `ltc_1`/`ltc_2` DDS of `selfshadow/ltc_code`, BSD-2-Clause); `DeferredLighting_Area.glsl`
+ `LTC.glslh` in an "Area Lights" pass that is only in the graph with area lights; viewport outline of each
light's shape. RenderFeatures has a rect and a disk light. Verified: Release, Debug and Lux-Runtime build;
golden `rf1` — all 17 Benchmark captures identical to `rf0` (known 2-px noise), RenderFeatures shows the
lights; Debug + sync validation on RenderFeatures and Benchmark: 0 validation ids (the 20 performance
warnings predate this). Older builds ignore the unknown `AreaLightComponent` key (the deserializer reads
known keys only) and drop it on save. Pending: driving the new inspector, create menu and add-component
row in the editor. Found on the way: the metal/rough target's `.w` holds `Specular`, so it is not free
for diffuse transmission (open question 2).

### Phase 2 — Sphere and tube lights, transparents, runtime parity

**Goal:** all four shapes; glass and other transparents are lit by area lights; Lux-Runtime and Dist
match the editor.

**Changes:** sphere and tube integrals in `LTC.glslh` (tube as a capsule: LTC line integral with
end caps); the transparent forward shaders call the same evaluation; two-sided flag; range falloff;
the cap warning; export check that the LUT data ships (it is compiled in, not an asset).

**Verification:** each shape in `RenderFeatures`; transparent glass reflects area lights; exported
game shows the same image (golden-capture the runtime if the harness supports it, otherwise a
screenshot compared by eye); Linux build compiles.

**Docs:** `Rendering.md` shapes and limits. **Exit:** all success criteria for area lights.

**Status 2026-10-22: code done on `feature/render-features`, editor checks pending.** Sphere and tube
shapes (`AreaLightShape::Sphere` / `Tube`, new `Length` key; a sphere is integrated as a disk facing the
shaded point, a tube as a facing rectangle lengthened by πr/4 per end — `LTC_FacingShape`); two-sided
only for flat shapes; `HazelPBR_Transparent.glsl` (the transparent pass's shader) adds
`LuxEvaluateAreaLights`, and the area buffer is now uploaded every frame (count 0 included) because
transparents read it; viewport outlines for all four shapes. Exported games: the shader pack is built
from the whole shader library (`ShaderPack::CreateFromLibrary`) and the LTC tables are compiled into
Core, so the runtime has everything; not run through an export. Linux: portable code, not built here.
Verified: Release, Debug and Lux-Runtime build; golden `rf2` — Benchmark identical to `rf1` except
`colliders`, which lands in its other known state (collider-line blending, 107–248 px, max 73, seen
before this phase in `p6A`); RenderFeatures shows all four lights and lit glass; Debug + sync
validation: 0 ids (the 20 old performance warnings).

### Phase 3 — Glass refraction

**Goal:** glass with Transmission > 0 refracts the opaque scene behind it, blurred by roughness and
tinted by its volume.

**Changes**
- `MaterialAsset.h/.cpp` — **NEW** transmission inputs with revision bumps on every setter (the
  `MaterialTable` revision rule from the retained plan), serializer defaults.
- `AssimpMeshImporter.cpp` — map `KHR_materials_transmission`, `_ior`, `_volume`.
- `LuxPBR_Transparent.glsl` (and `HazelPBR_Transparent.glsl` if still used) — refraction lookup.
- `SceneRenderer.cpp` — run `Pre-Convolution` when SSR is on or a refractive draw exists; bind the
  chain to the transparent pass; fold into the structure hash.
- Material inspector — the new inputs.

**Verification:** `RenderFeatures` glass row reads correctly (straight lines bend, rough glass blurs,
thick tinted glass darkens with depth); Benchmark golden identical with no refractive material;
SSR on/off both correct; Debug sync validation clean; perf: `Pre-Convolution` cost now appears only
on frames with refractive glass.

**Docs:** `Rendering.md` (refraction, Pre-Convolution condition), material section of Architecture.

**Status 2026-10-23: code done on `feature/render-features`, editor check pending.** Model (decided
while building, glTF-style): transmission takes its share from the diffuse lobe and adds the refracted,
absorbed, base-colour-tinted opaque scene weighted by (1 - Fresnel); alpha stays coverage, so refractive
glass keeps opacity 1 and transmission 0 is exactly today's blend. `MaterialSurfaceParameters` gains
Transmission / IOR / Thickness / AttenuationColor / AttenuationDistance (YAML keys written when not
default; asset packs reuse the YAML); the GPU row grows to 11 vec4 (`Transmission`, `AttenuationColor`);
`MaterialScene::HasRefractiveMaterials()` drives `m_FrameHasRefraction`, which runs Pre-Convolution
without SSR and binds it to the transparent pass at `(1, 11)` (black otherwise); blur capped at mip 5;
sky fallback off screen; importer maps the three glTF extensions (thickness texture not read); material
editor "Refraction" section for transparent materials. Verified: Release, Debug, Lux-Runtime build; golden
`rf3`: Benchmark identical to `rf2`; RenderFeatures panes sharp / frosted / rough, spheres bend, the
tinted block absorbs; SSR off (`SSR=0`) gives the same glass with Pre-Convolution alone (0.088 ms); Debug +
sync validation: 0 ids. Merge note: PR #34 also grows the material row and may add transparent passes
that must bind `u_RefractionSource`, `AreaLightData` and the LTC tables.

### Phase 4 — FFT bloom spike and cost gate

**Goal:** the FFT chain runs beside the old bloom behind a temporary option, and its cost is known.

**Changes:** **NEW** `PostProcessing/BloomFFT*.hlsl` (downsample, bright, FFT line pass, kernel,
normalize, multiply, resolve) — reimplemented, 512-point transforms in groupshared memory; graph
passes; a temporary `SceneRendererOptions` switch.

**Verification:** at 1280×720 and 1920×1080, `/profile` A-B-A (median of 3) of old vs FFT bloom on
Benchmark; the image shows glare around emissive points and the sun.

**Exit criteria:** the cost and the look are reported to the user **before** Phase 5. Proposed budget:
FFT bloom ≤ 0.6 ms GPU at 1080p on the RTX 4070 Ti; above that, the user decides whether to still
replace (their stated choice) or keep a fallback.

**Status 2026-10-23: spike done, gate passed — the user decides on Phase 5.** `PostProcessing/BloomFFT.glsl`
(GLSL compute, one shader with five modes: bright pass, kernel PSF, 512-point shared-memory line FFT,
spectral multiply with DC normalisation, resolve) behind the temporary `SceneRendererOptions::BloomFFT`
(golden option `BloomFFT=1`); the source is area-averaged into half the 512² grid along its longer side
(the other half is padding, so the circular convolution never wraps); the kernel spectrum is built once;
the result writes the bloom texture the composite already reads. Grids: 2x RGBA32F + 2x RG32F 512² ≈ 12 MB.
A-B-A on Benchmark (medians of 3, same build): 1280×720 mip bloom 0.059 / FFT 0.109 / mip 0.060 ms;
1920×1080 0.083 / 0.133 / 0.083 ms — +0.05 ms, nearly resolution-independent, far under the 0.6 ms budget.
Option off: Benchmark and RenderFeatures pixel-identical to `rf3`; FFT on, Debug + sync validation: 0 ids.
Look: a sharp core, a soft halo and six faint streaks, slightly chromatic; the kernel is hard-coded for
now (Phase 5 adds its settings).

### Phase 5 — FFT bloom replaces the mip bloom

**Goal:** one bloom, FFT, everywhere.

**Changes:** remove `Bloom.glsl` and its passes; map `BloomThreshold`/`BloomIntensity`/`BloomDirtIntensity`;
**NEW** kernel settings in `PostProcessSettings` (serialized with defaults), Scene post-process UI;
`EnableBloom` tiering still skips it; remove the temporary switch.

**Verification:** every golden capture with bloom changes — capture a new reference label and have
the user approve it by eye; noBloom variant identical to Phase 4's; Debug sync validation clean;
runtime and Dist builds (the HLSL compiles into the shader pack).

**Docs:** `Rendering.md` (bloom chain, kernel settings, cost).

**Status 2026-10-23: done, awaiting the user's look check.** `Bloom.glsl`, its three mip textures, the
mip views and materials, and the `BloomFFT` option are gone; `SceneRenderer::BloomCompute` is the FFT
chain and writes `m_BloomTexture` (RGBA16F, at the bloom resolution scale), which the composite and the
Bloom debug view read. Deviation from the plan: the kernel settings live in the renderer's
`BloomSettings` and the project's `ProjectSceneRendererSettings`, next to every other bloom setting,
not in the scene's `PostProcessSettings`: bloom is renderer-owned (`GetEffectivePostProcessSettings`
overlays it onto the scene values), so a scene-level copy would never be read. Settings: `StreakCount`
(0-8), `StreakLength`, `StreakIntensity`, `StreakRotation` (degrees), `HaloIntensity`,
`ChromaticSpread`; YAML under `PostFX/Bloom` with defaults for missing keys, runtime format 28 (older
packs keep the defaults), Renderer panel → Post FX rows. The kernel spectrum is rebuilt only when they
change. `Bloom Upsample Scale` no longer does anything: its row is removed and the field kept so
project files round-trip. Golden `rf5`: every bloom capture changes, `noBloom` identical to `rf3`; the
defaults reproduce the approved Phase 4 FFT look (RenderFeatures vs the Phase 4 FFT capture: max 2,
mean 0.01). Benchmark bloom 0.059 → 0.109 ms. Debug + sync validation: 0 ids. Release, Debug and
Lux-Runtime build; `BloomFFT.glsl` compiles offline with spirv-val clean.

### Phase 6 — Frosted-glass overlays and popups

**Goal:** viewport overlays and popups draw over a blurred, tinted backdrop; the preference turns it
off.

**Changes:** backdrop capture + dual-Kawase blur + glass composite (**NEW** shaders, editor-only);
`ImDrawCallback` hooks around overlay/popup backgrounds; **NEW** editor preference.

**Thread and lifetime:** ImGui building on the main thread; the callbacks run during ImGui rendering
on the render thread (`ImGuiRenderer.cpp`), so any state they read is captured by value at submit.
Backdrop textures resize with the swapchain and free through `Renderer::SubmitResourceFree`.

**Verification:** each overlay and popup in light and dark editor themes; preference off draws the old
look; resizing and DPI changes don't crash; editor frame time before/after recorded; ImGui scope and
ID audit (`Conventions.md § ImGui correctness`) plus driving every new state.

**Docs:** `Architecture-LuxEngine.md` editor section (the glass hook and preference).

**Status 2026-10-23: done, awaiting the user's look check.** `ImGuiGlass.hlsl` (full-screen
triangle; DOWN / UP dual-Kawase, COMPOSITE, PRESENT). `ImGuiLayer::End` puts an
`ImGuiGlassBackdropCallback` command first in each popup, menu and tooltip, and in the four
translucent viewport overlays that call `ImGuiLayer::RequestGlassBackdrop()` (Performance HUD,
viewport settings toolbar, acoustic legend, room acoustics); the orientation gizmo and selection
badge (background alpha 0) and the near-opaque toasts do not. Deviation from the plan: no copy out
of the back buffer (TRANSFER_SRC on swapchain images is optional in Vulkan and NRI cannot report
it); a frame with backdrops draws ImGui into an RGBA8 UI target that the blur reads directly and
presents it with one full-screen pass. Frames without backdrops, and the preference off, take the
old direct path unchanged. Preference "Glass Overlays" (Application Settings → Editor,
`Editor.GlassOverlays`, default on). There is no light editor theme, only the two dark ones.
Verified in the editor (Release, 3862x2110): the Performance HUD and the settings, File and View
popups blur what is behind them; preference off restores the old look (sharp scene through the HUD,
opaque popups); a window resize with glass on (1612x951 to 1926x1038) is clean. Cost (RenderDoc, two
backdrops): ~0.008 ms GPU per backdrop (six small passes) plus 0.080 ms for the full-screen present,
~0.10 ms of the ImGui renderer's 0.34 ms. Debug + sync validation with the popups open: no errors
(only the 20 pre-existing performance warnings `rf3dbg` has). Golden `rf6` Benchmark and
RenderFeatures identical to `rf5`. Not verified: a popup in an undocked platform window. The start
page and other popups that push an opaque `PopupBg` pay for a backdrop they cover.

### Phase 7 — Slimmer G-buffer

**Goal:** the same image from a cheaper G-buffer.

**Changes (each kept only if its golden is identical within the known noise):**
1. Drop the velocity target (no reader).
2. Octahedral normals in RG16 snorm; find a home for diffuse transmission (open question 2).
3. Base colour as RGBA8 sRGB.
All encode/decode stays in `LuxGBuffer.glslh`.

**Verification:** Benchmark A-B-A of `GBufferPass` + `DeferredLightingPass` + SSR/GTAO GPU time; golden
set identical (GTAO/SSR variants included); G-buffer debug views still correct.

**Docs:** `Rendering.md` G-buffer layout.

**Status 2026-10-23: done — step 1 kept, steps 2 and 3 measured and rejected by the gate.** First dev (PR #34,
foliage) was merged in (52d78287): the material row is 13 vec4, this branch's refraction vec4 renamed
`Refraction`, diffuse transmission keeping `Transmission`; golden `rf7m` identical to `rf5`.
1. **Velocity target dropped** (kept): no G-buffer attachment, no previous-transform fetch or clip
   varyings in `GBuffer_Static`, the dead `forwardSpec` and `GetGeometryVelocityOutput` gone; emission is
   attachment 4. Golden `rf7a` identical to `rf7m` (all 18). G-buffer pass 0.371 → 0.337 ms at 1080p
   (Benchmark, medians of 3).
2. **Octahedral normals** (rejected): in RG16F, 1,152 pixels differ by up to 52/255; in a new RG16 snorm
   format, 353 pixels by up to 35 — all grazing-angle rim highlights, where N·V near 0 divides the
   specular and the octahedral z (1 − |x| − |y|) is coarser than the xyz layout's. G-buffer pass and
   deferred lighting unchanged (0.337 / 0.311 ms). Diffuse transmission would have moved to the base
   colour alpha (opacity is decoded but never read).
3. **RGBA8 sRGB base colour** (rejected): 181 pixels up to 26/255 on Benchmark; it also clips base colours
   above 1.
Answer to open question 2: the base colour's alpha is free; metal/rough's w is specular.
Found on the way: GTAO scaled its power by `AOShadowTolerance` lerped with the normal's w, the
luminance the forward path once stored there. In the deferred G-buffer that w is 0, or a foliage
pixel's diffuse transmission since PR #34, so the setting did nothing except on foliage, by accident;
no current-frame luminance exists when GTAO runs (before lighting). Removed 2026-10-24 at the user's
choice: the setting, its UI row, the GTAO constant (a pad keeps the 88-byte layout) and the shader
term; runtime format 29, older packs skip the field. Golden `rf7f` identical to `rf7a` (all 18).
Debug sync validation clean; Release, Debug and Lux-Runtime build.

### Phase 8 — Meshlet path that pays off

**Goal:** the mesh-shader prepass is no slower than the classic one, then a meshlet G-buffer pass
follows; otherwise mesh shaders stay off with the measured reason recorded.

**Changes:** first a RenderDoc + Nsight diagnosis of the 25.9 ms prepass (scripted capture recipe in
memory `renderdoc-scripted-capture`; Nsight needs the user to unlock GPU counters); then per-meshlet
frustum/cone/occlusion culling in a task stage, meshlet sizing per NVIDIA guidance (≤64 vertices,
≤126 triangles), LOD selection consistent with `MeshLOD.glslh`; then **NEW** meshlet G-buffer shaders.

**Verification:** `MeshShaders=1` golden identical to classic; Benchmark A-B-A prepass and G-buffer
GPU time; Debug sync validation.

**Status 2026-10-24: diagnosed — the 25.9 ms was the pipeline-statistics query, not the meshlet path.**
RenderDoc on Benchmark (`MeshShaders=1`): 17 `vkCmdDrawMeshTasksEXT`, four of them 3–5 ms, one only
66 task groups. Bisected with the golden harness (medians of 3, 1280×720): no fragment work 23.2 ms;
mesh shader emitting nothing 21.9 ms; task stage only 4.1 ms — the cost was in launching task/mesh
work, before rasterization. With the PIPELINE_STATISTICS query (open around every command-buffer
segment, task/mesh counters included) disabled: **0.151 ms**, against 0.142 ms for the classic prepass
in the same build. Fix: statistics are counted only while the Profiler's Pipeline Statistics section
is open (`RenderCommandBuffer::RequestPipelineStatistics`, taken once per render frame). Golden `rf8a` identical to `rf7f`
(all 18); Debug sync validation clean, classic and mesh-shader. The meshlet prepass is now at parity,
not ahead (0.147 vs 0.117 ms in one golden run).

**Closed 2026-10-24: mesh shaders stay off; no meshlet G-buffer pass.** Three ways to make the meshlet
prepass win were built and measured (Benchmark, 1280×720, medians of 3, all pixel-identical to the
classic prepass, then reverted):
| Meshlet prepass variant | PreDepth GPU |
|---|---|
| as committed (frustum-culled meshlets, one task group per instance) | 0.151 ms |
| + whole-instance frustum test and world-space backface cones | 0.178 ms |
| task threads over (instance, meshlet) pairs end to end, 32 per group | 0.154 ms |
| mesh shader writing positions only, no fragment work (opaque bound) | 0.145 ms |
| classic prepass, same build | 0.118 ms |
The cones reject too little to pay for their math; filling the task groups for Benchmark's many small
instances changes nothing; even a depth-only opaque meshlet pipeline stays behind. The rest is the
meshlet data itself: built greedily in index order (`BuildSubmeshLODMeshlets`: loose spheres, wide
cones) and fetched from the full 56-byte vertex.
Spatially clustered meshlets were tried next, as Lucky Engine builds them (`MeshProcessor::BuildMeshlets`:
meshoptimizer 1.1 `meshopt_buildMeshlets`, cone weight 0.25, `meshopt_optimizeMeshlet`,
`meshopt_computeMeshletBounds`), with meshoptimizer vendored for the trial: still pixel-identical, but
0.163 ms with frustum culling and 0.184 ms with the backface cones, against 0.120 ms classic in the same
session. Better meshlets do not change it: on Benchmark's thousands of small instanced meshes the
classic instanced draw is cheaper on this GPU than task and mesh dispatch. Everything was reverted,
meshoptimizer included. A meshlet G-buffer pass is not built: the depth pass cannot win here. Revisit
only for scenes of a few dense meshes, or with GPU instance culling feeding the task stage.

### Phase 9 — QTangent vertices

**Goal:** 24–28-byte vertices, identical image, every existing mesh migrated automatically.

**Changes:** `Mesh.h` `Vertex` → position, packed quaternion frame with sign, UV; vertex layouts in
`SceneRenderer.cpp`; every vertex shader and the meshlet shaders; `AssimpMeshImporter.cpp`
orthogonalizes frames and records the reflection sign; `MeshSourceFile` version 2 — a version-1
cache is rebuilt from its source asset on load; runtime packs re-exported.

**Verification:** golden set identical (normal-mapped surfaces checked in the debug views); mesh
memory before/after in the Renderer Debugger Memory tab; an old project opens and re-cooks without
errors; runtime export works.

**Docs:** `Architecture-LuxEngine.md` asset/mesh format; `Rendering.md` vertex layout.

**Status 2026-10-24: done, as Lucky Engine does it.** Lucky (`M:\LuckyEngine\MAC-Source`, Apache-2.0)
keeps the full `Vertex` on the CPU and uploads a 28-byte `PackedVertex` (float3 position, int16×4
snorm QTangent, float2 UV) with `QTangent_ToTBN` in the shaders; LuxEngine now does the same, so
`MeshSourceFile`, the mesh caches and runtime packs are untouched (no version bump, no re-export).
`ShaderDataType::Short4N` (RGBA16_SNORM) is new; the mesh layout and the 11 shaders on it declare
position, QTangent and UV at locations 0–2; `GBuffer_Static`, `HazelPBR_Transparent` and
`HazelPBR_Static` decode the frame; the meshlet prepass reads a 7-float stride. Not touched: the
`_Anim` and `LuxPBR_*` shaders (no pipeline uses them). The encoder orthonormalizes the frame, so the
image changes where imported tangents were skewed — 14,180 Benchmark pixels (1.5%), most of them SSR
reflections on glossy normal-mapped floors, 12,639 on RenderFeatures; the user accepted the corrected
frame as the reference: golden **`rf9`** (reproduced from `rf9a`, all 18). Cost: Benchmark vertex
buffers halve (~11 → ~5.5 MB); no measurable GPU-time change at 1080p (G-buffer and shadow passes
within run-to-run noise; a 32-byte padded variant was tried and dropped). Debug sync validation clean
on Benchmark, RenderFeatures and the mesh-shader prepass; Release, Debug and Lux-Runtime build.

---

## Part 4 — Verification

- Every phase: Release golden set (`--scenes Benchmark,RenderFeatures --features`) against the
  previous phase's label, Debug + sync-validation run, and `/cr` before commit. Known noise: a
  60-px max-1 difference after a frame hitch (memory `golden-settle-residual`) — re-run before
  blaming a change.
- Perf phases: A-B-A in one session, medians of 3, Release, same window size and focus.
- **Baselines (Phase 0, 2026-10-22, `dev` @ `9a231480`, Release, 1280×720, medians of 3, golden
  label `rf0`):**

  | Scene | Frame | GPU | Main thread | PreDepth | GBuffer | DeferredLighting | Bloom | TransparentForward | PreConvolution | SSR |
  |---|---|---|---|---|---|---|---|---|---|---|
  | Benchmark | 3.11 ms | 1.85 ms | 2.17 ms | 0.163 | 0.282 | 0.140 | 0.059 | 0.020 | 0.089 | 0.145 |
  | RenderFeatures | 2.29 ms | 1.35 ms | 0.68 ms | 0.014 | 0.047 | 0.088 | 0.061 | 0.041 | 0.088 | 0.105 |

  Absolute numbers move with GPU clock state between sessions (this set ran ~30% faster than the
  2026-10-20 captures with no code change in those passes), so every perf claim uses an A-B-A in one
  session; these rows are orientation only. `rf0` was captured with the working-copy
  `LuxSample.luxproj`, whose `ShadowFilterMode` was 0 (uncommitted); a different value changes
  every shadowed pixel, so recapture the baseline if it changes. The harness applies `--features`
  variants to the first scene only, so RenderFeatures has its default capture.

## Part 5 — Risks

- **FFT bloom cost** at high resolution may exceed the budget; Phase 4 measures before anything is
  removed.
- **Glass UI backdrop** needs pixels already drawn by ImGui in the same pass; if the callback cannot
  copy mid-pass, it captures the viewport image instead (overlays) and uses a blur of the panel
  colour for popups. Found in the first day of Phase 6.
- **Refraction artifacts** where the refracted point is off-screen or in front of the glass; the
  environment fallback hides most of it; anything else is accepted screen-space behaviour.
- **QTangent** sign/reflection bugs show as inverted normal maps on mirrored UVs; the golden set
  covers Sponza's mirrored UVs.
- **Meshlet path** may never beat the classic path on this content (NVIDIA warns gains are not
  guaranteed); the exit allows "stays off".

## Part 6 — Open questions

1. Do older builds skip an unknown `AreaLightComponent` cleanly on load? (check `SceneSerializer` in
   Phase 1)
2. Where does diffuse transmission go once normals are octahedral — the free channel of the
   metal/rough target (unverified that it is free), or two bits of the normal? (Phase 7 spike)
3. Does the shader compiler accept HLSL `groupshared` compute for the FFT? GTAO.hlsl compiles as
   compute; groupshared use is unverified. (first hour of Phase 4)
4. FFT bloom budget — 0.6 ms at 1080p is a proposal; the user confirms in Phase 4.

## Part 7 — Research notes

- **LTC** — Heitz, Dupuy, Hill, Neubelt 2016; fitted tables in `selfshadow/ltc_code`, BSD-2-Clause
  with a citation request; three.js ships the same data (~80 KB).
  [license](https://raw.githubusercontent.com/selfshadow/ltc_code/master/LICENSE),
  [three.js](https://threejs.org/docs/pages/RectAreaLightTexturesLib.html),
  [paper](https://www.advances.realtimerendering.com/s2016/s2016_ltc_rnd.pdf)
- **Refraction** — HDRP: refract, project to screen, sample a roughness-indexed colour pyramid,
  attenuate by optical depth; fall back to probes off-screen. Lux reuses Pre-Convolution as the
  pyramid. [HDRP docs](https://docs.unity3d.com/Packages/hdrp/manual/Refraction-in-HDRP.html)
- **Convolution bloom** — Unreal uses FFT with a padded buffer to stop wrap-around and positions it as
  a high-end/cinematic option; standard bloom is far cheaper.
  [UE settings](https://docs.unrealengine.com/4.26/API/Runtime/Engine/Engine/FConvolutionBloomSettings/index.html),
  [UE bloom](https://dev.epicgames.com/documentation/ja-jp/unreal-engine/bloom-in-unreal-engine)
- **Octahedral normals** — Cigolle et al. 2014 survey; octahedral is the standard compact choice; 16 bits
  per channel exceeds the paper's 8-bit case. [JCGT](https://jcgt.org/published/0003/02/01/paper.pdf),
  [Aras](https://www.aras-p.info/texts/CompactNormalStorage.html)
- **QTangent** — Frey (Crytek) 2011; orthogonalize first, carry the reflection sign through the w > 0
  canonical form. [talk](https://ia600902.us.archive.org/25/items/crytek_presentations/izfrey_siggraph2011.pdf),
  [zeux 2026](https://zeux.io/2026/04/30/quantizing-tangent-frames/)
- **Meshlets** — NVIDIA: 64 vertices / 126 triangles, task-stage culling pays only when it removes
  geometry the existing culling doesn't; no guaranteed win over the classic pipeline.
  [NVIDIA](https://developer.nvidia.com/blog/advanced-api-performance-mesh-shaders/),
  [Vulkan sample](https://docs.vulkan.org/samples/latest/samples/extensions/mesh_shader_culling/README.html)
- **Rejected:** any temporal accumulation (refraction denoise, bloom flicker fixes via history) — the
  no-temporal rule; Lucky's path tracer and `PTTaa` — rule and scope.
