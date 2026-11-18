# LuxEngine Asset Path References Plan

Planning document for GitHub issue [#161](https://github.com/starbounded-dev/LuxEngine/issues/161):
saved data refers to assets by a path relative to the project's `Assets/` folder instead of a random
`AssetHandle`. This is a **spike plan**: the smallest slice that proves or kills the idea before the
rest of the asset types are converted. What actually exists is described by
`.claude/docs/Architecture-LuxEngine.md § 2.8`; this file is a point-in-time record.

**Goal card**
- **Goal:** a scene's mesh, material, collider and audio references survive a regenerated
  `AssetRegistry.lzr`, a fresh git worktree and a clean clone, because they are stored as
  Assets-relative paths.
- **Success criteria:** in a fresh worktree with the registry deleted, `AudioSponza.luxscene` and
  `AudioDemo.luxscene` load with **zero unresolved references**, and re-saving them produces
  byte-identical files (only path-form references, no handle churn). Observable by a headless test,
  not the editor.
- **Non-goals (for the spike):** textures, materials' own texture refs, prefabs, `.luxproj`,
  asset packs, C# script `StoredFields`, gitignoring the registry, a "Repair references" UI.
- **Constraints:** old projects and scenes must still load; no editor launch without explicit OK;
  Windows only for now; no new dependency; runtime/Dist must keep working from asset packs.
- **Owner's words:** "it should work in a relative location from the asset folder."

## Decisions this plan is built on

| Decision | Choice | Consequence |
|---|---|---|
| Identity in saved files | Assets-relative path, forward slashes | Readable diffs; registry stops being the source of truth |
| Reading | Accept **both** a path string and the old number | Old scenes load unchanged; no migration step needed to *load* |
| Writing | Write the path when the target is a registered file asset; otherwise the number | Memory-only assets keep working; scenes convert on their next save |
| Resolution | Read-only `GetAssetHandleFromFilePath` at load | Safe on the asset worker (`Threading.md`: never mutate the registry from a worker) |
| Registry in git | **Unchanged in the spike** | Decided after the spike, when every reference type is converted |

## Part 0 — Where we are (ledger)

State legend: ✅ read and confirmed in source this session · ⚠️ read, not run · ❌ missing.
Nothing below was verified at runtime.

| Fact | State | Evidence |
|---|---|---|
| Handles are random, minted on first import of a path | ✅ | `Asset/AssetManager/EditorAssetManager.cpp` `ImportAsset`: `metadata.Handle = AssetHandle();` |
| Registry is the only path→handle map; stores `Handle`, `FilePath`, `Type`, sorted by handle | ✅ | `EditorAssetManager::LoadAssetRegistry`, `WriteRegistryToFile`; `Project::GetActiveAssetRegistryPath` → `Assets/AssetRegistry.lzr` |
| Scene file stores handles as bare numbers | ✅ | `Scene/SceneSerializer.cpp`: `StaticMeshComponent.AssetID`, `MeshComponent`/`SubmeshComponent` `AssetID`, `MeshColliderComponent.ColliderAsset` (line ~739), `SerializeMaterialTable`, `SoundBanks` (~1437) |
| A static mesh is written as its **MeshSource file handle**, never a derived handle | ✅ | `SceneSerializer.cpp` `GetSerializableStaticMeshHandle` maps a memory `StaticMesh` to `GetMeshSource()` |
| `ColliderAsset` and `AssetID` already may equal the same file handle | ⚠️ | `AudioSponza.luxscene` uses one value for both |
| Audio refs are `AudioEventRef.Sound` (a `UUID`) written as a number | ✅ | `Audio/AudioEventRef.h`, `Audio/AudioEventRefSerializer.cpp` lines 36/48/58/67 |
| **Precedent for path references exists** | ✅ | `Asset/MaterialSerializer.cpp` `ResolveTexturePath` → `GetAssetHandleFromFilePath` (textures can be a `Path:` node) |
| Handle-writing sites across serializers | ✅ | match counts: Material 54, Mesh 27, Sound 11, Project 9, MeshRuntime 5, Prefab 5, SceneAsset 4, others ≤3 |
| Memory-only assets live in a separate map and have no path | ✅ | Architecture §2.8, `AddMemoryOnlyAsset`/`IsMemoryAsset` |
| Documented rule "Reference assets by `AssetHandle`, never by path after import" | ✅ | Architecture §2.8 Rules — **becomes false, must change** |
| How script fields of type `Sound` are stored | ❌ unknown | scene shows `Type: Sound`, `Data: <number>`, but no `DataType::Sound` found in `Core/Source`; spike item S1 |
| Whether `SceneSerializer::Deserialize` runs on the asset worker | ❌ unknown | `SceneAssetSerializer` exists; spike item S2 decides if lookup misses need deferral |
| Handle mismatch caused the reported breakage | ✅ | working registry `Sponza.gltf` = 730999186646148702 vs committed/scene value 5094136855824747147; same for `AudioDemo.lbank`, `Fart.lsound` |

## Part 1 — Design

One new helper, used at every reference site the spike touches:

- `AssetReference::Write(out, key, handle)` — if `handle` is a registered, file-backed asset, emit
  its `FilePath` (generic, forward slashes) as a string; else emit the number as today.
- `AssetReference::Read(node)` — a **string** scalar → `GetAssetHandleFromFilePath(path)`; a
  **number** → the handle as before. An unresolved path returns 0 and is logged once with the key
  and path (never throws, never imports).

**System/thread:** lives in `Asset/` (Architecture Part 1: Asset depends on Core, Project). Pure
lookup: runs on whichever thread deserializes, takes the registry's shared lock only
(`EditorAssetManager::GetAssetHandleFromFilePath` is `const`). Never mutates the registry
(`Threading.md`). At project open `ProcessDirectory` registers every file before any scene loads, so
existing files always resolve.

**Runtime/Dist:** the pack path is untouched. `RuntimeAssetManager` has no path map in the editor
sense; scenes inside `AssetPack.lap` keep numeric handles (the exporter writes through the same
serializer with a "pack" flag that forces numbers). This is checked in Phase 3, not assumed.

**Ownership, GPU, renderer, ImGui, scripting, Linux:** N/A for the spike — serialization only, no
GPU objects, no UI, no C# API, no platform code (path normalisation is `generic_string`).

**Forward compatibility cost (accepted):** an older engine build reading a path where it expects a
number gets `as<uint64_t>(0)` = 0, i.e. an unassigned reference. Old→new works; new→old does not.

## Part 2 — Phases

### Phase 0 — Reproduce and answer the unknowns (no engine changes)
**Goal:** the failure is a repeatable test, and S1/S2 are settled.
**Changes:** none to engine code. Adds a note to this file with the answers.
- **S1** — find how a script `Sound`/`Material` field is typed and saved
  (`Scripting/ScriptEntityStorage.hpp` `DataType`, `SceneSerializer.cpp` `SerializeStoredField`,
  `ScriptCore/Source/Lux/`). Decide: in or out of scope for the full plan.
- **S2** — find the thread `SceneSerializer::Deserialize` runs on for `OpenScene` and for
  `SceneAssetSerializer`.
- **S3** — reproduce: `git worktree add`, copy project, delete `AssetRegistry.lzr`, load through the
  headless harness (`headless-engine-harness` memory: console exe linking `Core.lib`, no editor) and
  count unresolved handles.
**Verification:** a number: "N of M references unresolved" on the baseline.
**Exit:** S1, S2 answered; baseline N recorded. **Rollback:** delete the worktree.

### Phase 1 — Mesh references as paths (thinnest slice)
**Goal:** `StaticMeshComponent`, `MeshComponent`, `SubmeshComponent`, `MeshColliderComponent` and the
`MaterialTable` write and read Assets-relative paths.
**Changes**
- `Core/Source/Lux/Asset/AssetReference.h/.cpp` — **NEW**: the helper above.
- `Scene/SceneSerializer.cpp` — the sites listed in Part 0 call `AssetReference::Write/Read`.
  `GetDeserializedStaticMeshHandle` keeps its default-primitive fallback for numbers.
- Tests: **NEW** unit test next to the existing serializer tests (path→handle, number→handle,
  unresolved→0, memory-only asset stays numeric).
**Playbook:** new source file → regenerate projects (`scripts\Win-GenProjects.bat`, `Building.md`).
**Thread and lifetime:** lookup only; no ownership change.
**Verification:** build Debug; unit test green; headless load of `AudioSponza.luxscene` with
registry intact **and** with registry deleted → mesh/material/collider all resolve; re-save is
byte-identical on a second save.
**Docs:** none yet (rule in Architecture is rewritten in Phase 4 once the spike passes).
**Exit:** both loads resolve; second save idempotent. **Rollback:** revert the helper and call sites;
old numeric scenes were never rewritten.

### Phase 2 — Audio references as paths
**Goal:** audio refs are paths too.
**Changes**
- `Audio/AudioEventRefSerializer.cpp` — `Sound` read/write through the helper (scene components,
  `AudioSurfaceTable`, `MusicDirector`, `AudioZone` ambience).
- `Scene/SceneSerializer.cpp` — `SoundBanks` list via the helper.
- `Asset/SoundSerializer.cpp` (Clip handles), `Asset/AudioAssetSerializer.cpp` (bank Sounds list) —
  same helper. Banks and sounds are files, so they stay in the editor project tree.
**Verification:** headless load of `AudioDemo` and `AudioSponza` with the registry deleted → every
`AudioEventRef`, bank and clip resolves; `AudioEventInstance` can be created for each Sound.
**Exit:** zero unresolved refs in both scenes. **Rollback:** revert the serializer edits.

### Phase 3 — Fresh-worktree proof and pack check
**Goal:** the success criterion, end to end.
**Changes:** none new; a script `scripts/` — **NEW** (one small PowerShell/bash file) that creates
a worktree, deletes the registry, runs the headless load and prints the unresolved count.
**Verification:** run it on two worktrees that each import the same new asset independently, then
merge: scenes still resolve. Export a runtime pack from the converted scene and load it with
`RuntimeAssetManager`: references resolve (confirms packs are unaffected).
**Exit:** zero unresolved in both; pack loads. **Rollback:** revert Phases 1–2.

### Phase 4 — Verdict and docs
**Goal:** a go/no-go with evidence for the full conversion (textures, materials, prefabs, project
file, script fields, registry out of git).
**Docs:** `.claude/docs/Architecture-LuxEngine.md § 2.8` — replace the rule "Reference assets by
`AssetHandle`, never by path after import" with the path-at-the-file-boundary rule and describe
`AssetReference`; `.claude/docs/Conventions.md` — add `AssetReference` to helper reuse.
**Exit:** findings written under Part 5; a follow-up plan or a stop.

## Part 3 — Verification beyond phases
- Opening the editor must not modify tracked files (Architecture §2.8 rule): a scene that loads in
  the editor is not re-saved until the user saves it.
- Existing scenes (`Benchmark`, `RenderFeatures`, others) must still load numerically: smoke-load
  every `.luxscene` headlessly after Phase 1.

## Part 4 — Risks
| Risk | Likelihood | Early detection |
|---|---|---|
| Deserialization runs on the worker and a path is not registered yet | Medium | S2; if so, defer the miss to a main-thread fix-up |
| Script `StoredFields` use a separate encoding that forces a C# change | Medium | S1 |
| Case-insensitive paths differ between save and registry | Low–medium | normalise in the helper; test with a case-changed path |
| Asset in a derived/sub-asset form with no file | Low | memory-only stays numeric; `GetSerializableStaticMeshHandle` already maps to the MeshSource file |
| Every scene rewritten on next save → large diffs | Certain | accepted; do it deliberately as one commit |
| New→old engine builds read 0 | Certain | accepted; documented |

## Part 5 — Open questions
- S1, S2 (above). S4: do prefabs and `.lbank` files embed other scenes' handles that the committed
  registry is the only way to decode? Resolved in the full plan.
- What would invalidate this plan: S2 showing scenes deserialize before `ProcessDirectory`
  completes, or S1 showing script fields cannot be converted without breaking existing assemblies.

## Part 6 — Research notes
Not fetched this session (no web search run); recorded as leads to verify, not facts.
- **LuckyEngine** (`M:\LuckyEngine\MAC-Source`, read this session): same random-handle design
  (`EditorAssetManager.cpp:659`), registry in the tree; only repairs *moved files* by filename score
  (lines 817–862) and has a `TODO` for ties. Nothing to copy except that repair idea.
- **Godot 4.x** (recalled, unverified): scenes store both a stable id and a `res://` path, using the
  path when the id is stale. The id-plus-path hybrid is the main alternative to this plan.
- **Unity** (recalled, unverified): `.meta` sidecar GUID per asset, committed; survives moves, adds
  one file per asset.
- **Rejected:** content hash as identity — changes on every edit.

## Follow-ups (out of scope)
Textures/materials/prefabs/project file/packs, script fields, gitignoring the registry, a
"Repair asset references" editor action with a tie prompt.

## Part 7 — Spike results (2026-11-17)

All phases were run headlessly (`AssetRefHarness`, a scratch console exe linking `Core.lib`, run
against a clean git worktree of the sample project; the editor was never launched).

**Phase 0 answers**
- **S1 (script fields):** `DataType` has `Entity, Prefab, Mesh, StaticMesh, Material, Texture2D, Scene,
  Sound, SoundBank` (`Scripting/ScriptEntityStorage.hpp`); asset-typed fields were stored through the
  `uint64_t` default branch of `SerializeStoredField`. They now go through `AssetReference`
  (`Entity` stays numeric: it is an entity UUID, not an asset).
- **S2 (thread):** yes. `SceneAssetSerializer::TryLoadData` is reached from the asset worker
  (`EditorAssetSystem.cpp` calls `AssetImporter::TryLoadData`), so path resolution is a read-only
  registry lookup and never imports. `MaterialSerializer` already did the same for textures.
- **S3 baseline (registry deleted in a fresh worktree):** `AudioSponza` 15 of 15 references
  unresolved (12 scene + 3 script fields), `AudioDemo` 31 of 31.

**Added beyond the plan (found while grounding):** the pack stores scene and `.lsound`/`.lbank` text
produced by the same writers, and the player has no path map, so path output is opt-in
(`PathModeScope`), opened only by `SceneSerializer::Serialize(filepath)` and the sound/bank file
save. `SerializeRuntime` writes numbers. `AssetRegistry` gained a path index (`FindByPathKey`,
`EditorAssetManager::GetAssetHandleFromPathKey`) because the old lookup scanned every entry.
An unresolved path keeps a stable FNV-1a handle and is written back unchanged, so saving never
erases a reference.

**Results with the final code**
| Check | Result |
|---|---|
| Converted `AudioSponza` + 3 `.lsound` + 2 `.lbank`, registry deleted, refs resolved | 12 scene + 3 script + 5 audio-file references, **0 unresolved** |
| `SceneSerializer::RunRoundTripSelfTests` (incl. new `AssetReference` cases) | PASS |
| Scene pack (`SerializeToAssetPack`) and packed `.lsound`/`.lbank` text | no path strings: numeric |
| Second save equals first | equal as entity sets (34 entities); **byte order differs**: entities are written in a different order on each load/save. The cause was not isolated; this change does not touch ordering |
| Benchmark (6,911 meshes, 13,876 refs), `Deserialize` alone | 107 ms numeric, 113 ms path form. A first build showed 2,100 ms: the registry path index was never populated (an edit that silently did not apply), so every read scanned the registry; found by timing, fixed, re-measured |
| Committed scenes still load | `AudioDemo` and `AudioSponza` numeric forms resolve with the committed registry |

**Built:** Core Release and Debug, Editor Release, Lux-Runtime Release (all compile with this change).
**Not verified:** editor open/Play/save, the Dist player loading a pack exported with this build,
the editor audio-panel UI tests (`tests/editor/run_audio_panels.py`; they launch the editor), Linux,
and any scene other than the `Audio*` pair and `Benchmark`. The converted scene was not opened in
the user's own checkout, whose working registry holds uncommitted changes.

**Verdict: go.** References as paths work end to end for meshes, collider, material tables,
audio refs, banks, sounds and script asset fields, and old numeric files keep loading. Remaining
for the full plan: texture and material refs inside `.lmat`, prefabs, `.luxproj`, a "Repair asset
references" action, then gitignoring `AssetRegistry.lzr` once nothing depends on its handles being
stable. Surface and dialogue tables use the same scope on save. Two things to settle first: the entity-order instability (it makes every converted scene a
whole-file diff), and whether to convert committed scenes in one deliberate commit.

**Converted in this spike (separate commit; `AudioSponza` is a whole-file diff because entity order changes on save):** `AudioSponza.luxscene`, `Audio/Sounds/*.lsound`,
`Audio/Banks/*.lbank`.

---

# Part 8 — Next slice: finish the conversion and retire the committed registry

Written 2026-11-17 after the spike (commits `4978800f`..`bf065e64`). Plan only. Web research was
skipped: the design is fixed by the engine's own serializers and the spike's Part 6 prior-art notes.

**Goal card**
- **Goal:** every reference the engine saves is an Assets-relative path, saves are stable (no
  reordering churn), broken references are reported with candidates, and `AssetRegistry.lzr` can leave git.
- **Success:** in a fresh sparse worktree with the registry deleted, **all 13 committed scenes** and
  every `.lmat`/`.lsound`/`.lbank`/`.lprefab` load with 0 unresolved references (harness `check`);
  a second save is **byte-identical**; opening a project never modifies a tracked file; the registry
  is untracked and the sample still opens.
- **Non-goals:** the asset-pack format (stays handle-based and numeric), runtime player changes,
  Linux, content-hash identity, `.meta` sidecars.
- **Constraints:** old numeric files keep loading; packs, snapshots and undo diffs stay numeric;
  never launch the editor without explicit OK that turn; never `Project::Load` against the main checkout.
- **Owner's words:** "it should work in a relative location from the asset folder."

## 8.0 Where we are (ledger; ✅ read in source this session, ⚠️ read, not run)

| Fact | State | Evidence |
|---|---|---|
| **Material texture refs already save as paths**, packs as handles, via `TextureReferenceSerialization::{AssetPaths,AssetHandles}` | ✅ | `Asset/MaterialSerializer.cpp` `WriteTextureReference`, `TrySerialize` (paths) vs `SerializeToAssetPack` (handles); the committed `.lmat` files already hold paths (`AlbedoMap: Meshes/Source/Sponza/<n>.jpg`) |
| Material `Parent` is a bare number in files | ✅ | `MaterialSerializer.cpp` write ~line 425, read ~616 (`as<uint64_t>`); 2 of 213 `.lmat` hold a numeric `Parent`/`Texture`/`Graph` |
| Material graph nodes write `Texture` and `Graph` as numbers; `MaterialGraph::Serialize` is shared by file and pack | ✅ | `Renderer/MaterialGraph/MaterialGraph.cpp` ~784/786 write, ~838/846 read |
| Prefab base files go through `SceneSerializer::Serialize(path)`, so they are **already path form** after the spike | ✅ | `Asset/PrefabSerializer.cpp` `WritePrefabFile` |
| Prefab **variants** use `SerializeToString()` (numeric) plus `BasePrefab: <number>` | ✅ | same function; read in `TryLoadData` as `node["BasePrefab"].as<uint64_t>` |
| `.lmesh` / `.lsmesh` / `.lmc` write `MeshSource` / `Mesh` as numbers | ✅ | `Asset/MeshSerializer.cpp` lines 44, 80; the sample project has **no** such files, so this content is untested |
| `.luxproj` holds one handle, `RuntimeExport.IconHandle`; `StartScene` is already a path with a numeric fallback | ✅ | `Project/ProjectSerializer.cpp` 733, 842–844, 1011–1017; `LuxSample.luxproj` line 22 |
| Scene `FontHandle` is a number; the default font is a memory asset | ✅ | `SceneSerializer.cpp`; `Editor/Source/Panels/SceneHierarchyPanel.cpp` `Font::GetDefaultFont()->Handle` |
| The repo has one project, one registry, 13 scenes | ✅ | `git ls-files` |
| **Entity order flips on every save** | observed; ⚠️ cause is a hypothesis | `SceneSerializer::SerializeToYAML` iterates `m_Registry.each`; Deserialize creates entities in file order. If `each` visits newest first, every save reverses the file. Commit `bd9c7391` wrote the current loop to fix an earlier reversal: check what it assumed |
| Unresolved paths are remembered (`AssetReference::Unresolved()`); unresolved numbers are not | ✅ | `Asset/AssetReference.h` |
| Loading a project already rewrites the registry (3 added lines in a clean worktree) | observed | spike worktree `git status` after `Project::Load` |

## 8.1 Decisions

| Decision | Options | Recommendation | Consequence |
|---|---|---|---|
| Entity order | A: make load → save the identity. B: leave it | **A, first** | Otherwise every converted scene is a whole-file diff (Benchmark is 129k lines) |
| Retiring the registry | A: gitignore after every ref converts (a rebuilt cache). B: keep it committed and only repair | **A, last, gated by a measurement** | Needs all tracked files converted; a clean clone re-imports every file |
| Repair | A: report with candidates in Core. B: an editor modal | **A now, B later** | B needs ImGui and an editor launch to verify, which needs your OK |
| Converting committed scenes | A: one deliberate commit after the order fix. B: lazily on next save | **A** | One reviewable diff of reference lines |
| Memory-only assets (default font, procedural meshes) | stay numeric | stay numeric | They are not registry-backed, so they never broke |

## 8.2 Design additions

- **Rule unchanged:** write paths only inside `AssetReference::PathModeScope` opened by a *file*
  save; reads accept both; packs, snapshots and `SerializeRuntime` stay numeric.
- **Material file save** opens the scope when `textureReferenceSerialization == AssetPaths`. That
  covers `Parent` and the graph's `Texture`/`Graph`. The pack path stays unscoped, so the shared
  `MaterialGraph::Serialize` stays numeric there.
- **NEW `AssetReference::Report()` / `FindCandidates(path)`:** drains the unresolved store into
  (path, count, candidates) for a log summary after a project loads. No UI.
- **Threads:** lookups stay read-only (asset-worker safe). `Report` runs on the main thread after
  `SyncWithAssetThread`. Nothing touches the render thread or the GPU.
- **Runtime/Dist:** unchanged; the player reads numeric pack text. Re-verified by harness `packcheck`.

## 8.3 Phases

### Phase A — Stable saves (byte-identical round trip)
**Goal:** `load → save → load → save` gives identical bytes.
**Changes:** `Scene/SceneSerializer.cpp` `SerializeToYAML`: emit entities in the order a load
recreated them (settle the `each` direction by experiment in the harness first).
`SerializeEntitySnapshots` is keyed by UUID and stays.
**Verification:** harness `resave` on all 13 scenes: byte-identical; `RunRoundTripSelfTests` passes;
the file order of `AudioDemo` is unchanged after one save.
**Docs:** none. **Exit:** 13 of 13 identical. **Rollback:** revert the loop.

### Phase B — Remaining file serializers
**Goal:** `.lmat` `Parent` and graph refs, prefab variants (`BasePrefab`), and `.lmesh`/`.lsmesh`/`.lmc`
read and write paths.
**Changes:** `MaterialSerializer.cpp` (scope in `TrySerialize`; `Parent` through `AssetReference`),
`MaterialGraph.cpp` (`Texture`/`Graph`), `PrefabSerializer.cpp` (scope around the variant's
`SerializeToString`, path `BasePrefab`), `MeshSerializer.cpp` (three writers and readers). Replace
`MaterialSerializer.cpp:142` `TryParseAssetHandle` with `AssetReference::TryParseHandle` and delete the duplicate.
**Verification:** the sample has no `.lmesh`/`.lprefab`, so the harness writes minimal fixtures with
the engine's own writers in the worktree; convert, delete the registry, `check` shows 0 unresolved;
`packcheck` shows the packed material text numeric.
**Exit:** fixtures resolve with the registry deleted; packs numeric. **Rollback:** per file.

### Phase C — Project file and scene leftovers
**Goal:** `IconHandle` and a file-backed `FontHandle` save as paths.
**Changes:** `Project/ProjectSerializer.cpp` 733 and 995 (path with numeric fallback);
`SceneSerializer.cpp` `FontHandle` write/read. The default font stays numeric.
**Verification:** `check` on `LuxSample.luxproj` with the registry deleted. The file also stores every
renderer setting (`luxproj-settings-regression-trap`): diff it before and after.
**Exit:** no asset number left in the converted files. **Rollback:** revert.

### Phase D — Convert committed content, with an inventory
**Goal:** every tracked `.luxscene`, `.lmat`, `.lsound`, `.lbank`, `.luxproj` is in path form.
**Changes:** the harness `convert`/`convertasset` over the clean worktree (committed registry), copy
back. **NEW** harness mode `inventory` counts numeric vs path references per file type and fails if
any numeric asset reference remains.
**Verification:** registry deleted → `inventory` 0 numeric, `check` 0 unresolved on all 13 scenes;
Benchmark `Deserialize` stays near the spike's 113 ms.
**Exit:** one data commit, reference lines only (Phase A keeps it small). **Rollback:** `git revert`;
numeric files still load.

### Phase E — Registry as a rebuilt cache
**Goal:** `AssetRegistry.lzr` is untracked and a clean clone opens.
**Measure first (headless, `/profile` method):** `Project::Load` with the registry present vs deleted on
the sample. A deleted-registry open must cost at most a few seconds, and an existing registry must not
re-import. If the cost is higher, plan an untracked persisted cache instead of a rebuild.
**Changes:** `.gitignore` and `git rm --cached` the file; engine code only if the measurement demands.
**Verification:** a fresh clone in a scratch folder passes `check` on every scene with no registry;
two opens leave `git status` clean.
**Docs:** Architecture §2.8 (the registry is a cache; rewrite the "opening the editor must not modify
tracked project files" rule), `Building.md` if CI relied on the file.
**Exit:** clean clone passes. **Rollback:** re-add the file.

### Phase F — Repair report (Core only; UI deferred)
**Goal:** after a load, unresolved references are listed with their filename candidates.
**Changes:** **NEW** `AssetReference::Report()` and `FindCandidates(path)` using `AssetRegistry`
filename matches. A tie returns a list and is never auto-picked (LuckyEngine scores candidates and
leaves ties as a `TODO`). Log only.
**Verification:** harness: rename one asset in the worktree, load, expect one unresolved path with the
moved file as the single candidate; two same-named files give a tie.
**Exit:** both cases correct. **Follow-up needing your OK to launch the editor:** a "Repair asset
references" modal (ImGui scopes, unique IDs, every state driven in the editor per
`Conventions.md § ImGui correctness`).

## 8.4 Verification beyond phases
- Final run of the spike battery plus `inventory`, on a fresh sparse worktree: registry deleted, then
  a second worktree where an agent imports a new asset independently and the two merge.
- Debug Core, Release Editor and Lux-Runtime build; `RunRoundTripSelfTests` passes.
- Not coverable headlessly: editor open/Play/save UI, the audio-panel UI tests, the Dist player on a
  new pack, Linux. Say so in the PR.

## 8.5 Risks
| Risk | Likelihood | Detection |
|---|---|---|
| The `each` direction guess is wrong and the fix reorders something else | Medium | Phase A experiment on 13 scenes before any other change |
| A clean-clone registry rebuild is slow | Medium | Phase E measurement gates the gitignore |
| Old or hand-written files hold backslashes or `./` paths | Low | `ResolvePath` normalises as a fallback; `inventory` flags misses |
| Prefab-variant and `.lmesh` formats are untested content | Medium | Minimal fixtures from the engine's own writers (Phase B) |
| Converting Benchmark rewrites a 129k-line file | Certain | Phase A first; review only reference lines |
| Two assets share a filename, so repair is ambiguous | Low | Report returns ties and never auto-picks |

## 8.6 Open questions
- The direction of `entt::registry::each` and what `bd9c7391` assumed (Phase A).
- Whether anything outside the serializers keys on registry handles across sessions (thumbnail
  cache, recent-scene list, editor layout): grep before Phase E.
- Whether the default font handle is stable across runs (scenes mostly store 0).
- What would invalidate this plan: Phase E measuring a rebuild far over budget, or Phase A showing
  the entity order is deliberately unstable.

**First phase to implement:** Phase A (it keeps every later diff small). Implement with `/dev`,
review with `/cr`, ship with `/send-pr`. Implemented 2026-11-17: see Part 9.

---

# Part 9 — Results of the Part 8 slice (2026-11-17, PR #174)

All phases A-F are done. Everything below was run headlessly with `AssetRefHarness` (a scratch console exe
linking `Core.lib`) against clean git worktrees of the sample project; the editor was never launched.

| Phase | What changed | Result |
|---|---|---|
| A: stable saves | `SceneSerializer::SerializeToYAML` writes the reverse of `registry.each()` (it visits the newest first, so the old order reversed the file on every save). `Scene::Copy` creates entities in entity-index order instead of view order (the view is oldest-first once `SortEntities` ran, which made every save of an *opened* scene reverse the file) | `resave` and `resavecopy` (the editor's open path) on all 24 sample scenes: second save byte-identical, 48 of 48; the entity order of a saved scene equals the file's |
| B: serializers | `.lmat` `Parent`, `DefaultTexture`, graph refs; `.lmesh`/`.lsmesh`/`.lmc`; prefab variant `BasePrefab`; `.lanimgraph` state `Clip` and animation event `Sound`; dialogue-table `Clip`; project `SurfaceTable`/`DialogueTable`; scene `Prefab`, `SkeletonComponent` asset/clip/graph, sprite `Texture` and sky `EnvironmentMap` all read both forms and write paths inside a file save | Packs stay numeric (`packcheck` on AudioDemo, Benchmark and RigPosed: no path in the pack) |
| C: project file and leftovers | `IconHandle` is dropped when `IconPath` names the icon (already the writer's rule) and read tolerates a path; `FontHandle` already a path | `LuxSample.luxproj` holds no numeric reference |
| D: convert committed content | One reference-only edit of 24 scenes (14,456 lines), 2 `.lmat`, `LuxBiped.lanimgraph`, `LuxSample.luxproj`, by text rewrite from the committed registry, not a re-save: a re-save also rewrites unrelated lines (`Children`, post-process fields) | `inventory`: 248 files, 0 numeric references. Each converted scene re-saves byte-identical to the re-save of its numeric original (24 of 24) |
| E: registry as a cache | `AssetRegistry.lzr` untracked and ignored | With the registry deleted, `check` on all 24 scenes: 0 unresolved. A rebuild is about 6 s in a Debug harness (a few runs took 9-20 s, one 89 s while the machine was busy with a build) |
| F: repair report | Done in the previous commit (`AssetReference::Report`) | Reports unresolved paths with filename candidates (log only) |

**Dependents of the committed registry, changed in the same PR:** `tests/audio/native` (hashes each audio
file's path to a handle, reads the Benchmark director through `ReadValue`), `scripts/GenerateBenchmarkScenes.py`
(emits paths; no registry upsert; its scenes match the committed ones reference for reference),
`tests/audio/run_scene_acoustics.py` and `tests/editor/run_remaster_export.py` (a mesh's "handle" is its path;
the registry copy is optional). The last two were edited and syntax-checked only: they launch the engine and were not run.

**Two latent bugs found by the self-tests and fixed here:** `Scene::CreateEntityWithID` read its `name` argument
after adding components, and `DuplicateEntity` passes a name that points into the same registry's tag pool
(use after reallocation); `DuplicateEntity` also iterated the live `Children()` vector while creating entities
(the second child of a duplicated parent was lost). `SceneSerializer::RunRoundTripSelfTests` passes.

**Not verified:** editor open/Play/save UI, the Dist player on a pack exported with this build, the two engine-launching
Python runners above, Linux. A user's local `AssetRegistry.lzr` is now untracked: pulling this deletes the tracked copy,
and `Project::Load` rebuilds it.
