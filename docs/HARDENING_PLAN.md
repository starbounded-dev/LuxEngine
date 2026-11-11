# LuxEngine Hardening Plan

Written 2026-11-09 on `feature/native-audio-system`. This plan proves, by running them, the features
that were written and built but never driven in the editor or in Play. It is item **M1** of
`docs/FEATURE_CATALOG_2027.md`. It is a planning document; what is built is described in
`.claude/docs/Architecture-LuxEngine.md`.

**Goal card**

- **Goal:** every feature the backlog marks "builds, not run" has been run, and either works or has
  a filed bug.
- **Success:** one table (Part 4) with a dated result per feature; every check that can be a script
  is a script that a later session can re-run; `docs/NEXT_UP_BACKLOG.md` no longer says "not run"
  anywhere.
- **Non-goals:** new features; performance work; Linux (catalog M12 re-runs these same scripts
  there); AMD/Intel GPUs (M9); VR; Discord.
- **Constraints:** no temporal rendering; no editor launch without your OK (see decision H1); the
  main project and your preferences must not be touched by a test; Windows only for now.

**Decisions this plan is built on** (H1 and H3 are yours; the rest are recorded reasoning)

| # | Decision | Choice | Consequence |
|---|---|---|---|
| H1 | May scripts launch the editor? | **Proposed: yes, for runners that set `LUX_USER_DATA_DIR` and use their own project**, plus `golden_run.py` on the sample project | Without it only Phase 5 (your hands) can run. The isolated runners cannot open the main project or write your preferences. `golden_run.py` does open `LuxSample.luxproj`: the backlog recorded `AssetRegistry.lzr` untouched after 19 such launches, and each sweep ends with `git status` on the sample |
| H2 | Where do Play checks live? | In C# scripts inside a small test project, reporting PASS/FAIL to the log | No new engine system; the same project is exported in Phase 4 to test the runtime |
| H3 | What happens to a feature that fails? | **Proposed:** fix it in this pass if the fix is small or a day; otherwise file it in the backlog and hide the feature from the release UI | Keeps the pass from turning into feature work |
| H4 | New golden reference | Capture `hp0` at the start; you look at the images once and accept or reject | Later sweeps compare to `hp0` |
| H5 | Web research | Skipped | The work is determined by this repository's own code and tools |

---

## Part 0 — Where we are

Read in the source on 2026-11-09. "Built" means the symbol is in the tree; nothing here was run for
this plan.

### The features to prove

| Feature | State | Evidence | Closed by |
|---|---|---|---|
| Material graph: canvas, 46 node types, functions, previews | ⚠️ built; codegen was checked with glslc; an editor self-test now covers saves, undo and graph scopes | `Renderer/MaterialGraph/MaterialGraph.h` — `MaterialGraph`, `CompileResult`; `Panels/MaterialEditor/MaterialEditorPanelTests.cpp`; commit `6db06b40` | Phase 0, 3, 5 |
| Material instances (parent + overrides) | ⚠️ built; the same self-test mentions instances | `Renderer/MaterialAsset.h` — `SetParent`, `GetOverrides` | Phase 0, 3 |
| Clearcoat, sheen, subsurface, anisotropy, triplanar, detail maps | ⚠️ built; no test mentions them | `MaterialAsset.h` — `Clearcoat`, `ClearcoatRoughness` | Phase 3 |
| Prefab structural sync, removal/re-parent records, Restore | ⚠️ built; hierarchy and settings self-tests mention prefabs; the 24-check headless harness is not in the repository | `Scene/Scene.h` — `SyncPrefabInstance` | Phase 0, 2, 5 |
| `OnFixedUpdate`, one Jolt step at a time | ⚠️ built | `Scene.h` — `InvokeScriptFixedUpdate`; `Physics/PhysicsScene.h` — `AdvanceFixedClock` | Phase 2 |
| Character controller on `JPH::CharacterVirtual` | ⚠️ built; its 15-scenario harness is not in the repository | `Physics/JoltPhysics/JoltCharacterController.cpp` | Phase 2, 5 |
| C# `OnCollisionBegin/End`, `OnTriggerBegin/End` in 3D | ⚠️ built; touched since (`5c06ae5b`) | `Physics/ContactPairTracker.h`; `ScriptCore/Source/Lux/Scene/Entity.cs` | Phase 2 |
| C# `Gizmos`, `Entity.Instantiate(Prefab)`, deferred `Entity.Destroy` | ⚠️ built | `Scene.h` — `AddDebugLine`, `SubmitToDestroyEntity`; `ScriptCore/Source/Lux/Asset.cs` | Phase 2 |
| Named layouts and per-panel state | ⚠️ built; the workspace self-test mentions layouts and panel state | `Editor/EditorPanel.h` — `SaveState`, `LoadState`; `Workspace/WorkspaceSelfTests.cpp` | Phase 0, 5 |
| Quality per category | ⚠️ built; no test mentions it | `Project/Project.h` — `QualityCategories` | Phase 1, 5 |
| Asset-upload ordering fix | ⚠️ built | `Asset/AssetManager/EditorAssetManager.h`, `RuntimeAssetManager.h` — `SyncWithAssetThread` | Phase 1 |
| Dist Editor with the stamped shader pack | ⚠️ starts without shader errors; the viewport was never looked at | commits `eed580fd`, `b66511e0`; your note of 2026-11-09 | Phase 1 |
| Editor tool host | ⚠️ headless tests passed in October; harness not in the repository | `Editor/EditorToolHost.h` — `EditorToolHost` | Phase 5 (one undo check) |
| Backlog 1.3, 1.4, 1.5 (40 sprite textures and fonts; font cache; `SharedMeshNodes.gltf`) | ⚠️ never driven | `tests/meshes/SharedMeshNodes.gltf` | Phase 5 |
| Area-light shadows, audio, Help panel | ✅ working, confirmed by you 2026-11-09 | — | out of scope |

### The tools that already exist

| Tool | What it does | Evidence |
|---|---|---|
| Editor self-test runners | Launch the Debug editor on a throw-away project with its own user-data folder, under both threading policies; the editor runs a built-in test and writes a `PASS` status file; the log is scanned for validation errors and asserts | `tests/editor/run_*.py` (13 runners), `native_checks.py` — `require_clean_graphics_log`; env `LUX_USER_DATA_DIR`, `LUX_*_SELFTEST` |
| Golden capture | Launches the editor on chosen scenes, captures an image and frame timings, can enter Play, can export, can run Debug with sync validation, can force a threading policy, compares two labelled sets | `tests/rendering/golden_run.py` — `--features`, `--play`, `--export-dir`, `--config`, `--threading`, `--sync-validation`, `--compare`; `Editor/Source/Tools/GoldenCapture.cpp` |
| Threading A/B | Captures each scene under both policies and fails if the images differ | `tests/rendering/threading_ab.py` |
| Export round trip | Exports saved panel fixtures and runs the packaged player | `tests/editor/run_remaster_export.py` |
| Script build in a test | Builds a C# project with `dotnet` | `tests/editor/run_script_templates.py` |
| Headless suites | Audio (ctest), managed audio bindings, shadow shader, runtime resources, YAML oracle | `tests/audio`, `tests/rendering/run_shadow_shader.py`, `tests/runtime/run_resources.py`, `tests/yaml/run.py` |

**What is missing:** nothing runs a scene in Play and checks gameplay behaviour (the Play mode of
golden capture verifies audio only), and no golden scene shows the new material features. CI runs
the audio tests only (`.github/workflows/main.yml`); none of the editor runners or golden sweeps.

---

## Part 1 — Design

Three kinds of check, cheapest first. A feature gets the cheapest kind that can judge it.

1. **Script decides.** A runner launches the editor or the player, the program reports PASS or FAIL,
   the log must be clean. Used for behaviour: physics, callbacks, saves, sync.
2. **Image decides.** A golden capture, compared with a reference you accepted once. Used for
   rendering: material lobes, quality tiers, the Dist editor, both threading policies.
3. **You decide.** A written checklist for what only hands and eyes can judge: how a canvas feels,
   whether a menu is where you expect it.

New content, all test-side:

- `tests/play/` — **NEW**: a small Lux project (scene, prefab, C# scripts) and `run_play.py`.
  The scripts assert in `OnFixedUpdate`/`OnUpdate` and log `PLAYTEST PASS <name>` or
  `PLAYTEST FAIL <name>: <reason>`. Runs on the main thread like any script; owns nothing.
- `Editor/LuxSampleProject/Assets/Scenes/MaterialShowcase.luxscene` and its `.lmat` files — **NEW**:
  one sphere per feature. Text files, written by hand, which also tests that such files load.
- At most one small engine hook, only if the Phase 2 spike shows golden capture's Play mode cannot
  carry the test (it waits for audio verification): a `LUX_PLAY_SELFTEST` variable that follows the
  existing `LUX_*_SELFTEST` pattern in `EditorLayer.cpp`. Compiled out of Dist like the others.

No serialization change, no new dependency, no new thread, no renderer change. The shared docs gain
one section: how to run the hardening suites (`Building.md` or `Rendering.md § Golden image capture`,
wherever the golden instructions already live).

---

## Part 2 — Phases

### Phase 0 — Baseline: run everything that already exists

**Goal:** know what passes today, before writing anything.

**Changes:** none to the engine. Results go into Part 4.

**Verification**
- Build: Debug, Release and Dist of `Lux.sln` at the branch head (`MSBuild Lux.sln /restore /m`),
  in the background; the `bin/` folders exist but may be older than the head.
- Run, in this order: the headless suites; then the 13 `tests/editor/run_*.py` runners; then
  `run_remaster_export.py`.
- Log: each runner prints `PASS` and its evidence folder.

**Exit criteria:** every suite has a row in Part 4: pass, or fail with the log line.
**Rollback:** nothing to roll back.

### Phase 1 — Rendering sweeps with the golden tools

**Goal:** a new reference, and four open items closed by image.

**Changes:** none.

**Verification**
- `golden_run.py --label hp0 --features` (Release). You look at the images once (H4).
- `golden_run.py --config debug --sync-validation --label hp0-debug`: zero validation errors, zero
  sync hazards.
- `threading_ab.py --no-build`: identical images under both policies. This is the check for the
  asset-upload ordering fix.
- `golden_run.py --config dist --label hp0-dist`, then `--compare hp0 hp0-dist`: the Dist editor's
  viewport matches Release. This is the check your note asked for.
- Quality tiers: two captures of `Benchmark` with every category at the lowest and at the highest
  tier (through `--options`, or a copy of the project file with `QualityCategories` changed). They
  must differ from each other, load without warnings, and the highest must match `hp0` if the sample
  is saved at that tier.
- After the sweeps: `git status Editor/LuxSampleProject` shows no change.

**Exit criteria:** `hp0` accepted; the four items have a result.
**Rollback:** delete `bin/golden/hp0*`.

### Phase 2 — Play test project

**Goal:** gameplay behaviour is checked by a script, in the editor's Play mode.

**Spike first (an hour):** can `golden_run.py --project tests/play/... --play --frame N` run a
project that has no audio, for N frames, and do `Log` calls from C# reach the captured log? If yes,
no engine change. If no, add the `LUX_PLAY_SELFTEST` hook described in Part 1.

**Changes**
- `tests/play/PlayTest.luxproj`, `Assets/Scenes/PlayTest.luxscene`, `Assets/Prefabs/Crate.lprefab`,
  `Assets/Scripts/Source/*.cs` — **NEW**.
- `tests/play/run_play.py` — **NEW**: copies the project to a temp folder, builds its scripts with
  `dotnet` (as `run_script_templates.py` does), launches under both threading policies, fails on any
  `PLAYTEST FAIL`, on a missing expected `PLAYTEST PASS`, or on `require_clean_graphics_log`.

**The checks, one script each**

| Name | Asserts |
|---|---|
| `FixedStep` | `OnFixedUpdate` is called with the fixed step; over 2 s the count is within one of 2 s ÷ step; it runs before the physics step (a force applied there moves the body the same step) |
| `FallAndLand` | A dynamic box dropped from 5 m reaches the floor and stops; it does not pass through |
| `Character` | A character controller lands, reports grounded, walks up a 30° ramp, does not climb a 60° one, is carried by a moving kinematic platform |
| `Contacts` | Two boxes colliding give exactly one `OnCollisionBegin` and, after separation, one `OnCollisionEnd`, on both scripts |
| `Triggers` | A body passing through a trigger gives one `OnTriggerBegin` and one `OnTriggerEnd`; so does a character controller |
| `Spawn` | `Entity.Instantiate(prefab)` at a position puts the root and its children there; its script's `OnCreate` ran; its body collides |
| `Destroy` | `Destroy()` on itself inside `OnUpdate` fires `OnDestroy` once, and the entity is gone next frame, with no crash |
| `Gizmos` | `Gizmos.DrawLine/DrawWireBox/DrawWireSphere` every frame for 2 s: no error, no growth in the log (visual check is in Phase 5) |
| `PrefabSync` | The scene's instance of `Crate` has a child the prefab does not (kept), lacks one it deleted (stays deleted), and has the child the prefab added after the instance was saved |

**Thread and lifetime:** scripts run on the main thread; nothing new is owned.

**Verification**
- Build: no project regeneration unless the hook is needed (then `scripts\Win-GenProjects.bat` is
  still not needed: no new source file, the hook goes in `EditorLayer.cpp`).
- Run: `python tests/play/run_play.py`; nine PASS lines under each threading policy.

**Docs:** the "how to run" section named in Part 1.
**Exit criteria:** the runner passes, or each failing check is a filed bug with its log.
**Rollback:** delete `tests/play/`; revert the hook commit if there is one.

### Phase 3 — Material showcase scene

**Goal:** every material feature is in one golden image.

**Changes**
- `Editor/LuxSampleProject/Assets/Scenes/MaterialShowcase.luxscene` and
  `Assets/Materials/Showcase/*.lmat` — **NEW**: spheres in a row under one sun and one sky light:
  standard, clearcoat, sheen, subsurface, anisotropy, triplanar, detail map, a graph material (noise
  into colour, exposed parameter), an instance of it overriding that parameter, a material function,
  a cutout graph (opacity) casting a shadow.
- `tests/rendering/golden_run.py` — `DEFAULT_SCENES`: add `MaterialShowcase`.

**Verification**
- Run: `golden_run.py --scenes MaterialShowcase --label hp3`, and the same with
  `--config debug --sync-validation`.
- Look: each sphere differs from the standard one in the way its feature says; the instance differs
  from its parent only by the overridden parameter; the cutout's shadow has holes. You accept the
  image.
- Log: no shader compile error, no validation error.
- `git status` afterwards: only the new files; `AssetRegistry.lzr` changes are reviewed, not
  committed blindly.

**Exit criteria:** image accepted, or a bug filed per wrong sphere.
**Rollback:** delete the scene and materials; revert the one-line `DEFAULT_SCENES` change.

### Phase 4 — The same checks in the exported game

**Goal:** what passed in the editor also passes in `Lux-Runtime` (Dist), from an asset pack.

**Changes**
- `tests/play/run_play.py` — an `--export` mode: export the Play test project through
  `golden_run.py --export-dir` (as `run_remaster_export.py` does), start the packaged player, read
  the same PASS/FAIL lines from its log.

**Open point to settle first:** how the player ends by itself and where its log goes.
`RuntimeLayer.cpp` already takes `LUX_RUNTIME_AUDIO_REPORT`; the lazy route is a C# script that
quits after the checks, if a quit call exists in the C# API. If none exists, that is a real gap
(catalog SC6) and gets a small internal call rather than a test-only hack.

**Verification:** nine PASS lines from the Dist player; `MaterialShowcase` exported and captured
or, if the player has no capture path, started and its log clean.

**Exit criteria:** runtime results in Part 4.
**Rollback:** remove the `--export` mode.

### Phase 5 — Hands and eyes (yours, about an hour)

**Goal:** the things no script can judge.

A checklist, written into this file as Part 4b when the phase starts, each line a single action and
what must be seen. It covers:

- Material graph canvas: add a node from the search palette, connect, comment, copy/paste, undo,
  minimap, find, collapse to a function, a node error shown on the node.
- Material instance: Create Instance from the toolbar and from the Content Browser; Reset on an
  override.
- Prefabs: the overrides UI on an instance, revert/apply, Restore on a converted old scene.
- View → Layouts: save, switch, delete; restart and the layout and each panel's state are back.
- Scene Renderer → Quality per category: pick a tier, edit an owned value, see the category go to
  Custom.
- Play: walk the character in the `Physics` scene; `Gizmos` lines are visible and do not flicker.
- Tool host: one scripted edit is one undo step.
- Backlog 1.3, 1.4, 1.5.
- ImGui: no ID-conflict popup at any point.

**Exit criteria:** each line ticked or turned into a bug.

### Phase 6 — Fix, record, keep

**Goal:** the pass leaves the engine better and the checks alive.

**Changes**
- Each bug from Phases 0–5, by rule H3, one at a time through `/dev` and `/cr`.
- `docs/NEXT_UP_BACKLOG.md` — every "not run", "not yet tried" and "untested" line replaced by its
  dated result.
- `.github/workflows/main.yml` — the editor runners and `run_play.py` added to the self-hosted
  Windows job (it has a GPU; the hosted runners do not). Golden sweeps stay manual: they need an
  accepted reference.
- `docs/FEATURE_CATALOG_2027.md` — M1 marked done; FINISH rows updated.

**Exit criteria:** Part 4 has no empty cell; CI runs the runners.

---

## Part 3 — Risks

| Risk | Likelihood | Caught by |
|---|---|---|
| The binaries in `bin/` are stale and results describe old code | High | Phase 0 builds first |
| A golden sweep changes `LuxSample.luxproj` or the registry (the editor saves renderer settings into the project) | Medium | `git status` after every sweep; never commit those files as part of this pass |
| Hand-written scene or material files do not load | Medium | That is a finding; fall back to authoring them in the editor in Phase 5 |
| Golden capture's Play mode cannot run a project without audio | Medium | Phase 2 spike; the fallback hook is small |
| A Play check is flaky because of frame timing | Medium | Checks assert on fixed steps and counts, never on wall-clock time; both threading policies must agree |
| The self-hosted runner cannot run the editor (no desktop session, missing .NET SDK component) | Medium | Phase 6 tries one runner first; if it fails, the runners stay a local pre-release step |
| A failure turns out to be a design problem, not a bug | Low–medium | Rule H3: file it, hide the feature, move on |

**What would invalidate the plan:** if Phase 0 shows the existing runners themselves do not pass on
this branch. Then the first job is repairing the test tools, and Phases 1–4 wait.

## Part 4 — Results

Filled in as phases run. One row per suite or feature: date, build, result, evidence folder or bug.

### Phase 0 — baseline, 2026-11-09, commit `5a5a31a6` (last source change), Windows, RTX 4070 Ti

**Builds**

| Build | Result | Note |
|---|---|---|
| `Lux.sln` Debug | ✅ 0 errors | With the Visual Studio 2026 MSBuild |
| `Lux.sln` Release | ✅ already current | Built 21:37 by another session, after the last source commit |
| `Lux.sln` Dist | ✅ 0 errors | Builds `Lux-Runtime`; the solution leaves `Editor` out of Dist on purpose |
| Dist `Editor.exe` | ⚠️ stale (14:36, before the shader-pack commits) | Needs its own build: finding F2 |
| Same builds with the VS 2022 Build Tools MSBuild | ❌ `MSB4236: The SDK 'Microsoft.NET.Sdk' specified could not be found` on `ScriptCore` and `Coral.Managed` | Finding F1 |

**Headless suites**

| Check | Result | Evidence or reason |
|---|---|---|
| Native audio (`ctest`, 2 tests) | ✅ pass | `bin/native-audio-tests` |
| `tests/audio/run_soundwave.py` | ✅ pass | needs an MSVC developer environment |
| `tests/audio/run_occlusion_windows.py` | ✅ pass | same |
| `tests/audio/run_managed.py` | ✅ pass | |
| `tests/rendering/run_shadow_shader.py` | ✅ pass | |
| `tests/editor/test_native_checks.py` | ✅ pass | |
| `tests/yaml/run.py` | ❌ 33 problems in 215 files | Not a parser fault: 26 files are new and 7 changed since the reference was recorded on 2026-11-03. Finding F3 |
| `tests/runtime/run_resources.py` | — not applicable | Reads the Linux `Lux-Runtime/Makefile`; belongs to the Linux pass (M12) |
| `tests/audio/run.py` | — not applicable | Linux only (clang) |

**Editor self-tests** (Debug editor, throw-away project, Single and Multi threading; each requires a
clean log: no validation error, no assert)

| Runner | Result | What it asserts (from its PASS line) |
|---|---|---|
| `run_workspace_smoke.py` | ✅ both policies | workspace lifecycle, persistence, recovery, concurrent saves |
| `run_workspace_documents.py` | ✅ both | dirty documents, workspace switches, restored saves |
| `run_workspaces.py` | ✅ | adaptive docking, 20 size round trips (compiled test host) |
| `run_panel_controls.py` | ✅ both | panel sizing; Beam save and recovery, Unicode paths, tabs, search |
| `run_browser_panels.py` | ✅ both | Content Browser reflow, navigation, pickers, rename undo |
| `run_hierarchy_panels.py` | ✅ both | hierarchy and inspector, multi-edit, component search, persistence |
| `run_settings_panels.py` | ✅ both | settings and renderer panels, save recovery, Help, Game View, overlays |
| `run_material_panels.py` | ✅ both (225 s) | material saves, retry, reload, undo, graph scopes |
| `run_diagnostic_controls.py` | ✅ both | Log filtering, asset metadata, History navigation |
| `run_diagnostic_plots.py` | ✅ both | Profiler plots |
| `run_audio_panels.py` | ✅ both | audio authoring, save/undo/pack, preview |
| `run_project_templates.py` | ✅ | project templates (compiled test host) |
| `run_script_templates.py` | ✅ | script templates compile against ScriptCore in Debug, Release and Dist |
| `run_remaster_export.py` | ✅ (184 s) | a saved graph material, a Sound and the Mixer exported; the Dist player ran; 0 validation ids |

The sample project was not changed by any run (`git status Editor/LuxSampleProject` identical before
and after).

**What Phase 0 settles for the feature list in Part 0**

| Feature | After Phase 0 |
|---|---|
| Named layouts and panel state | ✅ run. `WorkspaceSelfTests.cpp` asserts selection, the menu item, persistence, legacy conversion and a rejected invalid store. Only the look of the menu is left for Phase 5 |
| Material graph: save, revert, undo, pack round trip, live canvas with preview | ✅ run (`MaterialEditorPanelTests.cpp`), and the graph material survives export to the Dist player |
| Material instances: parent, `UpdateOverrides`, shared graph | ✅ run, same test |
| Clearcoat, sheen, subsurface, anisotropy, triplanar, detail maps | still unproven: no test mentions them. Phase 3 |
| Prefab structural sync | still unproven: the tests use the prefab editor's selection only, never `SyncPrefabInstance`. Phase 2 |
| Everything in Play (fixed update, character, contacts, triggers, spawn, destroy, gizmos) | still unproven. Phase 2 |
| Quality per category, asset-upload ordering, Dist editor viewport | still unproven. Phase 1 |

**Findings**

| # | Finding | Size | Goes to |
|---|---|---|---|
| F1 | The VS 2022 Build Tools MSBuild cannot build the C# projects (.NET SDK resolver missing). Same fault your note records for the self-hosted CI runner. Local builds work with the VS 2026 MSBuild | S | Fix the Build Tools install (add the .NET SDK component), or CI stays red on Windows |
| F2 | The `Editor.exe` in the Dist folder is older than the Dist shader-pack commits. `scripts/Lux-BuildTest.ps1` already says "an old Editor.exe is not Dist evidence". Build `Editor/Editor.vcxproj` in Dist before Phase 1 | S | Phase 1, first step |
| F3 | The YAML oracle's reference is 6 days stale, so the suite fails on any asset change. With yaml-cpp gone it can only be re-recorded from `Lux::Yaml` itself; it is now a change detector for sample assets. Re-record it once your staged sample changes are committed, or retire it | S | Phase 6; your call |
| F4 | `tests/editor/run_workspaces.py` reads `env["Path"]`; from Git Bash the key is `PATH` and it raises `KeyError` | S | Phase 6 |
| F5 | Five suites need an MSVC developer environment and say so only when they fail. A wrapper script that sets it up and runs every suite would make the baseline one command | S | Phase 6 |
| F6 | Catalog rows marked "?" that these tests answer: multi-entity editing exists (E3), a prefab editor context exists (E11), Beam already has save and recovery (part of E7/M5) | — | Catalog update in Phase 6 |

### Phase 1 — rendering sweeps, 2026-11-09, Windows, RTX 4070 Ti

All captures used an isolated user-data folder. `LuxSample.luxproj`, `AssetRegistry.lzr` and
`Editor/imgui.ini` were byte-identical before and after every sweep.

| Check | Result | Evidence |
|---|---|---|
| Release reference `hp0`: 5 scenes + 17 feature variants | ✅ 22/22 captured, 0 validation ids. **Not yet accepted by you (H4)** | `bin/golden/hp0`, PNGs in `bin/golden/hp0/png` |
| Four more scenes (RenderFeatures, ManyLights, LowSun, Mirroring) | ✅ captured | `bin/golden/hp0x` |
| Debug with sync validation, 22 + 4 captures | ✅ 0 validation ids, 0 sync hazards; images match Release 22/22 | `hp0-debug`, `hp0x-debug` |
| Threading A/B (asset-upload ordering fix) | ✅ 5/5 identical under Single and Multi | `bin/golden/ab-*` |
| Dist editor viewport against Release | ✅ since the F7 fix (2026-11-10): 9 of 9 scenes identical to the Release reference. Before it: 8 of 9 differed by 33–82% of pixels, sky and text only, **no meshes** | `f7-dist`; before: `hp0-dist`, `hp1-dist` |
| Quality tiers 0–4 on Benchmark (Release), recaptured 2026-11-10 after the F8 fix | ✅ every tier differs from the next (25.7%, 13.1%, 3.6%, 3.0% of pixels) and GPU median rises with the tier (0.89 → 1.94 → 2.08 → 2.67 → 2.94 ms). Low: hard shadows, no reflections, no AO; Cinematic: soft shadows, floor reflections, contact AO. **For your eyes:** `bin/golden/f8-png` | `f8-q0` … `f8-q4` |
| Quality tiers in Debug with sync validation, after the F8 fix | ✅ Low and Cinematic run with no assert and 0 validation ids (before the fix: assert `Could not find uniform 'u_AOSettings.BentNormals' on material 'GTAO-Composite'`) | `f8-low-debug`, `f8-q4-debug` |
| Stale-cache reproduction after the fix: a Low run, then a default run | ✅ the default run matches `hp0` exactly, in Release and in Debug | `f8-low` → `f8-b`; `f8-low-debug` → `f8-c-debug` |

**Change made in this phase:** `Editor/Source/Tools/GoldenCapture.cpp` gained the option `Quality=T`
(0 Low to 4 Cinematic), which calls `SceneRenderer::ApplyQualityPreset`; documented in
`.claude/docs/Rendering.md`. Tiers cannot be driven any other way: `--options` had no tier option,
and the project file stores tiers as labels only.

**Findings**

| # | Finding | Size | Status |
|---|---|---|---|
| F7 | **The Dist editor cannot show meshes.** `Core/premake5.lua` removes `AssimpMeshImporter.cpp` from Dist, and `MeshSourceSerializer::TryLoadData` returns an error under `LUX_DIST`. Right for the exported game (it loads packs), wrong for an editor. The Dist session log was not read (pruned); the cause is from the source and the images. Decision needed: ship the Release editor in the zip, or give the Dist editor the importer (a build flag separate from "is this the player") | M–L | **Fixed 2026-11-10.** The importer is compiled into `Core` in every config and `MeshSourceSerializer` calls it through `SetSourceImporter`, which the editor registers at startup and the player does not. Result: the Dist editor matches the Release reference exactly on all 9 scenes (`f7-dist`); Release (9 scenes) and Debug (Benchmark, sync validation) are unchanged; the Dist and Release players no longer contain the Assimp import, and the export round trip with the Dist player passes. Lucky compiles and links Assimp everywhere and calls it directly; that would put the Assimp DLL in every exported game, so it was not copied. **Still true of the Dist editor:** it has no shader compiler, so it cannot compile a new or edited material graph or hot-reload a shader; it renders what the checked-in shader pack holds |
| F8 | **The editor's shader cache ignores global macro values.** A session that ends with GTAO off leaves the no-GTAO `AO-Composite` in `Resources/Cache/Shader`. The next session wants GTAO, sees its macro value "unchanged" in memory (`Renderer::SetGlobalMacroInShaders` returns early), and never recompiles: **AO is silently missing in Release** (1–2% of pixels on Benchmark and AudioDemo) and Debug asserts. Reproduced: `Quality=0` run, then a default run. Proven: deleting the three cached AO shaders restored an exact match with `hp0`. Reachable by a user who lowers AO quality and reopens a project that has it on. Fix: record the macro values a cached shader was built with (`VulkanShaderCache`, as the shader-pack stamp already does) and recompile on mismatch | M | **Fixed 2026-11-10**, smaller than planned: `VulkanShaderCompiler::PreProcessGLSL` hashed the stage's raw text; it now hashes the preprocessed source, as the HLSL path already did, so macro values are part of the hash. Verified by the reproduction above. The first launch after this change recompiles every GLSL shader once |
| F9 | A two-line guard in `SceneRenderer::AOComposite` made the Debug assert go away; it was reverted, because the assert is the only alarm for F8 | S | Closed: with F8 fixed the assert no longer fires, so there was no transition window to guard. Debug and Release were rebuilt without it on 2026-11-10. **The Dist binaries from 2026-11-09 22:54 still contain the guard and lack the F8 fix: rebuild Dist before using them** |
| F10 | Golden runs share `Editor/Resources/Cache` with your own editor, so a run that changes macros can leave state behind (this is how F8 was found) | S | Harmless since the F8 fix: a leftover variant is detected and recompiled |

**Left in Phase 1:** your look at the `hp0` and tier images.

### Phase 2 — Play checks, 2026-11-10, Debug editor, Windows

Spike result: golden capture's Play mode could not carry the test (it requires a passing Soundwave
verification), so the planned hook was added: `LUX_PLAY_SELFTEST=<frames>` in
`EditorLayer::OnAttach`/`OnUpdate`, compiled out of Dist. `tests/play/run_play.py` writes the
project, builds the scripts and judges the log.

**Result: 12 of 12 checks pass under Single and under Multi** (`bin/play-tests/1794360728487005300`).

| Check | What it proves |
|---|---|
| FixedStep | `OnFixedUpdate` runs at 1/60 s; step count matches frame time within three steps over 2 s |
| FixedBeforeStep | `OnFixedUpdate` runs **before** the physics step: a velocity set there moves the body in that step |
| FallAndLand | a box dropped from 5 m comes to rest on the floor at y = 0.5 |
| Character | `CharacterVirtual` lands, reports grounded, walks 4 m+ in 2.5 s, jumps over 0.5 m and lands grounded |
| SlopeClimb / SlopeBlock | climbs a 30° ramp (rises over 1 m), cannot climb a 60° one (slope limit 45°) |
| Contacts | exactly one `OnCollisionBegin` and one `OnCollisionEnd` on **both** bodies of a pair |
| Triggers | one `OnTriggerBegin`/`End` for a body and for a character controller |
| Spawn | `Entity.Instantiate(prefab, position)` from a script field: right position, 3 children, `OnCreate` already run, the body lands |
| Destroy | `Destroy()` on itself inside `OnUpdate`: `OnDestroy` once, entity gone, no crash |
| Gizmos | `DrawLine/Ray/WireBox/WireSphere` every frame and every step for 2 s, no error (visual check left to Phase 5) |
| PrefabSync | an instance saved before the prefab gained a child: kept child, recorded deletion and instance-added child all as expected after scene open |

**Findings**

| # | Finding | Size | Status |
|---|---|---|---|
| F11 | **`Entity.Instantiate` crashed the editor every time the prefab had a script**: fatal .NET error "attempted to call a UnmanagedCallersOnly method from managed code", process exit `0x80131506`. The internal call was declared with the managed calling convention, so the runtime refused the native side's callback into C# (`OnCreate`). Isolated with `--only Spawner` | S | **Fixed**: `Scene_InstantiatePrefab` is `delegate* unmanaged<...>` (`ScriptCore/Source/Lux/InternalCalls.cs`); rule added to Architecture Part 4 "Add a new C# internal call". No other internal call runs C# before it returns |
| F12 | C# `Vector3` has no `Length()` and no subtraction | S | Catalog SC2; the tests use a local helper |
| F13 | Not covered yet: a character carried by a moving kinematic platform; the Box2D side; runtime (exported player) | — | Platform and Box2D: later checks. Player: Phase 4 |

## Part 5 — Open questions

1. Is golden capture compiled into the Dist editor? `EditorLayer.cpp` has `#ifndef LUX_DIST` blocks
   near it; `golden_run.py` offers `--config dist`. Resolved by the first Dist capture in Phase 1.
2. Does `--options` reach the quality categories, or must the project file be edited? Read
   `GoldenCapture::ParseOptions` at the start of Phase 1.
3. Is there a C# call to quit the game? Grep `InternalCalls.cs` at the start of Phase 4.
4. ~~How much of layouts, instances and prefabs the existing self-tests really assert.~~ Answered in
   Phase 0 (Part 4): layouts and instances are asserted; prefab sync is not.
5. How is the Dist editor built and stamped with its shader pack? The solution excludes it. Read
   `scripts/Lux-BuildTest.ps1`, `Editor/premake5.lua` and the release workflow before Phase 1.

## Part 6 — Research notes

None. Skipped on purpose (decision H5).
