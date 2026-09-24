# LuxEngine "See the Sound" Plan

This plan makes LuxEngine's acoustics visible and gives the engine a working occlusion signal. It
is the signature feature for the public showcase. In the demo scene, a viewer turns on one overlay
and sees why a sound is muffled: which walls sit between the listener and the source, what they are
made of, how thick they are, and how much of the sound they remove. They also see the room's reverb
and zones. Choosing a render material such as "Brick" then sets both how a wall looks and how it
sounds.

This is a planning document written on 2026-09-24. For what is actually built, see
`.claude/docs/Architecture-LuxEngine.md` § 2.10 Audio.

**Goal card**

- **Goal:** "See the sound". An in-editor visualization of the acoustics (occlusion paths, room
  reverb, zones and portals, surface materials), plus render materials that also drive acoustics.
- **Success:** in `FMODDemo`, with the overlay on during Play, a source behind the building wall
  (1) sounds muffled, (2) shows a listener→source path with the wall hits labeled by material,
  thickness and dB, and (3) goes back to clear sound and a clear path when a door portal opens.
  Assigning an acoustic tag to a render material changes the sound of every wall that uses it,
  unless a wall sets its own.
- **Non-goals:** swapping the audio backend (frozen); ray-traced *graphics* or GI; any temporal
  rendering technique; baked acoustics; running VA in edit mode.
- **Constraints:** FMOD Studio + Vercidium Audio (VA) stay as they are. Existing `.luxscene`,
  `.lmaterial` and `.luxproj` files still load and sound the same unless the author opts in. No new
  installs. Order of work: make it work → make it good → make it fast.
- **User decisions (2026-09-24):** engine-side occlusion; the acoustic tag lives on the render
  material; visualization of the simulation runs in Play only; the plan lives in this file.

**Decisions this plan is built on**

| Decision | Choice | Consequence |
|---|---|---|
| Occlusion source | **Engine-side**, from Jolt raycasts through acoustic geometry (user) | Muffling works now. VA occlusion stays selectable for when it is fixed upstream. This adds a second occlusion *source*, but still only one filter path: the Studio `Occlusion` parameter. |
| Default for projects without the setting | Engine occlusion | VA returns ≈0.998 everywhere today, so old projects gain the muffling their authors expected. The change is audible, so it's written in the release notes. |
| Where the acoustic tag lives | Optional tag on `MaterialAsset` (user) | Precedence follows Steam Audio: `AudioSurfaceComponent` → explicit collider tag → render material tag → Default. |
| How old colliders keep their sound | NEW `MeshColliderComponent::AcousticFromMaterial`. A missing key reads as `false`; new colliders default to `true` | Every existing scene writes `AcousticMaterial` explicitly (`SceneSerializer.cpp:721`), so old colliders keep their tag and nothing changes silently. |
| Granularity of material inheritance | Per entity: the material of the collider's selected submesh, else material slot 0 | The geometry system holds one `AcousticGeometry` per entity (`Scene::BuildAcousticGeometry`). A per-submesh split is a follow-up. |
| Multi-hit raycast | NEW `PhysicsScene::CastRayAll` using Jolt's all-hit collector | `CastRay` only returns the closest hit (`ClosestHitCollisionCollector`, `PhysicsScene.cpp:1094`). Excluding and recasting per wall would cost k casts per source. |
| Portals in occlusion | Analytic segment-vs-rectangle test using the portal's `Open` | Portals exist only as VA shutters, not as Jolt bodies, so a raycast can't see a closed door. |
| When the simulation is drawn | Play only (user) | The material view (Phase 3) reads only components, so it also works in edit mode. |
| Drawing path | The existing `EditorLayer::DrawAudioVisualisation` overlay, plus per-tag collider colors in the collider pass | No new render pass. The material view uses 23 fixed collider materials created in `Init()`. |

---

## Part 0 — Where we are

| Capability | State | Evidence |
|---|---|---|
| VA ray visualization (listener rays, bounces, normals) | ⚠️ Built in `bf87229` (2026-09-09). Not verified at runtime in this session | `RaytracedAudioScene::SetVisualisationEnabled/GetVisualisation`; `EditorLayer::DrawAudioVisualisation` (`EditorLayer.cpp:2488`) |
| Visualization settings UI | ⚠️ Built | `AudioVisualisationSettings`, `AudioDebugPanel::UI_Visualisation` |
| Source→listener line | ⚠️ Built. It's a plain straight line and carries no data | `DrawAudioVisualisation`, `DrawEmitters` branch |
| VA per-source occlusion | ❌ Broken upstream: output is bit-identical with or without geometry (1.8.0 and 1.9.0) | `docs/vercidium-repro/occlusion_output.txt`; `AUDIO_SYSTEM_PLAN.md:62` |
| VA reverb/ambience | ✅ Responds to geometry (it's the control column in the repro) | `RaytracedAudioAmbience` (returned/outside %, decay LF/HF, absorption) |
| Occlusion → FMOD path | ✅ Working plumbing. The value it carries is flat | `Scene.cpp:870-875`: `AudioEventAcoustics::OcclusionGainLF` → Studio `Occlusion` |
| Acoustic geometry | ✅ Mesh colliders only, one tag per entity | `Scene::SyncAudioGeometry`, `BuildAcousticGeometry` (`Scene.cpp:1624-1702`) |
| Acoustic tags and coefficients | ✅ 23 stable tags, per-tag LF/HF transmission metres and flat loss | `AcousticMaterial.h` — `AcousticMaterialProperties` |
| Tag precedence today | ✅ `AudioSurfaceComponent` > `MeshColliderComponent::Acoustic` | `SyncAudioGeometry` |
| Render material ↔ acoustics | ❌ Independent by design | Architecture § 2.10, "renderer materials remain independent" |
| Zone/portal lines | ✅ Selected entities only | `FrameRenderPacket::AudioZoneLines` |
| Collider debug pass | ✅ Two fixed colors (simple/complex) | `SceneRenderer.cpp:7761-7790`, `FrameRenderPacket::ColliderDebugItem` |
| Raycast | ✅ Closest hit only, main thread | `PhysicsScene::CastRay`. `Scene.cpp:1734` simulates on the main thread; the simulation thread is unwired (`Threading.md`) |
| Material YAML/packing | ✅ Optional keys written only when not default; asset packs store the YAML | `MaterialSerializer.cpp:283`, `SerializeToAssetPack` |
| VA lifecycle | Play only; Simulate mode does not start VA | `Scene::OnRuntimeStart` → `OnRaytracedAudioStart` |
| Runtime project format | Version 24 | `ProjectRuntimeFormat.h:27` |
| Local VA SDK (Linux dev box) | ⚠️ **1.8.0**, not the 1.9.0 the branch targets | `vaudio.h` `VA_VERSION_MINOR 8` (gitignored) |

---

## Part 1 — Goals and non-goals

See the Goal card. The showcase must demonstrate three things together: **hear** it (muffling and
reverb), **see** it (paths, hits, rooms), and **author** it once (the material drives both look and
sound).

---

## Part 2 — Design

```
Scene (main thread, runtime update, after physics simulate + VA join)
 ├─ AudioOcclusion (NEW, Core/Source/Lux/Audio)   per-source gain from CastRayAll + portals
 │    └─ OcclusionDebug records (last result per source: hits, material, thickness, dB)
 ├─ RaytracedAudioScene                            reverb/ambience (unchanged), VA occlusion (optional)
 └─ AudioEventAcoustics.OcclusionGainLF  ← Engine | Raytraced  (ProjectAudioSettings::OcclusionSource)

EditorLayer::DrawAudioVisualisation (main thread, Renderer2D inside OnOverlayRender)
 ├─ occlusion paths + hit labels  ← Scene::GetAudioOcclusionDebug()
 ├─ VA rays/bounces (existing), room ambience HUD, zones/portals (all)
SceneRenderer collider pass
 └─ per-tag colored acoustic geometry (material view, edit + Play)
```

**Occlusion model.** For each audible, unculled source, cast listener→source with `CastRayAll`. Keep
only hits whose entity contributes acoustic geometry (mesh collider, motion not Disabled). Characters,
triggers and primitive colliders are ignored, which matches what VA sees. Pair the entry and exit
hits of each entity to get its thickness. For one-sided or unpaired geometry, use `FlatTransmission`.
Each wall multiplies the gain: `g *= exp(-thickness / TransmissionLF)`. The curve is calibrated by ear
in Phase 0. Closed portal rectangles crossed by the segment add their material weighted by
`1 − Open`. The resulting gain is smoothed per source over 80–150 ms. This is audio parameter
smoothing, not temporal rendering. Casts run at a fixed rate (default 20 Hz), staggered across
sources, with a per-frame cast budget.

**Threads.** Everything new runs on the main thread in `Scene::OnUpdateRuntime`, after physics
simulate and the VA join, in both `MultiThreaded` and `SingleThreaded`. Jolt narrow-phase queries on
the main thread between steps are already used by footsteps (`Audio.PlayFootstep`). The editor reads
debug records on the main thread. The only renderer input is the collider-pass colors, which go
through `FrameRenderPacket`.

**Ownership.** `AudioOcclusion` is a scene-owned value member, cleared on stop, entity destruction
and culling. It owns no GPU resources. The 23 collider materials are created once in
`SceneRenderer::Init` and freed with the renderer.

**Serialization.**
- Project: `Audio.OcclusionSource` (Engine/Raytraced), `Audio.OcclusionRateHz`,
  `Audio.OcclusionCastBudget`, plus runtime format **25** (a bounded block appended after 24's
  fields). Formats 16–24 read as Engine with defaults.
- Scene: `MeshColliderComponent.AcousticFromMaterial` (missing → false).
- Material: optional `AcousticMaterial: <Name>` key, written only when set.

**Editor.** The Audio Debugger's Visualisation section gains an "Occlusion" group. A single viewport
toolbar toggle, **See the sound**, turns on the showcase preset. The Material Editor gains an
"Acoustics" row, and the collider inspector gains "Inherit from material".

**Runtime/Dist.** Occlusion is engine code and runs in `Lux-Runtime` and Dist. Visualization is
editor-only. The runtime has no C# API for this until Phase 5.

---

## Part 3 — Phases

Every phase builds Core then Editor in Debug and Release on Linux (plus a Windows build before any
PR), runs the editor on `LuxSampleProject`, runs `tests/audio/run.py` where audio code changed, ends
with `/cr`, and is committed on its own. Phases that add files regenerate projects
(`Win-GenProjects.bat` / `./premake5 gmake2`).

### Phase 0 — Prerequisite and spike: occlusion you can hear

**Goal:** prove that a raycast-thickness model produces convincing muffling in `FMODDemo` at an
acceptable cost, before any UI exists.

**Changes**
- Bring this Linux box's VA SDK to 1.9.0, then re-run all three audio test runners. The PR #31 Linux
  results were recorded against 1.8.0.
- `Core/Source/Lux/Physics/PhysicsScene.{h,cpp}` — **NEW** `CastRayAll(const RayCastInfo*,
  std::vector<SceneQueryHit>&)` using `JPH::AllHitCollisionCollector`, sorted by distance, returning
  back-face hits so exits can be found (`RayCastSettings::mBackFaceMode`).
- `Core/Source/Lux/Audio/AudioOcclusion.{h,cpp}` — **NEW** pure function: segment hits + per-tag
  `AcousticMaterialProperties` + portal rectangles → LF gain, plus a debug record.
- `Scene.cpp` (~line 870) — behind a temporary hard-coded switch, replace
  `result.OcclusionGainLF` with the engine gain.

**Playbook:** none applies. No new component, asset type or pass.

**Verification**
- Numbers: one listener with 16 and then 64 sources in `FMODDemo`; Tracy zone
  `AudioOcclusion::Update` < 0.3 ms/frame at 20 Hz on the dev box.
- Listening: the `MusicAmbiance` event inside the building is audibly muffled from outside and
  clear inside. Tune the curve constant by ear and record it.
- Headless: a new check in `PhysicsAudioTests`, a wall between two points: gain < 1, and gain
  decreases as thickness grows.

**Exit criteria:** audible, cheap, deterministic in the headless test. **Rollback:** delete the switch.

### Phase 1 — Engine occlusion ships

**Goal:** engine occlusion is the default, configurable, and in exported games.

**Changes**
- `ProjectAudioSettings`: `OcclusionSource`, `OcclusionRateHz`, `OcclusionCastBudget`, plus YAML,
  Project Settings UI, runtime format 25 (`ProjectRuntimeFormat.h` `Version = 25`) with the bounded
  block and older-version defaults.
- `Scene`: an owned `AudioOcclusion` with staggered scheduling, smoothing, and clearing on culling
  and destruction; the acoustic-geometry filter; portals.
- `AudioValidation`: warn when an event on an occludable source lacks the `Occlusion` parameter.

**Thread and lifetime:** main thread; cleared in `OnRuntimeStop` and on entity destruction.

**Verification**
- Run: Play `FMODDemo`, then walk in and out of the building and through the door with the portal
  open and closed. Switch the source to Raytraced and the muffling disappears, as expected.
- Export a runtime and repeat. Load an old format-24 export and it plays with Engine occlusion.
- Log: no audio errors; the validation warning appears for a deliberately stripped event.
- Headless: extend `PhysicsAudioTests` (portal open/closed, culled source cleared, format-25
  round-trip).

**Docs:** Architecture § 2.10 (occlusion source, model, format 25); `docs/AUDIO_DYNAMIC_GEOMETRY.md`
(portals now occlude); `Threading.md` if the scheduling adds a rule.

**Exit criteria:** tests pass, export works, and the listening check passes. **Rollback:** set the
default source to Raytraced.

### Phase 2 — See the occlusion (the showcase moment)

**Goal:** the overlay shows each source's path, the walls it crosses, and what they remove.

**Changes**
- `Scene` — **NEW** `GetAudioOcclusionDebug()` returning the last records (main thread, read-only).
- `EditorLayer::DrawAudioVisualisation` — replace the plain source line with the path colored by
  gain (green → red), entry and exit markers per wall, and a `Renderer2D::DrawString` label per wall
  (`Brick · 0.30 m · −9 dB`) plus a per-source total. Portals crossed are drawn open or closed.
- `AudioVisualisationSettings` — `DrawOcclusionPaths`, `DrawOcclusionLabels`, `OnlySelectedSource`.
- Viewport toolbar — **NEW** "See the sound" toggle that turns on the curated preset (paths,
  labels, emitters, zones for everything) and turns it off again.

**Verification**
- Run: the success scenario from the Goal card, from three camera angles. Labels are readable and
  paths stop at the source.
- ImGui: new settings have unique IDs, scopes close on every path, and every toggle is exercised
  during Play and Stop.
- Log: nothing new.

**Docs:** Architecture § 2.10 "Editor observability". **Exit criteria:** the Goal card scenario is
visible. **Rollback:** remove the draw branch; the data stays harmless.

### Phase 3 — Material view (edit mode too)

**Goal:** a viewport mode that tints every acoustic surface by its effective tag, with a legend.

**Changes**
- `FrameRenderPacket::ColliderDebugItem` — add `Color`.
  `SceneRenderer` — 23 per-tag collider materials created in `Init()`, chosen per draw. No new
  pipeline and no new `(set, binding)`.
- `Scene::CaptureColliderDebug` — in acoustic mode, submit only acoustic geometry, colored by the
  **effective** tag (the same resolver as `SyncAudioGeometry`, factored into **NEW**
  `Scene::ResolveAcousticMaterial(Entity)`).
- Editor — a legend overlay (ImGui, viewport-anchored) and a toggle in the viewport menu.

**Playbook:** none. This uses the existing collider pass, not a new render pass.

**Verification:** in edit mode and in Play, the building walls show their tags. Changing a
collider's tag recolors it on the next frame. Validation layer clean. `/profile` shows no cost with
the view off.

**Docs:** `Rendering.md` (the collider pass now takes a per-item color).

### Phase 4 — One material, look and sound

**Goal:** a render material carries an acoustic tag, and colliders can inherit it.

**Changes**
- `MaterialAsset` — optional `AcousticMaterial`, plus the `MaterialSerializer` key (written only
  when set). Asset packs carry it inside the YAML, so no pack format change.
- Material Editor — an "Acoustics" row with a tag picker, and a thumbnail badge.
- `MeshColliderComponent::AcousticFromMaterial` (NEW; YAML missing → false; new components → true),
  inspector "Inherit from material". Copy/duplicate/prefab paths carry it (the field
  steps of Architecture Part 4 "Add a new component": copy/duplicate/prefab paths, both serializer
  directions, and the `SceneHierarchyPanel` inspector).
- `Scene::ResolveAcousticMaterial` — precedence `AudioSurfaceComponent` → explicit collider tag →
  render material of the selected submesh (else slot 0) → Default. `SyncAudioGeometry`, occlusion and
  the material view all use it. A material edit during Play updates the tag through the existing
  `UpdateGeometry` path.
- C#: `MeshColliderComponent.Material` keeps returning the *effective* tag (read-only, unchanged).

**Verification**
- Open every sample scene. Before/after YAML diffs show no tag changes, and the material view looks
  the same.
- Set `Bricks097` → Brick, create a new wall with that material and a collider, and hear and see it
  as Brick. An `AudioSurfaceComponent` override still wins.
- Export a runtime, and the effective tags match the editor.
- ImGui checks for both new widgets.

**Docs:** Architecture § 2.10 (the "renderer materials remain independent" sentence changes),
§ 2.8 if the material asset schema is described there, and `docs/MATERIAL_EDITOR_PLAN.md` gets a
pointer.

### Phase 5 — Rooms, reverb, and breadth

**Goal:** the rest of the story is visible and scriptable.

**Changes**
- A viewport HUD for listener ambience: returned vs outside energy, LF/HF decay and average
  absorption from `RaytracedAudioAmbience`, labeled as VA measurements.
- Zones and portals: "show all" with live weights (extends the `AudioZoneLines` capture beyond the
  selection).
- VA ray visualization: verify it at runtime (it's unproven in Part 0), and color bounces by the
  effective tag of the entity hit (a nearest-hit lookup via `CastRay` on bounce, visualization only).
- C#: `AudioSourceComponent.OcclusionGain` read-only, for gameplay (for example, AI hearing).
  Follows Architecture Part 4 "Add a C# internal call".

**Verification:** walk from the building into the open. The HUD shows outside energy rising and
decay falling. Zone weights animate across the portal.

### Phase 6 — Showcase hardening

**Goal:** it holds up in front of an audience.

- Author the demo: tag the demo materials, add a door with an `AudioPortalComponent`, and make a
  scripted camera path for the recording.
- Windows build and run, Linux Wayland run, Release and Dist runtime export (occlusion audible,
  no editor overlay).
- `/profile`: 64 sources within budget; overlay cost reported.
- Docs: `docs/Editor` audio pages, and a "See the sound" section in the README showcase.

---

## Part 4 — Verification

The Goal card scenario, recorded as a video, in the editor (Phase 2) and as an exported runtime
(sound only). The headless suite covers the occlusion math, portals, culling, the format-25
round-trip and tag precedence. Every sample scene must load with no tag change (Phase 4).

## Part 5 — Risks

| Risk | Likelihood | Early detection |
|---|---|---|
| Thickness pairing fails on open or non-manifold meshes (single planes, missing back faces) | High | Phase 0 test with a plane wall. Fall back to `FlatTransmission` |
| Engine and VA disagree once VA occlusion is fixed upstream | Medium | The source is a setting. Compare the two in the debugger when VA ships a fix |
| Colliders don't match visuals (simplified colliders), so paths look wrong on screen | Medium | Phase 2 draws hits on colliders. The material view shows the colliders themselves |
| Cast cost with many sources | Low–medium | Phase 0 budget numbers; staggering and the cast budget |
| Default switch changes how existing projects sound | Certain, by design | Release note. The Raytraced option restores the old behavior |

## Part 6 — Open questions

- The exact transmission curve and constant. Settled by ear in Phase 0.
- Whether `RayCastSettings` back-face hits work on Jolt mesh shapes in the vendored version.
  Settled by the Phase 0 test.
- Whether the existing VA visualization rays render correctly on VA 1.9.0. Checked in Phase 5, or
  earlier if Phase 0's SDK update makes it cheap.
- Whether the HF band should also reach Studio. `AudioEventAcoustics` carries only LF today, and
  Studio has one `Occlusion` parameter. Out of scope unless the sound designer asks for it.

**What would invalidate the plan:** if Phase 0's muffling isn't convincing or costs too much, stop
and revisit the occlusion decision with the user before building UI on top of it.

## Part 7 — Research notes

**Goal:** make acoustics visible, and author surfaces once.

- **Prior art — visualization:** Wwise's Game Object 3D Viewer and Profiler draw the listener,
  emitters, spatial-audio geometry, rooms, portals, and diffraction/transmission paths in real time.
  This is the reference for Phase 2 and Phase 5.
  [Wwise Spatial Audio](https://www.audiokinetic.com/products/wwise-spatial-audio/) ·
  [Rooms and Portals guide](https://blog.audiokinetic.com/rooms-and-portals-with-wwise-spatial-audio/)
- **Prior art — material precedence:** Steam Audio uses the explicit component material first, then a
  physics-material → acoustic-material mapping, then a default. This is the Phase 4 precedence.
  [Steam Audio Unreal settings](https://valvesoftware.github.io/steam-audio/doc/unreal/settings.html) ·
  [Steam Audio Geometry](https://valvesoftware.github.io/steam-audio/doc/unreal/geometry.html)
- **Prior art — tying acoustics to visual materials:** Project Acoustics binds acoustic materials to
  Unity visual materials and suggests them from the visual material name. That supports the Phase 4
  model. Name-based suggestion is a possible follow-up.
  [Project Acoustics material assignment](https://learn.microsoft.com/de-de/gaming/acoustics/unity-baking-materials)
- **Concept:** absorption per band plus scattering (Steam Audio: low/mid/high absorption, 0–1
  roughness) matches VA's LF/HF absorption and scattering, so the existing tag coefficients carry
  over.
  [Steam Audio Material](https://valvesoftware.github.io/steam-audio/doc/unity/material.html)
- **Rejected:** baked acoustics (Project Acoustics' probe bake). It adds an offline pipeline and
  conflicts with "small and refined". It also isn't needed, since VA is real-time.
- **Rejected:** temporal accumulation of the visualization. It's forbidden, and it isn't needed.
- **Still unknown:** a published model for VA's "transmission metres" semantics, which Phase 0
  calibrates empirically.

## Follow-ups (out of scope)

Per-submesh acoustic tags; name-based tag suggestion; HF occlusion to Studio; diffraction around
portal edges; VA in edit mode.
