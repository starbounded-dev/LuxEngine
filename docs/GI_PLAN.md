# LuxEngine Global Illumination Plan

Planning document (point-in-time, like the other `docs/*_PLAN.md`). What is actually built is described by
`.claude/docs/Architecture-LuxEngine.md`. Written 2026-11-23 against `dev` @ `cd39b6b7`.

**Goal card**
- **Goal:** add a ray-traced Path Tracer (ground-truth reference), baked Lightmaps, Realtime GI and Ray-Traced GI, plus Diff views (Lightmap vs reference, Realtime GI vs reference).
- **Success:** in the editor, one scene can be viewed as path-traced reference, lightmapped, realtime-GI and RTGI; two Diff views show per-pixel error against the reference; lightmaps work in the runtime and Dist builds on non-RT GPUs.
- **Non-goals:** replacing or changing the rasterized deferred renderer (it stays the default and the only path every GPU runs); SDF/non-RT fallback for realtime GI; reflections/shadows via RT; DX12 backend.
- **Constraints:** self-contained (no new installs, small permissive vendored libs only); existing `.luxscene`/`.luxproj` must still load; start RT/GI from scratch (old DDGI and ray-path-tracing work was deleted and never worked as wanted).
- **User rulings (2026-11-23):** the old "no temporal rendering" rule is **removed**: progressive accumulation, hysteresis caches and denoisers are allowed. Realtime GI and RTGI are RT-only; lightmaps cover non-RT hardware. Build order: spike, path tracer, Diff views, lightmaps, realtime GI, RTGI.

## Decisions this plan is built on
| Decision | Choice | Consequence |
|---|---|---|
| Temporal techniques | Allowed (rule retired) | PT accumulates; GI uses hysteresis probe/hash caches and a denoiser |
| Non-RT GPUs | Lightmaps only | Lightmaps must ship as plain textures; runtime/Dist need no RT |
| Raster vs RT | Raster default; RT features optional | Feature-gated, off by default; Dist/runtime work without RT |
| Order | Spike, PT, Diff, Lightmap, RTGI-probes, RTGI-screen | Every later phase is proven against the PT reference |
| Ray API | Ray queries in compute (`VK_KHR_ray_query`) first, not RT pipelines | Simpler, matches how NRI's acceleration-structure API is used; avoids the maintenance1 pairing NRI needs for RT pipelines (`VulkanDevice.cpp:452`) |
| Denoiser | Decided in Phase 5; do not assume NRD | NRD is no longer in `Core/vendor/` and was never wired |

## Part 0 — Where we are (ledger)
| Capability | State | Evidence |
|---|---|---|
| RT device extensions (AS, ray query, RT pipeline) | Enabled when supported; never used at runtime | `Core/Source/Lux/Core/Window.cpp:61`, `Platform/Vulkan/VulkanDevice.cpp:429-600` |
| NRI ray-tracing API | Header exists in the NRI fork (`Core/vendor/NRI/Include/Extensions/NRIRayTracing.h`, seen in the main checkout; submodule empty in this worktree); **not exposed through the Lux RHI** | grep of `Renderer/RHI/*.h` finds only a shader-stage flag (`RHITypes.h:49`) |
| Scene BVH / AS builder | **Missing** | no hits in `Core/Source` (soundwave has its own CPU `GeometryView`, not reusable for GPU rays) |
| Path tracer | **Missing**; `Editor/Resources/Shaders/PathTracing/` is an empty directory | listed 2026-11-23 |
| Lightmap UVs | Partial: `VertexAttributes::UV1` exists and the Assimp importer fills it from `mTextureCoords[1]` when present; no UV1 generation, no unwrapper (xatlas) | `Asset/AssimpMeshImporter.cpp:641`, `Renderer/Mesh.h:52` |
| Indirect diffuse today | IBL irradiance plus screen-space `u_AmbientOcclusion` only | `DeferredLighting.glsl:254`, `LuxEvaluateImageBasedLighting` in `Include/GLSL/PBR.glslh:303-369`, also `HazelPBR_Static.glsl:165`, `HazelPBR_Transparent.glsl:186` |
| Light probes / GI | **Missing** | no probe/lightmap hits in `Core/Source` |
| Denoiser | **Missing** | NRD absent from `Core/vendor/` |
| Runtime verification of any of the above | None; everything here is read from source, not run | |

## Part 1 — Design
- **Raster stays primary.** The deferred raster pipeline is unchanged and always the default. Path tracing, ray-traced GI and probe GI are opt-in options: off by default, zero cost when off (passes skipped, no AS built), hidden/disabled in the UI on GPUs without ray query, and absent from the Dist player unless a project turns them on. Lightmaps are the only GI that needs no RT. Each phase's verification includes: with the option off, the frame is bit-identical to before and GPU time is unchanged.
- **One integration point.** Indirect diffuse enters through `LuxEvaluateImageBasedLighting` plus `u_AmbientOcclusion`. Lightmaps, probe GI and RTGI each become an alternative *indirect-diffuse source* chosen per scene (and lightmap per object), feeding the same term in deferred and the forward/transparent shaders. Specular IBL stays.
- **Systems/threads.** New `Renderer/RayTracing/` (AS manager, ray-query compute passes) owned by `SceneRenderer`; everything on the render thread via `Renderer::Submit`, correct under both threading policies. AS data enters via `FrameRenderPacket`/`RenderScene` proxies, not ECS reads. The lightmap baker is an editor-only job on the JobSystem for CPU prep, GPU dispatch via `Renderer::Submit`.
- **Ownership.** AS and bake targets are GPU resources freed through `SubmitResourceFree`; scene switch/Play must drain in-flight frames first (see the scene-switch GPU drain note; no dedicated helper exists today, name the mechanism in Phase 0).
- **Serialization.** `LightmapSettings` per scene (`PostProcessSettings`-style, serialized only when non-default) and a per-mesh-renderer lightmap index/scale-offset. Old scenes load with none = today's lighting.
- **Assets.** New `Lightmap` asset type (playbook "new asset type": enum, class, serializer plus runtime serializer for asset packs, importer, extension). Baked data is stored in HDR-compressed textures (format decision in Phase 3).
- **Packaging.** Lightmaps go into `AssetPack.lap`; Dist has no shader compiler, so new shaders must be in `ShaderPack.lsp`.

## Part 2 — Phases (each builds, runs, and leaves the engine shippable)

### Phase 0 — Ray-query spike (retire the biggest risk)
- **Changes:** `RHI/` — **NEW** minimal acceleration-structure wrapper over NRI (BLAS from `Mesh` vertex/index buffers, one TLAS); **NEW** `RayQuerySpike.glsl` compute pass; debug view in the existing G-buffer debug UI showing hit distance/normal.
- **Questions it must answer:** does NRI RT work through `RHIDevice` and the resource state tracker; how do BLAS/TLAS barriers map in `NRIBarrierEmitter`; descriptor set/binding collisions (global (set,binding) namespace, grep first); GPU drain on scene switch; behaviour on a GPU without ray query (pass skipped, UI says so).
- **Verify:** build Debug; in the editor open the sample scene, enable the debug view, hit-distance image matches the geometry; no validation errors in the log; run once with `MultiThreaded` and once `SingleThreaded`. **Editor launch needs your explicit OK that turn.**
- **Exit:** spike works or the plan is revised before any other phase starts. **Rollback:** feature-gated, delete the pass.

### Phase 1 — Scene AS + Path Tracer (editor reference)
- **Changes:** AS manager builds/updates BLAS per mesh and TLAS from the render packet (static first, then dynamic transforms; skinned meshes excluded in v1, stated limit); **NEW** `PathTrace.glsl` (unidirectional, next-event estimation for directional/point/spot/area lights, environment importance sampling, Lux PBR BRDF consistent with `PBR.glslh`, bindless material/texture access via the existing set-4 bindless table); progressive accumulation buffer (resets on camera/scene change); viewport mode "Path Traced" with sample counter; editor-only (not in Dist).
- **Verify:** Cornell-box-style test scene converges to a stable image; a white furnace test (diffuse white sphere in uniform environment) returns radiance 1 within tolerance; compare direct lighting only against the existing deferred result.
- **Docs:** Rendering.md (new pass, bindings), Architecture (new system).

### Phase 2 — Diff views
- **Changes:** **NEW** diff pass: reads the accumulated path-traced image and the rasterised result; outputs absolute/relative error heat map plus MAE/PSNR readout in the viewport; modes "Diff vs Lightmap" and "Diff vs Realtime GI" are the same pass with a different candidate (enabled once those exist). Use linear HDR before tonemapping.
- **Verify:** diff of the PT against itself is zero; a deliberately biased candidate shows the expected error.

### Phase 3 — Lightmaps
- **Changes:** UV1 generation with vendored xatlas (MIT; state size when added to `Dependencies.lua`) for meshes without UV1; per-renderer lightmap index and scale/offset; baker reusing the Phase 1 kernel (hemisphere sampling per texel, dilation/seam padding, optional spatial denoise); `Lightmap` asset plus pack export; shaders sample the lightmap as the indirect diffuse source in deferred and forward; "Bake" action and settings in the editor; old scenes unchanged.
- **Verify:** bake the sample scene, toggle lightmaps on/off in the viewport, run Diff vs Lightmap (error should be low on static surfaces); export a runtime build and confirm the lightmaps load without RT.
- **Open:** compression format (BC6H vs RGBM), directional lightmaps for normal maps (v2).

### Phase 4 — Realtime GI (probe-based, RT-only)
- **Design:** world-space probe volume with ray-query tracing, hysteresis blending, visibility/distance moments for leak reduction, sampled in the lighting passes as indirect diffuse. Start from scratch (do not mine the deleted DDGI branch).
- **Verify:** Diff vs Realtime GI against the PT; moving a light converges within a stated time; log shows no binding collisions; costs zero when off.

### Phase 5 — Ray-Traced GI (screen-space ray tracing for GI)
- **Design:** per-pixel 1 spp diffuse GI rays from the G-buffer with the probe field as the secondary-bounce source, spatial filtering plus a denoiser; temporal reuse now allowed. Denoiser choice (hand-written or vendored) is made here with a size/licence/startup cost statement. Fast-SDF-AO's spatial reservoir reuse and à-trous filtering are candidate techniques.
- **Verify:** Diff vs the PT reference at fixed spp; profile with `/profile` (Tracy GPU zones), set a ms budget before starting.

## Part 3 — Risks
- NRI RT may not map cleanly onto Lux's resource state tracker (Phase 0 answers this).
- Dynamic and skinned meshes need AS refit/rebuild; v1 limits stated above.
- Probe leaking and update lag in Phase 4; lighting-change lag was accepted by the user.
- The `luxproj` settings trap: the editor saves renderer settings into `LuxSample.luxproj`; diff that file first on any visual regression.
- Per memory: validation-layer and device-lost behaviour on the dev GPU have been fragile; test every phase under validation.

## Part 4 — Open questions
- Lightmap compression format and directional lightmaps.
- Denoiser: write vs vendor.
- Whether a "Path Traced" mode also ships in Dist later (currently editor-only).
- Minimum RT hardware to advertise.

## Part 5 — Research notes
Only one external source was reviewed: **[Fast-SDF-AO](https://github.com/wrong-nebula/Fast-SDF-AO)** (MIT, WebGPU/JS).
- **Concept:** hybrid contact BVH plus FP16 SDF tracing for visibility, a world-space radiance cache and spatial ReSTIR-GI reuse with à-trous/SIGMA-style filters, no hardware RT.
- **Use for Lux:** ideas only (spatial reservoir reuse, hit-distance-guided à-trous filter for Phase 5). Its SDF path would be the basis of a non-RT fallback, which you declined for now.
- **Caveat:** its numbers (0.054 MAE vs the author's 512-ray reference, Apple M1) are self-benchmarked, not independent. The README says several stages (SHaRC cache, AO accumulation, reflection reprojection) rely on temporal history; that is now permitted here.
- A broader literature/prior-art search (DDGI, RTXGI, lightmapper designs) was **not** done in this pass; do it at the start of Phases 3 and 4.

## Next
Phase 0 is the first thing to implement (`/dev` then `/cr`, then `/send-pr`). Not started.

## Status (2026-11-23, after the limits pass)
All six phases are built and verified by `python tests/rendering/run_ray_query_spike.py` and `python tests/rendering/run_path_trace_check.py`
(real GPU runs through the golden-capture harness). With every option off the raster frame is bit-identical to `dev` (compared on `LowSun`
before the sky fix below; the Preetham lower hemisphere changed on purpose, so a sky-visible golden needs re-accepting).

| Phase | State | Evidence |
|---|---|---|
| GI0 ray-query spike + RHI | Done | AS as a descriptor input, `RayTracingScene`, standalone spike (107 analytic hits) |
| GI1 path tracer | Done | Cornell colour bleeding, white furnace 0.87 (the engine BRDF is not energy conserving), noise falls with samples |
| GI2 diff views | Done | heat map + MAE/PSNR; Diff Lightmap / Realtime GI / RTGI buttons |
| GI3 lightmaps | Done, limits below | bake over the PT kernel |
| GI4 realtime GI | Done | SH probe volume + DDGI-style octahedral distance visibility |
| GI5 ray-traced GI | Done | per-pixel + a-trous denoise |
| GI-S card + sample scenes | Built; the panel was not clicked through | `GI_CornellBox`, `GI_Furnace`, `GI_AreaLights`, `GI_Leak`, `GI_Textured`, `GI_HDRSky` |

**Limits pass (issues #221 to #231).** The comparison scenes for lightmaps, probes and RTGI use `GI_CornellDark` (no sky ambient): the raster renderer's image-based ambient light would otherwise be added on top of the baked/traced indirect and hide the gain.

| Limit | Result |
|---|---|
| GIL1 material textures | Done: the engine's material rows and bindless textures (albedo, metalness, roughness, emissive maps), UV and triplanar world/object mapping, ray-cone mip selection; scene `GI_Textured` |
| GIL2 constant sky | Done: the scene's radiance map (dynamic sky or an `.hdr` SkyLight) with intensity and rotation; scene `GI_HDRSky` |
| GIL3 area light shapes | Done: rect, disk, sphere, tube; scene `GI_AreaLights` |
| GIL4 skinned meshes | Done: CPU linear-blend skinning per instance (a changed pose rebuilds the BLASes; refit is a follow-up in #231) |
| GIL5 probe leaks | Implemented (octahedral mean/mean-squared distance per probe, Chebyshev weights, backface probe rejection, normal bias). The `GI_Leak` scene did not isolate a measurable difference, so the gain is not demonstrated |
| GIL6 exports | Export copies `Lightmaps/` next to the runtime project file and the runtime reads it from there. Not run end to end (needs a Dist editor and player) |
| GIL7 UV overlap / unwrapper | The baker now detects and reports overlapping UVs. An automatic xatlas unwrap is NOT done: it splits vertices, so it needs MeshSource support for a replacement UV1 stream |
| GIL8 validation / Dist | Debug build with the Khronos validation layer and synchronization validation: 0 messages across the path tracer, probes + diff, RTGI and the lightmap bake on `GI_CornellBox`. A Dist build was not made (the ray-traced code compiles in every config; the shaders need the compiler, so Dist skips them) |
| GIL9 HDR metric | Done: `PathTracerDiffPeak` scales radiance before the clamp so bright errors count |

Also found and fixed on the way: the Preetham sky's lower hemisphere carried the horizon's azimuth variation all the way to the nadir,
which showed as a bright pinch at the very bottom of the sky; it now fades to an azimuth-independent ground colour.

### Sampling, reuse and acceleration-structure pass (issues #232-#240, 2026-11-23)

All opt-in; the unbiased path tracer stays the reference. Each row was verified on the RTX 4070 Ti by its own check in `tests/rendering/run_rt_sampling_check.py` (scenes from `tests/rendering/make_rt_scenes.py`).

| Issue | Feature | Result |
|---|---|---|
| #232 | Power-weighted light choice (CDF in `PackLights`, D.w of each light) | converged image unchanged (RMSE 0.009); 8-spp error vs reference on 24 lights 0.069 -> 0.037 |
| #233 | MIS between NEE and BSDF sampling (analytic rect/disk/sphere/tube hit tests, power heuristic) | converged image equals NEE-only (RMSE 0.003, so no double count); 8-spp error 0.037 -> 0.033 on a glossy floor under large + small lights |
| #234 | Adaptive sampling (per-pixel count and luminance moments; Russian roulette already existed) | at the same average sample count (about 42 of 256 on `GI_CornellBox`) error vs reference 0.0105 -> 0.0050; biased by the stopping rule, off by default |
| #235 | Ray sorting (software wavefront, bins by octahedral direction) | identical image (RMSE 0) but SLOWER: Sponza 2.89 -> 3.75 ms GPU (1.30x), Cornell 1.48 -> 1.79 ms. Off by default; hardware SER needs an RT-pipeline raygen and the NRI fork has no reorder extension |
| #236 | ReSTIR DI (fused spatiotemporal resampling, one shadow ray) | 24 lights, 4 spp: error vs reference 0.053 -> 0.019; converged within 0.005; biased (1/M), centre-of-pixel rays |
| #237 | ReSTIR GI reuse for RTGI (Jacobian, visibility ray) | `GI_BounceSpot`, no temporal accumulation: error vs settled 0.0055 -> 0.0034; settled image within 0.003 of plain RTGI |
| #238 | In-house spatiotemporal RTGI denoiser (reprojection, moments, variance-guided a-trous). NRD is not in the repo and DLSS-RR needs engine motion vectors and a proprietary SDK: both stay optional follow-ups | 6 frames after a camera sway error vs settled 0.0030 -> 0.0023; a static camera settles within 0.0016 |
| #239 | Skinned BLAS refit (stable entity key + pose hash, ALLOW_UPDATE) | renders identically to rebuilding (RMSE 0); on the tiny rig mesh no measurable speedup (the gain scales with triangles) |
| #240 | BLAS compaction (deferred size readback) and TLAS refit | Sponza 26 BLASes 17.8 -> 7.4 MiB, identical image; moving instances refit the TLAS, identical image |

**Hardware ray tracing.** The ray-traced features use `VK_KHR_ray_query` over `VK_KHR_acceleration_structure`, which run on the RT cores of an
RTX card (and the ray accelerators of RDNA2+) automatically whenever `RHIDevice::SupportsRayQuery()` is true; without it the options are
unavailable and raster is unchanged. Making that visible (device, GPU timings), build tuning (compaction, refit, persistent scratch), the
ray-tracing-pipeline path (shader execution reordering, opacity micromaps) and optional denoisers (NRD, then DLSS Ray Reconstruction) are
tracked in #231.

### Materials, media and reflections pass (issues #241-#245, 2026-11-23)

| Issue | Feature | Result |
|---|---|---|
| #241 | Clearcoat, sheen, anisotropy in the path-traced BRDF (PBR.glslh formulas, tangent from uv deltas) | on `GI_Lobes` the sheen sphere's RMSE vs the raster frame 0.090 -> 0.037, anisotropic 0.094 -> 0.065; coated furnace sphere 0.872; plain scenes identical |
| #242 | Glass: smooth dielectric, Fresnel, Snell, TIR, Beer-Lambert absorption | furnace sphere 1.000; absorbing slab 0.471 vs 0.463 (attenuation colour is sRGB-authored, the row holds it linear); shadow rays stay opaque, so delta lights cast black shadows through glass |
| #243 | Fog: homogeneous, free-flight, HG phase, NEE in the medium, optional sphere (path tracer only; the raster renderer has no fog) | absorbing sphere 0.374 vs exp(-sigma d) 0.368; albedo-1 furnace 1.000; a lamp in fog glows; directional lights are not attenuated |
| #244 | Subsurface random walk (mean free path 5 cm, albedo = subsurface colour) | 0.8 m albedo-1 sphere in the furnace 0.993; thin slab lit from behind glows (0.218 vs 0) |
| #245 | Ray-traced reflections on the raster depth/G-buffer (replace SSR while on) | `GI_Mirror` floor RMSE vs path tracer 0.0537 -> 0.0278; escaped rays add nothing, so a bright sky double counts where geometry is hit |

| #246 | Ray-traced directional shadows: a visibility mask traced from the raster depth/G-buffer, read by deferred lighting in place of the cascades (soft by an angular radius) | `GI_Shadows` with the sun's shadow distance at 3 m (cascades end before the objects): RMSE vs the path tracer 0.205 -> 0.065; option off identical. Directional light only: point/spot/area shadows still use the shadow maps/atlas, and the translucency path keeps the cascades |

### Workflow pass (issues #227, #230, #247-#249, 2026-11-23)

| Issue | Feature | Result |
|---|---|---|
| #247 | Per-pixel debug views (bounces, rays, noise, samples) | more allowed bounces/rays move the views up the heat ramp, the noise view cools with samples |
| #248 | Lightmap denoiser (CPU a-trous on the read-back atlas, normal/position/luminance weights) | a 16-sample bake renders closer to a 512-sample bake with it (RMSE 0.0138 -> 0.0110) |
| #249 | Probe auto placement (grid from the bounds and a spacing; probes inside geometry refresh rarely) | `GI_CornellDark`: 729 probes vs 2048, Diff MAE 0.0156 -> 0.0162 |
| #227 | xatlas vendored (MIT) and an unwrap report; the unwrapped UVs are NOT written back into the mesh (vertex splitting across GPU buffers, meshlets, LODs): open | the default cube: 128 -> 128 vertices, 6 charts, 98% of the atlas |
| #230 | Texture Library panel: Poly Haven (871 textures) and ambientCG browse/search/download over WinHTTP (Windows only, header-only), maps land in Assets/Textures/Library/<name>/ | both download real JPEG maps (headless check); the panel itself was not clicked through; no thumbnails or drag-and-drop yet |

**Limits of this pass:** ReSTIR DI/GI and adaptive sampling are biased and must stay off for reference renders; ReSTIR DI traces from pixel centres and cannot combine with ray sorting; the ReSTIR GI sample treats radiance as direction independent; the denoiser has no motion vectors for moving objects (camera motion only) and was checked with camera motion, not object motion; skinned refit speed is unmeasured on a heavy mesh.

**Other limits that remain:** the path tracer has no transmission/clearcoat/sheen lobes; rect/disk lights are one-sided only; lightmaps are
indirect-only; RTGI traces its own primary surface, so geometry outside the ray-traced scene gets no GI. Not built or run: Linux, Dist.

### Ray-tracing follow-up (2026-11-25)

| Issue | Change | Windows Release verification |
|---|---|---|
| #55 | Scene-authored analytic height and distance fog | At unit exposure, near and far fog amounts measured 0.392 and 0.715. The height fixture measured 0.972 in the low region and 0.455 in the high region. A 6 m start left the near blocker clear. Disabling skipped the fog pass. The exported Windows player measured 0.388 and 0.710 against analytic values of 0.388 and 0.709. |
| #230 | CC0 texture search, previews, material-pack import, and asset drops | Poly Haven's 2K wall and ambientCG's 1K wood delivered base colour, normal, roughness, occlusion and height, plus metalness for the wood. The viewport and inspector assignment paths changed 8.3% of Cornell pixels. Windows and Ubuntu each reproduced their own render exactly when reopening the saved materials without library requests. |
| #231 | Hardware tier and GPU timings, persistent BLAS scratch | The RTX 4070 Ti exposed NRI ray-tracing tier 2. The animated fixture reused one 64 KiB scratch allocation in both build modes. At 1280×720, skinned BLAS refit took 0.046 ms and rebuild took 0.182 ms with identical images. The corresponding trace timings were 0.346 and 0.377 ms per sample, or 2.66 and 2.45 billion primary rays per second. |
| #250 | Shared fog extinction for raster direct light, ReSTIR DI, RT reflections, probes, RTGI and lightmap baking | In `GI_FogSun`, sunlit radiance fell to 0.522 in raster and 0.521 in both path tracing modes (analytic centre chord 0.489). Moving the sphere out of the light path reproduced the clear image in each mode. In direct shadow, fog reduced the probe-lit region from 0.2061 to 0.0110 and the RTGI-lit region from 0.2395 to 0.0109. Zero density reproduced both GI frames. |
| #251 | Replace the sky reflection on geometry hits, vary ray count by roughness, and denoise rough reflections | In `GI_MirrorSky`, replacement reduced mirror-floor RMSE against the path tracer from 0.0946 to 0.0588. One ray and both filter settings reproduced the mirror floor. In `GI_RoughReflection`, filtering reduced floor RMSE from 0.1007 to 0.0441 against a 512-sample path trace. With a 16-ray maximum, the adaptive budget reduced trace GPU time from 0.329 to 0.141 ms. The filter averaged 0.141 ms over 60 validation frames. |
| #252 | Transmit and tint shadow rays through glass | The tinted plate produced 59,012 red pixels, and disabling glass produced none. Mean red in the affected region was 0.369 through glass and 0.244 under the opaque plate. |
| #227, #253 | Persistent per-source xatlas UV generation for static and skinned meshes | The wall-and-floor mean RMSE against the path tracer fell from 0.0995 to 0.0684, and the unbaked frame was identical. The report-only check retained the four source triangles. After restart, the skinned fixture rebuilt the same 12 charts and its posed image matched exactly. The exported player skinned 350 vertices with two bones. |
| #254 | Opt-in Vulkan shader execution reordering after the first ray, with a material hint | On the RTX 4070 Ti, the SER and compute images matched at RMSE 0.000019 at 64 samples. Explicitly disabling SER reproduced the default image. The median of three 120-frame GPU runs in `GI_Lobes`, 1280×720, was 1.543 ms with compute and 1.912 ms with SER, about 24% slower. The Vulkan validation capture was clean. SER stays off by default. |
| #255 | Opt-in redistribution of realtime GI probe planes based on measured radiance variation | On the localized emitter in `GI_OverlapUV`, adaptive placement reduced path-tracer MAE from 0.0433 to 0.0358 with 168 probes in both runs. The option left the frame identical with realtime GI off. The Vulkan synchronization-validation capture was clean. |
| #256 | Constant-time light selection, shared ReSTIR candidates, and direct emissive mesh sampling | Splitting the same power across 24 and 288 analytic lights preserved the converged image. At 288 lights, four-sample RMSE fell from 0.1263 to 0.0367 with ReSTIR. The timed ReSTIR pass took 0.229 and 0.260 ms at the two light counts. In the small emissive-mesh fixture, converged floor brightness was 0.0440 with BSDF hits and 0.0460 with direct sampling. Four-sample RMSE was 0.0727 with BSDF hits, 0.0633 with MIS and 0.0389 with ReSTIR. A second fixture confirmed that the distance-limited shadow ray reaches the emitter sample after applying receiver bias. At 1,024 samples its floor brightness was 0.2385 against 0.2362 for BSDF hits. |
| #257 | Ray-traced ambient occlusion and directional contact shadows | The short rays reduced near-shadow RMSE against the path tracer from 0.3228 to 0.0564. The raised blocker remained outside contact range. Ambient rays darkened 3,508 near pixels and zero far pixels at the same threshold. Tracing and filtering took 0.133 and 0.037 ms at 1280×720. |
| #258 | Half-resolution RTGI with temporal denoising and a full-resolution depth/normal resolve | At 961×541, Cornell RMSE against the path tracer was 0.0719 for raster and 0.0321 for both RTGI modes. The two RTGI images differed by 0.0023 RMSE. After a camera sway, temporal reuse lowered error from 0.0097 to 0.0028. Switching back to native converged within 0.0016 RMSE. Trace, filtering and resolve took 0.221 ms together, down from 0.451 ms native on the RTX 4070 Ti. |
| #259 | Texture mip residency checkpoint | A repeatable camera route used at most 5,592,404 fine-mip bytes with a 6 MiB budget, promoted four textures and evicted three. Returning to the starting surface restored the original streamed image. The 6 MiB render reduced RMSE against the resident image from 0.1981 to 0.0574 versus the 1 MiB budget. |
| #260 | Ray-traced soft area-light shadows | On the rectangle-light fixture, penumbra RMSE against the path tracer fell from 0.0787 to 0.0665 over 15,157 pixels. Unoccluded lighting matched exactly. The RT area pass averaged 0.221 ms on the RTX 4070 Ti at 1280×720. |
| #261 | Tinted directional and area-light shadows, plus optional refractive caustics | Directional and area-light glass transmitted red onto 10,740 and 31,961 pixels. At 65,536 photons, the refractive sphere focused light onto 3,422 pixels with a mean display-intensity gain of 0.267. The IOR 1 control was identical with caustics enabled. Trace and resolve took 0.029 ms and 0.037 ms on the RTX 4070 Ti. |

The installed Vulkan SDK compiles hit-object instructions, but its SPIRV-Cross reflector rejects
both the EXT and NV hit-object types. The SER pipeline uses the EXT hint instruction after the
closest-hit or miss shader returns. The optimized SPIR-V used by the editor contains
`OpReorderThreadWithHintEXT`.

The final Windows Release Editor build at `33a1bdcd` passed all 27 checks in
`tests/rendering/run_rt_sampling_check.py`. The checks resumed on the same executable after
the execution connection dropped. The Windows Release `Lux-Runtime` build also linked,
and the regenerated engine shader pack matched the committed shader sources.
