# LuxEngine Particles Plan (M13, issue #49)

**Status (2026-11-19, PR #201):** done. Where the build differs from this plan: curve and gradient types, the shapes are cone, sphere and box (no circle), the Particles pass samples PreDepth without a depth attachment as planned (no validation messages with the Khronos layer forced on), the batch texture table is 16 slots in the material's set 0, Premultiplied needed a new `FramebufferBlendMode`, and `EditorLayer`'s Edit state steps previews itself because `OnRenderEditor` takes no time step. Measured on an RTX 4070 Ti at 1280x720 with 10 000 large alpha particles in one column: +0.32 ms GPU, +0.43 ms frame.

A planning document, written 2026-11-19. What is actually built is described by `.claude/docs/Architecture-LuxEngine.md` (a section is added in Phase 1). Checkpoints are the board's sub-issues: **G1 #107** (engine) and **E10 #121** (editor). G1 is XL, so Part 3 cuts it into phases that each stand alone; the board should get matching sub-issues under #107 before work starts.

## Goal card

- **Goal:** a basic CPU-simulated particle system that draws in 3D and in 2D, with curves over life, sprite-sheet (sub-UV) animation, soft particles and instanced drawing, plus an editor panel.
- **Success:** an entity with a `ParticleSystemComponent` shows smoke/fire/sparks in a 3D scene (HDR, soft against geometry, bloomed) and in a 2D scene (sorting layers); it plays in editor Play, in the exported Windows and Linux player, and in a preview in edit mode; C# can play, stop and burst it; a headless check, a Play test and a golden scene cover it.
- **Non-goals (deferred, per the catalog):** GPU simulation, trails/ribbons, mesh particles, collisions, sub-emitters, lit particles, velocity-stretched billboards, particles in world-space UI.
- **Constraints:** no temporal techniques (soft fade and sorting are per frame, spatial); nothing new to install; existing scenes and projects load unchanged (new component only); Windows and Linux; Dist player has no shader compiler (new shader goes in the pack).
- **User decisions (2026-11-19):** settings live **on the component** (no new asset type); build the **inspector and a Particle Editor panel** (E10), last, so it is the first thing cut if time runs out.

## Decisions this plan is built on

| Decision | Choice | Consequence |
|---|---|---|
| Settings storage | On `ParticleSystemComponent` (user) | Playbook "new component" (copy, serialize, inspector, packet, C#), prefabs reuse effects; no asset type. |
| 3D draw path | A dedicated "Particles" render pass, HDR, before bloom and composite | Soft particles, bloom, exposure work. Rejected: the Renderer2D overlay (it runs after tone mapping and SMAA: LDR, no soft fade). |
| Soft fade and depth test | In the shader from the sampled PreDepth, color-only pass, no depth attachment | Avoids sampling a depth image that is also attached. Same trick the tone-mapping pass uses. Spiked in Phase 3. |
| 2D draw path | One `Draw2DItem::Kind::Particles` item per system, drawn through `Renderer2D::DrawQuad` | Sorted with sprites by the existing layer/order key; no new render code for 2D. |
| Simulation | Pure code in `Lux/Particles/` (no registry, no GPU), struct-of-arrays with swap-remove, fixed 1/60 s steps, seeded RNG | Checked headless like `UILayout`; deterministic for tests and goldens. |
| Runtime state | In a scene-owned map by entity id, never in the component | Not serialized, not copied by Play's scene copy; a fresh Play starts clean. |
| Curves and gradients | Two small types: `ParticleCurve` (≤ 8 linear keys), `ParticleGradient` (≤ 8 color + alpha keys) | None exist in the engine today. Piecewise linear is enough and cheap to evaluate. |
| Blend modes | Alpha, Additive, Premultiplied per system (one pipeline each) | Three pipelines created once in `SceneRenderer::Init`. |
| Sorting | Alpha systems sorted back-to-front by view depth inside the system; additive not sorted; systems sorted by distance | Documented limit: particles draw after transparent meshes and do not sort against them. |
| Budgets | 1 000 default, 20 000 hard cap per system, 50 000 per frame | Over-budget spawns are dropped and counted, never crash. |
| Threads | Simulation on the main thread inside the scene update; the render thread reads only the packet | Same rule as UI: `FrameRenderPacket::Particles`, nothing reads the ECS at submit. |

## Part 0 — Where we are

Read this session, not run (⚠️ = built, unproven for this use; ❌ = missing).

| Capability | State | Evidence |
|---|---|---|
| Any particle code | ❌ none | grep of `Core/`, `Editor/`, shaders, ScriptCore finds only docs. |
| Frame packet | ✅ | `Renderer/FrameRenderPacket.h`: `Draw2D`, `UI`; `Scene::SubmitRenderPacket` copies them into the overlay callback. |
| 2D quads incl. UV rectangles, depth tested in 3D | ✅ | `Scene::SubmitRenderPacket` (`Scene.cpp` ~3965) `DrawQuad(transform, texture, tiling, color, uv0, uv1)`; `Renderer2D::BeginScene(viewProj, view, depthTest)`. |
| 2D sort key (layer, order, depth) | ✅ | `Scene::GetDrawOrder`, `CaptureDraw2D` (`Scene.cpp` ~3736). |
| Overlay runs after tone mapping | ✅ | graph order in `SceneRenderer.cpp` ~4590-4630: Composite, SMAA, DOF, `WorldOverlay2D`. Why 3D particles need their own pass. |
| Transparent pass and where to insert | ✅ | `addPass("Transparent Forward", ...)` ~4522, skipped when no transparent draw; framebuffer spec ~2085 (scene color + normals + PreDepth, reverse-Z `GreaterOrEqual`, no depth write). |
| Depth as a sampled input | ✅ | `PassInputDepth` binds `u_DepthTexture = m_PreDepthPass->GetDepthOutput()` (`SceneRenderer.cpp` ~3002). Transparent forward uses it. |
| Per-instance vertex streams | ⚠️ | `PipelineSpecification::InstanceLayout`, `Pipeline.cpp` ~284-322 maps it to `PER_INSTANCE`; `IndexedDraw::InstanceCount` exists (`Renderer.h`). No pass uses it yet. |
| Dynamic per-frame vertex data | ✅ | `Renderer2D` fills per-frame-in-flight `VertexBuffer`s with `SetData` and draws via `Renderer::RenderGeometry`. |
| Texture binding for arbitrary textures | ❓ | Renderer2D uses a texture slot array; the bindless table is for materials. Phase 3 spike decides. |
| Scene update hooks | ✅ | `OnUpdateRuntime` (1212), `OnUpdateSimulation` (1491, runs `UpdateSpriteAnimators`), `OnUpdateEditor` (1513, ticks only animators); `RefreshInactiveEntities`, `IsInactive`. |
| Curves/gradients, random helper | ❌ | none found (`SoundCurveTarget` is audio only). |
| Test rigs | ✅ | `tests/2d/Checks2D.cpp` (headless, Core.lib), `tests/play/run_play.py`, `tests/runtime/run_standalone_export.py`, `LUX_RUNTIME_CAPTURE`, `tests/rendering/golden_compare.py`. |

## Part 1 — Goals and non-goals

See the Goal card. Observable end state, checked in Phase 6: a golden 3D scene with smoke and sparks, a 2D scene using the same component, the Play test group, the exported Windows and Linux players, and one sample game using it where it fits.

## Part 2 — Design

**Layout.** `Core/Source/Lux/Particles/ParticleSystem.{h,cpp}` **NEW**: `ParticleSettings` (plain data: emission rate, bursts, duration, looping, prewarm, max particles, start lifetime/speed/size/rotation/color as min..max, shape point/sphere/box/cone/circle, gravity, drag, simulation space local/world, size and color over life, speed over life, sub-UV grid and frame mode, blend, soft distance, render mode 3D billboard or 2D sprite), `ParticleCurve`, `ParticleGradient`, `ParticleState` (SoA arrays, RNG, time, emission accumulator, counters), and `Step(state, settings, emitterTransform, dt)` plus `Fill(instances, state, settings, view)`. No dependency on `Scene`, `Renderer` or ImGui.

**Component.** `ParticleSystemComponent` in `Components.h`: `Enabled`, `ParticleSettings Settings`, `AssetHandle Texture`, `uint32_t Seed`, `SortingLayer`, `OrderInLayer`, `bool PlayOnAwake`, `bool PreviewInEditor`. Saved in the scene only for the non-default fields (new component, so old scenes are unaffected).

**Scene.** `Scene::UpdateParticles(ts)` steps every active system (skips `IsInactive`, honours pause/step like `UpdateSpriteAnimators`) from `OnUpdateRuntime`, `OnUpdateSimulation`, and, for components with `PreviewInEditor` or the selected entity, `OnUpdateEditor`. State lives in `Scene::m_ParticleStates` keyed by entity id; entity destroy and scene stop erase it. `CaptureParticles(packet)` appends `FrameRenderPacket::Particles` (3D: per system a texture, blend, soft distance, and a vector of 48-byte instances `{position, rotation, size, color, uvRect}`) and 2D systems as `Draw2DItem::Kind::Particles` items with the same instances.

**Render, 3D.** `SceneRenderer::SubmitParticles(...)` stores the frame's batches; a "Particles" pass is added after "Transparent Forward" only when the frame has particles (`executable ? !m_ParticleBatches.empty() : true`, like the transparent pass). Shader `Particles.glsl` **NEW**: vertex stage builds a camera-facing quad from the camera's right/up (`UBCamera`) and the instance, fragment samples the texture, multiplies instance color, and fades by `saturate((linearSceneDepth - linearParticleDepth) / softDistance)` read from `u_DepthTexture`. One static unit quad (4 vertices, 6 indices) plus a per-frame-in-flight instance buffer (`InstanceLayout`); one draw per batch with `InstanceCount = n`. The pass writes scene color only (additive/alpha/premultiplied pipelines), so HDR colors reach bloom and exposure.

**Render, 2D.** The overlay callback loops a `Particles` item's instances and calls `DrawQuad` with a transform built from position/rotation/size and the instance's UV rectangle. Renderer2D batches them.

**Threads.** Step and capture: main thread, scene update. Render: the packet is copied into render-thread work as the UI items are; `SceneRenderer` owns its instance buffers and resizes them on the render thread through `Renderer::Submit`. Correct under `MultiThreaded` and `SingleThreaded` because nothing crosses except by value.

**Ownership.** Per-frame instance buffers are `Ref<VertexBuffer>` per frame in flight owned by `SceneRenderer`; growth drops the old buffers through the usual deferred release. The component's `Texture` is resolved at capture time to a `Ref<Texture2D>` held by the packet for that frame. Scene switch: the packet holds no GPU object the renderer does not already keep alive for the frame, so no extra GPU drain is needed beyond what scene switching does today.

**Serialization.** `SceneSerializer` writes and reads the component (all of `Settings`, curves as key lists); a missing field loads the default. `Scene::Copy`/`DuplicateEntity`/prefab instantiation copy it. No `.luxproj` change, no asset-pack change (the texture is an ordinary texture reference; export already packs referenced textures).

**Scripting.** `ScriptCore` `ParticleSystem` component wrapper: `Play()`, `Stop()`, `Pause()`, `Clear()`, `Emit(int count)`, `IsPlaying`, `ParticleCount`, `Looping`, `EmissionRate`. Internal calls in `ScriptGlue.cpp` (`ParticleSystem_*`), docs regenerated by `tools/ScriptApiDocs`.

**Editor.** Inspector in `SceneHierarchyPanel.cpp` (collapsing sections by module), curve and gradient widgets in `Core/Source/Lux/Editor/` (NEW, reusable), "Create > Effects > Particle System" menu entry; Particle Editor panel (`Editor/Source/Panels/ParticleEditorPanel.{h,cpp}` NEW, registered in `EditorLayer`) with Play, Pause, Restart, Step, live count and budget drops, and the edit-mode preview of the selected system. ImGui is main thread only.

**Runtime, Dist, Linux.** The player runs the same Scene code. `Particles.glsl` joins the engine shader pack (identity changes, so `Editor/Resources/ShaderPack.lsp` is rebuilt and `tests/shaders/check_engine_pack.py` stays green). No platform-specific code, so Linux needs no `Platform/Linux/` counterpart; it is verified through the WSL standalone export as for M3 and M5.

**Build.** New files: regenerate projects (`scripts\Win-GenProjects.bat < /dev/null`). No new dependency.

**Docs.** Architecture new section "2.11b Particles (M13)"; Rendering.md: pass order line, the new pass's invariants (global `(set, binding)` checked by grep, structure hash, zero cost when off); Conventions.md only if a helper becomes canonical (curve/gradient widgets); README and the feature catalog (M13 row, G1, E10); in-editor Help page `components/particle-system.md`.

## Part 3 — Phases

Each phase builds, runs, verifies, and leaves the engine shippable. G1 = phases 1-4 and 6; E10 = phase 5.

### Phase 1 — A system that emits and draws in 2D (G1)

**Goal:** an entity with a particle component emits particles that fall under gravity and fade, in a 2D scene, in the editor and in Play.

**Changes**
- `Core/Source/Lux/Particles/ParticleSystem.{h,cpp}` — **NEW**: settings subset (rate, lifetime, speed, size, color, gravity, point/cone shape, local/world space), `ParticleState`, seeded RNG, `Step`, `Fill`.
- `Core/Source/Lux/Scene/Components.h` — `ParticleSystemComponent`.
- `Scene.cpp/.h` — `UpdateParticles`, `m_ParticleStates`, `CaptureParticles`, copy handling, hooks in the three update functions (editor only for preview), cleanup on entity destroy and `OnRuntimeStop`; `GetDrawOrder` handles the component.
- `FrameRenderPacket.h` — `Draw2DItem::Kind::Particles` with an instance vector; `SubmitRenderPacket` draws it.
- `SceneSerializer.cpp` — serialize and deserialize.
- `SceneHierarchyPanel.cpp` — plain inspector (drag floats, texture slot) and Add Component entry.
- `tests/2d/Checks2D.cpp` — `CheckParticleSimulation`: spawn count over time, lifetime death, determinism (same seed, same state), budget cap, swap-remove keeps indices valid.

**Playbook:** Add a new component, steps 1-6 and 8 (C# mirror is Phase 4).

**Thread and lifetime:** Step and capture on the main thread; the packet carries instances by value.

**Verification**
- Build: Debug and Release; regenerate projects.
- Run: `python tests/2d/run_2d_checks.py --config Release` passes the new checks; in the editor a 2D scene with the component shows particles in Play; save and reload the scene keeps the settings; duplicate keeps them.
- Log: no new warnings; nothing in the console on a scene switch.
- Play test: `UiTests`-style `ParticleTests.cs` is added in Phase 4; here `tests/play/run_play.py` only needs to still pass.

**Docs:** Architecture 2.11b (simulation, component, packet, threads).

**Exit criteria:** checks pass; 2D particles visible; scene save/load/duplicate round trip.

**Rollback:** revert the commit; no file format is touched except one new optional component.

### Phase 2 — The full emitter model (G1)

**Goal:** the settings the issue names work: curves over life, gradient, sub-UV, bursts, shapes, looping, prewarm, sorting layers.

**Changes**
- `ParticleSystem.{h,cpp}` — `ParticleCurve`, `ParticleGradient`, size/speed/color over life, sub-UV grid with frame-over-life or random frame, bursts (time, count, cycles), shapes (sphere, box, circle added), duration, looping, prewarm, drag, rotation and angular velocity, alpha-system back-to-front sort in `Fill`.
- `SceneSerializer.cpp` — curves and gradients as key lists, only non-default fields.
- `SceneHierarchyPanel.cpp` — inspector fields for the new settings (the nice widgets arrive in Phase 5).
- `Checks2D.cpp` — curve and gradient evaluation (ends, between keys, single key), sub-UV rectangle for frame n, burst timing, prewarm state equals stepping, sort order, serializer round trip of every field.

**Playbook:** none applies beyond Phase 1's serialization step.

**Thread and lifetime:** unchanged.

**Verification:** headless checks pass; a 2D scene with a sprite-sheet flipbook and a gradient looks right in Play (screenshot with `LUX_RUNTIME_CAPTURE`); an old scene file from `dev` still loads.

**Docs:** Architecture 2.11b updated.

**Exit criteria:** every setting in the inspector affects the result; round trip identical.

**Rollback:** revert; the Phase 1 subset still works.

### Phase 3 — Soft particles in 3D with one instanced draw (G1)

**Goal:** a particle system in a 3D scene draws as HDR billboards, fades softly against geometry, and costs one draw per batch.

**Spike first (the unproven part).** Before the full pass: draw one hard-coded batch from `SceneRenderer` with a color-only framebuffer on scene color, sampling `u_DepthTexture`, and confirm in the log (validation layer on) that there is no layout or hazard error and the fade matches the depth. Also settle how the pass binds the batch texture: a Renderer2D-style slot array or the bindless table. If the sampled-depth approach fails, fall back to a depth attachment with `DepthReadOnly` and record why.

**Changes**
- `Editor/Resources/Shaders/Particles.glsl` — **NEW**: choose the `(set, binding)` slots by grepping the whole shader corpus first (`Rendering.md`: the namespace is global).
- `SceneRenderer.h/.cpp` — particle pipelines (alpha, additive, premultiplied) and pass in `Init`, shader dependency registered, instance buffers per frame in flight, `SubmitParticles`, `ParticlesPass`, graph node after "Transparent Forward" with accurate reads (PreDepth, scene color) and writes (scene color), gated on the frame having particles, presence folded into `ComputeStructureHash`.
- `FrameRenderPacket.h` — `Particles` batches; `Scene.cpp` — `CaptureParticles` for 3D systems, `SubmitRenderPacket` hands them over; `VRSession.cpp` cleared or handled like `UI` (see Open questions).
- Debug/stats: Tracy zones `Particles::Step`, `ParticlesPass`; counters for particles drawn and dropped in `RendererStats`.
- Golden scene `tests/rendering`: smoke column and sparks beside a wall and floor.

**Playbook:** Add a new render pass (all six steps).

**Thread and lifetime:** the render thread allocates and writes the instance buffers inside `Renderer::Submit`; frames in flight each have their own buffer.

**Verification**
- Build: Debug, Release, Dist; regenerate projects.
- Run: in the editor a 3D scene shows particles intersecting the floor with a soft edge and without a hard line; bloom picks up an additive HDR particle; turning the component off removes the pass (Renderer Debugger shows no "Particles" node).
- Log: no validation errors, no binding collisions.
- Numbers: `/profile` protocol on the golden scene with 10 000 particles: pass GPU < 0.3 ms and Step CPU < 0.5 ms at 1920x1080 on the RTX 4070 Ti, recorded in the PR.
- Golden: capture and show it to the user to accept as the new reference (a changed or new golden is not a rejection).

**Docs:** Rendering.md pass order and invariants; Architecture 2.3 and 2.11b.

**Exit criteria:** spike result recorded; golden accepted; budget numbers met or reported.

**Rollback:** the pass is gated; reverting the graph node and `SubmitParticles` leaves Phase 1-2 working.

### Phase 4 — C# API (G1)

**Goal:** scripts can control particle systems.

**Changes:** `ScriptCore/Source/Lux/ParticleSystem.cs` **NEW** and the `Entity` component wrapper; `InternalCalls.cs`; `ScriptGlue.cpp` (`ParticleSystem_Play/Stop/Pause/Clear/Emit/GetIsPlaying/GetParticleCount` and property getters/setters, entity-liveness checks); `tests/play/Scripts/ParticleTests.cs` and `ExpectedChecks`/`OtherChecks` raised; API docs regenerated with `tools/ScriptApiDocs`.

**Playbook:** Add a new C# internal call (all four steps) and component step 7.

**Verification:** `tests/play/run_play.py` (editor Play and exported Dist) passes the new checks: Emit(10) gives ParticleCount 10, Stop then Clear gives 0, setting `EmissionRate` changes the live count.

**Docs:** in-editor scripting docs (generated) and `Editor/Resources/Docs/components/particle-system.md`.

**Exit criteria:** Play checks pass in both modes; docs regenerated.

**Rollback:** remove the C# wrapper; engine unaffected.

### Phase 5 — Editor: widgets, panel, preview (E10)

**Goal:** authoring is pleasant: curves and gradients are drawn and dragged, a panel controls playback, an effect previews in edit mode.

**Changes**
- `Core/Source/Lux/Editor/CurveEditor.{h,cpp}`, `GradientEditor.{h,cpp}` — **NEW** reusable ImGui widgets (key add/remove/drag, evaluated preview), used by the inspector.
- `SceneHierarchyPanel.cpp` — module sections (Emission, Shape, Start, Over Life, Texture Sheet, Renderer), Create > Effects > Particle System with a sensible default (a texture-less soft round sprite generated in code, no asset needed).
- `Editor/Source/Panels/ParticleEditorPanel.{h,cpp}` — **NEW**; `EditorLayer.cpp` registers it and the menu toggle.
- `Scene.cpp` — `OnUpdateEditor` steps a system when it is selected or `PreviewInEditor`; the panel's Play/Pause/Restart/Step call scene methods.
- Editor self-test via an env hook (as `LUX_BEAM_SELFTEST`) and `tests/editor/run_particles.py`: create a system, edit a curve and a gradient, restart, check counts, undo.

**Playbook:** Add a new editor panel (all four steps).

**ImGui verification (`Conventions.md § ImGui correctness`):** every Begin/Push/TreeNode closed on each path, unique IDs per curve key and gradient stop (`PushID` by index), `###` for labels that change, popups named consistently; each new state (empty curve, one key, eight keys, drag, delete, preview running, budget exceeded) driven in the editor.

**Verification:** the editor self-test passes under Single and Multi threading; the workspace, panel-control and diagnostic-control smoke tests still pass (the panel list is part of the workspace); a manual-style run through the self-test covers undo. The panel is not driven by hand beyond that; the PR says so.

**Docs:** `Editor/Resources/Docs` page, `docs/Editor/Keybindings.md` if any, Architecture 2.9 panel list.

**Exit criteria:** self-test and smoke tests pass; panel closed and reopened keeps its state through the workspace layer.

**Rollback:** remove the panel registration; the inspector still works with plain fields.

### Phase 6 — Runtime, export, Linux, a real use (G1 and E10)

**Goal:** it works in an exported game on Windows and Linux and something real uses it.

**Changes:** rebuild the engine shader pack (`Editor.exe --build-shader-pack`), run `tests/shaders/check_engine_pack.py`; add a particle system to one of the sample scenes (campfire or dust, whichever the sample has room for) and to `tests/play/run_play.py`'s scene; update README, the catalog (M13/G1/E10 Done notes), the Help page, `docs/PARTICLES_PLAN.md` scope note.

**Verification:** `tests/runtime/run_standalone_export.py` (obfuscated and plain) passes with particles drawn (runtime capture shows non-black particle pixels); the same under WSL Ubuntu 24.04 (Dist Core/Editor/Runtime); Dist editor start test (`run_dist_editor_start.py`) passes with the new pack; CI is green; rebuild `Editor/Editor.vcxproj` Dist explicitly before the export test (the solution does not).

**Exit criteria:** both platforms pass; PR checklist complete.

**Rollback:** the sample scene edit is separate from the engine.

## Part 4 — Verification of the whole

Headless simulation checks (Phases 1-2), Play checks in editor and exported Dist (Phase 4), editor self-test (Phase 5), a 3D golden and a runtime capture of the 2D scene (Phases 3 and 6), the standalone export on Windows and Linux (Phase 6), and the performance numbers from Phase 3.

## Part 5 — Risks

| Risk | Likelihood | Early detection |
|---|---|---|
| Sampling PreDepth in a color-only pass hits a layout or hazard problem | Low (tone mapping and the transparent pass do similar) | Phase 3 spike, validation layer |
| Binding an arbitrary texture per batch has no cheap path | Medium | Phase 3 spike; fallback: a 16-slot array like Renderer2D |
| Particles ignore transparent meshes in the sort (glass in front of smoke) | Certain, by design | Documented limit; revisit in 2028 with a unified transparent sort |
| Packet copy cost of many instances (`SubmitRenderPacket` copies vectors) | Medium at 50 000 | Measure in Phase 3; move instead of copy |
| Goldens not bit-stable (see `golden-settle-residual`) | Medium | Fixed-step simulation, fixed seed, simulate-to-time hook for captures |
| New shader changes the engine pack identity and breaks the CI pack check | Certain until rebuilt | Phase 6 rebuild; run `check_engine_pack.py` locally in Phase 3 |

## Part 6 — Open questions

- How does `VRSession` build packets? It clears `packet.UI`; decide whether 3D particles render in stereo or are cleared too (resolve in Phase 3 by reading it).
- The 2D scene's project type: should a 2D project's Create menu offer the 2D render mode by default (Phase 5, read `Project`/2D type code).
- Which sample game scene gets the effect (Phase 6, ask when reached).
- Whether `ExpectedChecks` / `OtherChecks` need splitting again (Phase 4).

**What would invalidate the plan:** if the render graph cannot take a color-only pass reading PreDepth after "Transparent Forward" without breaking resource states, Phase 3 changes shape (extra draws inside the transparent pass) and Phase 3 is re-planned before the rest.

## Part 7 — Research notes

### Research brief — particles
**Goal:** a basic CPU particle system with soft particles, instanced draw, curves and sub-UV.
- **Concept:** soft particles fade a sprite by the distance between the opaque depth behind it and its own depth, because the hardware depth test is all-or-nothing; the fetched depth must be put in the same space (linear or projective) as the particle's before the comparison. Applies: Phase 3 shader. [NVIDIA, Soft Particles](https://developer.download.nvidia.com/whitepapers/2007/SDK10/SoftParticles_hi.pdf)
- **Prior art:** Unity's Shuriken separates emission, shape, start values, over-lifetime curves and gradients, texture sheet animation (rows x columns) and a renderer module; per-module colors multiply. Applies: the settings layout and inspector sections. [Unity manual](https://docs.unity3d.com/Manual/ParticleSystemModules.html)
- **Prior art:** CPU systems keep live particles packed per property and fill a dead slot with the last live one (swap-remove), accepting lost order because rendering sorts anyway. Applies: `ParticleState`. [Flutter scene ParticleStorage](https://pub.dev/documentation/flutter_scene/0.20.0/scene/ParticleStorage-class.html), [Blender particle architecture](https://developer.blender.org/docs/features/nodes/proposals/particle_system_code_architecture/)
- **Pitfall:** ordering relative to other transparents and layers causes most visible bugs (Unity exposes a sorting fudge and order in layer). The plan sorts inside a system, orders systems by distance, and shares the 2D layer key. [Unity cookbook](https://borbware.github.io/unity-basics-course/unity-cookbook/particle-system)
- **Rejected:** off-screen, low-resolution particle rendering ([GPU Gems 3, ch. 23](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles)) — shrinks silhouettes, adds an upsample pass, not needed for a basic system. GPU compute simulation — deferred by the catalog.
**Informs decisions:** 3D draw path, soft fade in the shader, SoA with swap-remove, sorting.
**Still unknown:** the engine's best way to bind a texture per batch (Phase 3 spike).
