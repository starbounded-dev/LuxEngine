# LuxEngine — What's Next (backlog)

Everything open after the Lucky merge (`feature/lucky-merge`, commit `fe3771d7`), in one list, so it
can travel with the PR. This is a planning document, not architecture: what is built is described in
`.claude/docs/Architecture-LuxEngine.md`. Each item names where it comes from and how sure we are:

- **✔ verified** — read in the source on 2026-10-25.
- **◐ from a plan or note** — stated in a `docs/*_PLAN.md` or a session note; re-check before starting.
- **Decision** — needs your call before work starts.

Sizes: **S** (an hour or two), **M** (a day), **L** (several days), **XL** (a plan of its own).

Standing rules still apply to every item: no temporal rendering (deferred, not on this list);
ray tracing, terrain and DDGI/GI restart from scratch if ever picked up (not on this list);
the DX11/DX12 scaffolding stays; nothing may make game makers install extra software.

---

## 1. Finish verifying the Lucky merge (do these before merging to master)

| # | Item | Size | Source |
|---|---|---|---|
| 1.1 | Click through the Help panel: F1, every section, search, back/forward, a link to a missing page, Copy on a code block, each component's **?** button. Check ImGui shows no ID-conflict popup | S | ✔ built, never driven |
| 1.2 | Run the tutorial end to end on a **new** project: Create Project → Build Scripts → Ctrl+R → Script component → Play → Export | S | ✔ `.csproj` builds with `dotnet`; the editor flow was not run |
| 1.3 | A scene with 40 distinct sprite textures and 40 fonts: draws, no validation errors, no flicker while textures change | S | ✔ code path built, not driven |
| 1.4 | Font cache: corrupt a `.lfa` file, change a font under the same name; both regenerate | S | ✔ by reading only |
| 1.5 | Drag `tests/meshes/SharedMeshNodes.gltf` into a scene: three cubes, all collide, picking hits each | S | ✔ importer walk tested outside the engine |
| 1.6 | A Debug-build golden run with sync validation (`golden_run.py --config debug --sync-validation`) on the branch | S | not run yet |
| 1.7 | Linux build of the branch (`scripts/Linux-Build.sh`): meshoptimizer, new files, `Ref.h` | M | not run |
| 1.8 | Measure editor start-up with the Help panel loading all pages (expected a few ms) and the meshoptimizer LOD cost on big imports (Sponza +170 ms) | S | ✔ LOD cost measured offline only |

## 2. Bugs found but not fixed

| # | Bug | Failure | Size | Source |
|---|---|---|---|---|
| 2.1 | **Fixed (2026-10-25).** A failed async runtime load stayed "pending" forever | `RuntimeAssetSystem::TryLoadData` publishes only successes; `RuntimeAssetManager` erases `m_PendingAssets` only when an asset arrives, so the handle is never retried or reported as failed. Same flaw the research found in Lucky. Its error log is also untagged | S | ✔ `RuntimeAssetSystem.cpp:68-85`, `RuntimeAssetManager.cpp:74,251` |
| 2.2 | **Fixed (2026-10-25).** A StaticMesh with an explicit `SubmeshIndices` list missed the new extra placements | Only "all submeshes" (the empty list) picks up placements appended by the importer | S | ✔ `MeshSerializer.cpp` `SerializeMeshSelectionToYAML`, `StaticMesh::SetSubmeshes` |
| 2.3 | The sample project registers C# build intermediates as assets | `Assets/Scripts/Intermediates/**.cs` gets random-handle `ScriptFile` entries in `AssetRegistry.lzr` on every launch, dirtying a tracked file. New projects avoid it (intermediates in `Cache/`); the sample's premake still writes into `Assets` | S | ✔ seen in every golden run today |
| 2.4 | Mirrored placements (negative scale) render and collide inside-out | Winding flips under a negative-determinant transform; true for any negatively scaled entity, now reachable from glTF instancing | M | ✔ fixture has one; render/collision path not checked |
| 2.5 | `C# Entity.Destroy()` may not fire `OnDestroy` | `OnDestroy` is invoked on Play stop and undo restore; the script-side destroy path was not traced | S | ◐ found while writing the manual |
| 2.6 | `RenderStatsPanel` is never registered | Dead panel; the Profiler panel replaced it | S | ✔ `EditorLayer.cpp` has no `RenderStatsPanel` |
| 2.7 | `MeshSource::GetRootNode()` returns the importer's empty placeholder node, not the real root | No caller today, so latent | S | ✔ `Mesh.h:309`, `AssimpMeshImporter.cpp` root placeholder |

## 3. Lucky research follow-ups (not in the merge)

From `docs/LuckyEngine-Comparison.md` (decisions table) and `docs/LUCKY_MERGE_PLAN.md` Part 2.

| # | Item | What it buys | Size | Notes |
|---|---|---|---|---|
| 3.1 | **Fixed-step scripting:** C# `OnFixedUpdate(float)` called once per physics step, before it | Forces and controls applied at a stable rate, independent of frame rate | M | **Decision:** semantics and physics stepping one Jolt step at a time. ✔ scripts only get frame `OnUpdate` today (`Scene.cpp` `OnUpdateRuntime`) |
| 3.2 | **Character controller order:** compare Lux (controller simulates after Jolt) with Lucky (before) on jump, slopes, moving platforms | Correct character physics | M | ✔ `PhysicsScene::Simulate` |
| 3.3 | **Jolt bulk body add** (`AddBodiesPrepare/Finalize`) on Play start and prefab spawns | Faster Play start in large scenes | S | measure first |
| 3.4 | **Dependency-aware geometry cache:** keep processed geometry (LODs) in `Cache/` keyed by source contents + importer/processor version, validated, written atomically | Removes the meshoptimizer import cost on every load | M | ✔ every load re-imports today (`MeshSourceSerializer::TryLoadData`) |
| 3.5 | **Validate the rest of the asset pack:** scene/prefab/texture/audio entries and the pack index itself, like the mesh reader now does | A damaged game cannot crash the player | M | ✔ `StreamReader::ReadArray` trusts counts |
| 3.6 | **Profiling readiness:** golden/perf captures wait for content readiness, record warm-up and accepted-sample counts, flag timeouts | Trustworthy A/B numbers | S | ◐ research R14 |
| 3.7 | **Cluster light culling:** pre-transform lights once, split cluster tests across lanes, 16-bit light IDs, report list overflow visibly | GPU time with many lights | M | ◐ research R7; profile first |
| 3.8 | **Shadow caster volume** (Lucky's PSC) for cascades; test tall off-screen casters and low sun | Fewer missed or wasted shadow casters | M | ◐ research R9 |
| 3.9 | **Split deferred lighting** (base / punctual / area draws) as an opt-in experiment with HDR parity | Possibly lower register pressure | M | ◐ research R8; Lucky's win was on Apple M2 only |
| 3.10 | **Prefab structural reconciliation:** added, deleted and re-parented children follow the prefab; keep variants | Prefabs that survive hierarchy edits | L | **Decision:** override rules for deleted/added children |
| 3.11 | **Debug drawing from C#** (`Gizmos.DrawLine` etc.), persisted per update phase so it does not flicker | Gameplay debugging | M | ✔ no debug-draw internal calls in `ScriptGlue.cpp` |
| 3.12 | **Project-owned services:** separate the process-wide .NET/Jolt host from per-project script and physics state | Clean project switching, isolated tests | L | ◐ research runtime §1; only if a concrete bug needs it |
| 3.13 | **Crash watchdog:** a small parent process that survives the editor, collects dumps and the last log, shows a minimal report | Diagnosable crashes and hangs | L | ◐ research; existing Aftermath stays |
| 3.14 | **Owner-tagged deferred events:** queued main-thread work tagged with its owner, dropped when the owner goes | Async jobs that outlive a panel or project | M | ◐ research (Lucky `EventBus`) |
| 3.15 | **Editor tool host:** owner-thread, undo-aware scene mutations for scripted or external tools | Basis for automation and tests | L | ◐ research; see the SolidCraft MCP note |
| 3.16 | **Generated C# API reference** in the manual from ScriptCore XML comments, checked against the built assembly | Docs that cannot drift | M | ✔ manual has hand-written API notes |
| 3.17 | **More manual pages:** materials, Material Editor, Game View, physics layers, export settings, audio authoring in FMOD, prefabs walkthrough, a 2D tutorial | Complete onboarding | M | ✔ 17 pages today |
| 3.18 | **Point-light octahedral shadow atlas** instead of six spot tiles per light | Memory/descriptor cost of point shadows | M | ◐ research R9; only if measured as a bottleneck |
| 3.19 | **Animation pipeline:** import skeletons and clips, compressed sampling (ACL is vendored), blending, root motion, GPU skinning, reload | Animated characters | XL | ✔ asset types exist (`Skeleton`, `Animation`), no runtime pipeline; earlier plans called this "a separate plan later" |

## 4. Plans already in flight

Check each plan's status section before starting; these lines summarise them.

| # | Plan | Open part | Source |
|---|---|---|---|
| 4.1 | `AREA_LIGHT_SHADOWS_PLAN.md` | Phases 1-3 marked "code written, not yet run"; Phase 4 runtime checks are yours. The merged PR #35 contains shadowed area lights | ✔ plan status lines |
| 4.2 | `VR_PLAN.md` | Phase 1 coded and builds, untested at runtime; Phases 2-7 (stereo, Game View preview, lens distortion, headset output, calibration, hardening) | ◐ note + plan |
| 4.3 | `BEAM_EDITOR_PLAN.md` | Phases 0-11 (upstream editor core, never lose work, syntax, search, engine links, C# build errors inline, Roslyn spike, Vim mode, local history) | ◐ note says "start Phase 0"; Roslyn needs .NET only, no new install |
| 4.4 | `MATERIAL_EDITOR_PLAN.md` | Phases 4+ (advanced lobes, material instances, triplanar, node graph, graph-to-shader, node library) | ◐ Phases 1-3 visible in the code (Material Editor, thumbnails, Lux Standard inputs) |
| 4.5 | `BINDLESS_PLAN.md` | One resource heap, shared geometry pool, multi-draw per pass, shadows and everything else through the heap | ◐ status not recorded in the plan |
| 4.6 | `AUDIO_VISUALISATION_PLAN.md` | Occlusion visualisation, material view, rooms/reverb showcase | ◐ status not recorded in the plan |
| 4.7 | Quality category system | Per-category quality tiers: serialization and UI still pending | ◐ note |
| 4.8 | Multi-threading Phase 4 | The asset-upload race | ◐ note |
| 4.9 | Panel-state memory layer | Unity-like per-panel state on top of `imgui.ini` (never remove ImGui) | ◐ your queued goal |
| 4.10 | NRD denoiser | Signal wiring left; **Decision:** NRD is temporal by design, so it conflicts with the standing rule unless used only in a non-temporal mode | ◐ note |
| 4.11 | Async compute | Scaffolded and serialized; GTAO/SSR not on compute | ◐ note |
| 4.12 | Retained render scene | `FlushDrawList` ~1.4 ms and the moving-shadow +0.2 ms remain | ◐ note |
| 4.13 | Golden settle residual | Lighting penumbra not bit-stable after a hitch (60 px, max 1) | ◐ note |
| 4.14 | Vercidium EULA clause 5.1(d) | Resolve before audio work ships in an engine release | ◐ note — **Decision** (legal) |

## 5. Known gaps written into the architecture doc

| # | Gap | Size | Source |
|---|---|---|---|
| 5.1 | Physics ignores switched-off entities and components; colliders have no `Enabled` (needs Jolt add/remove during Play) | M | ✔ Architecture § 2.4 "Not done yet" |
| 5.2 | No C# `Entity.Active` and no `Enabled` on C# components | S | ✔ same section; it was your call to leave it out then |
| 5.3 | `NativeScriptComponent` is not covered by the switch-off rules | S | ✔ same section |
| 5.4 | The simulation thread is scaffolded but nothing constructs it | XL | ✔ `Threading.md` |
| 5.5 | Linux runs single-threaded by default (render-thread races with Wayland/Vulkan swapchain) | L | ✔ `Threading.md` |
| 5.6 | Accessibility captions are not shown in runtime VR; no C# `Lux.VR` API | M | ◐ `VR_PLAN.md` follow-ups |
| 5.7 | Wide lines rely on a fork patch to NRI; quad-expanded lines are the portable follow-up | M | ◐ `NRI_MIGRATION_PLAN.md` D15 |
| 5.8 | Per-submesh acoustic materials (one acoustic geometry per entity today) | M | ◐ `AUDIO_VISUALISATION_PLAN.md` |
| 5.9 | `KHR_materials_diffuse_transmission` import (the vendored Assimp lacks it) and a transmission colour texture | M | ✔ Architecture § 2.3 |
| 5.10 | Discord Social SDK runtime is unverified | S | ◐ note |

## 6. Bigger features worth a plan of their own

| # | Feature | Why | Size |
|---|---|---|---|
| 6.1 | Skeletal animation (3.19) and an animation graph editor | Characters | XL |
| 6.2 | Material node graph (Material Editor plan Phases 8-12) | Artist workflow | XL |
| 6.3 | UI system for games (screen-space layout, buttons, text input) beyond the 2D `Text` component | Menus and HUDs without custom code | XL |
| 6.4 | Particle system | Effects | XL |
| 6.5 | Navigation meshes and path finding | AI movement | L |
| 6.6 | Input actions (named actions bound to keys and gamepad, rebindable) over the raw `Input` API | Rebindable controls | M |
| 6.7 | Save-game API for scripts | Persisting player progress | M |
| 6.8 | Scene streaming / additive scenes | Larger worlds | XL |
| 6.9 | A runtime path tracer or reference renderer | Lookdev and validation — **Decision:** this is ray tracing, which restarts from scratch by your rule | XL |

---

## Suggested order

1. Section 1 (verify the merge). **2.1** and **2.2** are done.
2. Decisions: **3.1** fixed-step semantics, **3.10** prefab rules, **4.10** NRD, **4.14** EULA.
3. **3.4** geometry cache (pays back the LOD import cost) and **3.5** asset-pack validation.
4. Then pick the next large plan: VR (4.2), Beam (4.3), Material graph (6.2) or Animation (6.1).
