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
| Benchmark (6,911 meshes, 13,876 refs), load+save twice | 3.7 s numeric, 5.7 s path form; lookups are O(1), the extra ~2 s was not profiled |
| Committed scenes still load | `AudioDemo` and `AudioSponza` numeric forms resolve with the committed registry |

**Not verified:** editor open/Play/save, the Dist player loading a pack exported with this build,
the editor audio-panel UI tests (`tests/editor/run_audio_panels.py`; they launch the editor), a
Debug build (Release of Core and the harness built), Linux, and any scene other than the
`Audio*` pair and `Benchmark`.

**Verdict: go.** References as paths work end to end for meshes, collider, material tables,
audio refs, banks, sounds and script asset fields, and old numeric files keep loading. Remaining
for the full plan: texture and material refs inside `.lmat`, prefabs, `.luxproj`, a "Repair asset
references" action, then gitignoring `AssetRegistry.lzr` once nothing depends on its handles being
stable. Two things to settle first: the entity-order instability (it makes every converted scene a
whole-file diff), and whether to convert committed scenes in one deliberate commit.

**Converted in this spike (separate commit):** `AudioSponza.luxscene`, `Audio/Sounds/*.lsound`,
`Audio/Banks/*.lbank`.
