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

## Status (2026-11-23)
- Done and verified: GI0 hardware/shader path (`python tests/rendering/run_ray_query_spike.py`: BLAS+TLAS, compute ray query, 107 analytic cases pass on an RTX 4070 Ti, no editor).
- Built, not run in the editor: `Path Tracing` card in the Scene Renderer panel (session-only options on `SceneRendererOptions`, off by default, not wired to a renderer) and the `GI_CornellBox` sample scene (`Editor/LuxSampleProject/Assets/Scenes`, materials in `Materials/GI`).
- Not started: engine RHI/NRI acceleration-structure wrapper (rest of GI0), GI1 to GI5. These need in-engine GPU runs, which need an editor launch the owner has not approved.
