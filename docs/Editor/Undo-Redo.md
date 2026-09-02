# Undo / Redo

The editor's undo/redo system: how the current (v1) snapshot-based implementation works, what it
covers, and the phased plan to grow it into a full, granular, all-subsystem history.

Source: `Core/Source/Lux/Editor/EditorStack.h`, the `#if UndoDo` hooks in
`Core/Source/Lux/ImGui/ImGuiEx.h`, and the snapshot machinery in `Editor/Source/EditorLayer.cpp`
(`CaptureSceneSnapshot` / `CommitSceneSnapshot` / `PollSceneEditForUndo` / `UndoSceneEdit` /
`RedoSceneEdit` / `RestoreSceneSnapshot` / `AdoptEditorScene`).

---

## TL;DR

- **`Ctrl+Z`** undo, **`Ctrl+Shift+Z`** / **`Ctrl+Y`** redo, and **Edit → Undo / Redo**.
- Works in **Edit mode** on the scene you're editing, and in **Play/Simulate** on a separate transient
  history (discarded on Stop — undoing there restarts the runtime).
- Covers: every inspector field, rename, add/remove component, **gizmo** moves, entity **delete**
  and **duplicate**.
- Implemented as **whole-scene YAML snapshots** captured when an edit finishes — simple and
  crash-safe. The phased plan below replaces snapshots with granular commands for scale.

---

## Why snapshots (and why not the old pointer approach)

An earlier attempt enabled a pre-existing scaffold that recorded each edit as a **raw pointer** to
the changed value (`PushCopy(&value, oldValue)`). That is unsafe here: the editor's dominant inspector
idiom is copy-edit-writeback with a **stack local** —

```cpp
float perspectiveNear = camera.GetPerspectiveNearClip();   // a local
if (ImGuiEx::Property("Near", perspectiveNear))            // &perspectiveNear captured
    camera.SetPerspectiveNearClip(perspectiveNear);       // local dies at end of frame
```

— so a later `Ctrl+Z` would write through a dangling pointer and corrupt the stack. There are ~50
such sites across the panels; making the pointer model safe would mean auditing every one.

**The snapshot model sidesteps this entirely: it captures values (a serialized scene), never
pointers.** The old `PushCopy` call sites are kept, but `EditorStack::PushCopy` now **ignores its
arguments** and merely raises a "scene edited" flag — so nothing dangles, and no call site had to
change.

---

## How v1 works

### 1. The signal — `EditorStack`

`EditorStack` (`Core/Source/Lux/Editor/EditorStack.h`) is a tiny **main-thread singleton** carrying
one flag:

```cpp
template<typename T> void PushCopy(T*, const T&) { m_SceneEditPending = true; } // args ignored
void MarkSceneEdited()   { m_SceneEditPending = true; }
bool ConsumeSceneEdit()  { bool v = m_SceneEditPending; m_SceneEditPending = false; return v; }
```

- **Property widgets** raise it automatically: `ImGuiEx::Property(...)` calls `PushCopy` on every
  change (gated by `#define UndoDo 1` in `ImGuiEx.h`). This covers **every field** the inspector
  draws — transforms, lights, physics, camera, materials — with zero per-site work.
- **Non-widget edits** call `MarkSceneEdited()` directly: the gizmo (on drag release), entity
  delete, entity duplicate, add/remove component, and rename.

### 2. The commit — `EditorLayer::PollSceneEditForUndo`

Called every frame right after the panels render:

```cpp
if (m_SceneState != SceneState::Edit) { EditorStack::Get().ConsumeSceneEdit(); return; }
if (EditorStack::Get().ConsumeSceneEdit()) m_UndoCommitPending = true;
if (m_UndoCommitPending && !ImGui::IsAnyItemActive()) { CommitSceneSnapshot(); m_UndoCommitPending = false; }
```

The `!IsAnyItemActive()` gate is the **coalescing** trick: while a slider is being dragged the flag
is raised every frame, but the snapshot is deferred until the drag **finishes** — so a whole drag is
one undo step, not sixty.

### 3. The history — granular per-entity diffs

The scene is serialized **per entity**, not as one blob. `SceneSerializer::SerializeEntitySnapshots`
round-trips the scene through YAML and splits it into a `map<UUID, string>` of entity blocks plus a
`meta` string (name + post-processing). `CommitSceneSnapshot` diffs the current split against the
committed baseline and stores **only what changed**:

```cpp
current = CaptureSceneEntities(meta);            // map<UUID,string> + meta
for each entity in baseline:  if value differs (or gone) -> delta {before, after}   // changed / deleted
for each entity in current:   if not in baseline        -> delta {"",     after}    // created
if no deltas and meta unchanged -> return;       // no-op (e.g. a settings-panel edit)
push UndoCommand{ label, metaDelta, entityDeltas };   // bounded by step count + a byte budget
baseline = current;
```

So each history step is **O(change)** in memory — a two-entity edit stores two small YAML blocks, not
the whole scene. (Commit still serializes the scene once to compute the diff; it's the stored *history*
that shrank, which is what bounds memory across the history — see the budget in Phase 8.)

`UndoSceneEdit` / `RedoSceneEdit` apply the step's `before` / `after` sides to the baseline map (set a
changed entity, erase a created one, re-add a deleted one), then call `RestoreSceneState`, which
**reassembles the full scene** (`DeserializeFromSnapshots`) into a **fresh `Scene`** and retargets
everything (`AdoptEditorScene`). This is the key safety property: restore never edits the live scene in
place — it rebuilds the whole scene from the entity set every time, so the recursive-destroy /
two-way-parent-child hazards never arise. Selection is UUID-based (`SelectionManager`), so it survives a
restore where the entity still exists and is pruned where it doesn't.

An edit that **didn't change the scene** (a Scene-Renderer or Project-Settings field, which isn't part
of the scene YAML) produces no deltas and no meta change — a no-op, safe to over-signal.

### 4. When the history resets

`ResetUndoHistory()` rebaselines and clears the edit stacks at every point a snapshot's identity would
no longer apply: **`OnAttach`** (initial load), **`NewScene`**, **`OpenScene`**. Entering Play/Simulate
starts a **separate transient history** over the runtime scene (`BeginPlayUndoHistory`) that is cleared
on Stop — the edit-mode history is left completely untouched by a play session (see Phase 7 below).

---

## v1 coverage

| Action | Covered | How |
|---|:---:|---|
| Any inspector field (light, physics, camera, material params, …) | ✔ | `ImGuiEx::Property` signal |
| Transform (position/rotation/scale, typed or dragged) | ✔ | `DrawVec3Control` signals directly (it uses raw `DragFloat`, not `Property`) |
| Entity rename | ✔ | `MarkSceneEdited` on the Name field |
| Add component | ✔ | `MarkSceneEdited` in the Add-Component popup |
| Remove component | ✔ | `MarkSceneEdited` after removal |
| Gizmo move/rotate/scale | ✔ | `MarkSceneEdited` on drag release |
| Delete entity | ✔ | `MarkSceneEdited` after destroy |
| Duplicate entity | ✔ | `MarkSceneEdited` after duplicate |
| Create entity (Create-menu items) | ✔ | `MarkSceneEdited("Create Entity")` in the `createEntity` choke |
| Reparent (drag in hierarchy) | ✔ | `MarkSceneEdited("Reparent")` on the drag-drop targets |
| Prefab drag-into-scene | ✔ | `MarkSceneEdited("Add Prefab")` after instantiate |
| Scene post-processing (exposure, tonemap, grading) | ✔ | it lives in the scene `meta` → captured by the diff |
| Material *assignment* (which material on a mesh) | ✔ | it's a component field (`MaterialTable`) → scene diff |
| Renderer / project settings (quality, GTAO, shadows, culling…) | ✔ | `ProjectSceneRendererSettings` snapshot via a closure command (Phase 6) |
| Material *asset* properties (albedo/roughness) | ✘ | only via the unused `MaterialEditorPanel` — low value |
| Content Browser asset ops (rename / move / delete) | ✔ | closure commands; delete moves the file to `<project>/.trash` (never a real delete), undo restores the same handle |
| Content Browser *directory* ops | ✘ | still a permanent delete — not covered (recursive; own follow-up) |
| Play/Simulate edits | ✔ (transient) | separate play-mode stack, discarded on Stop; undo rebuilds & restarts the runtime |

Each undo step now carries a **label** — the Edit menu shows "Undo Move", "Undo Delete Entity",
"Redo Add Component", etc. The label comes from the edit that triggered the commit (the gizmo op,
the structural hook's string, or a generic "Edit" for field changes).

Undo/redo also **restores selection** (Phase 4): after a step is applied, the entities that step
touched (that still exist) are selected, so you see what changed — deleting an entity and undoing
re-selects it; a multi-entity edit re-selects all of them. And a **History panel** (Phase 5, View →
History) shows the whole stack with labels and lets you click any state to jump straight to it.

### Known limitations (addressed by the remaining plan)

- **Commit compute:** a commit still *serializes* the whole scene once to compute the diff (O(scene)),
  even though only the changed entities are *stored*. A future optimization could serialize just the
  affected entities (known from the selection), but the memory win — the thing that mattered across 64
  steps — is already done (Phase 3B).
- **Granularity:** the unit is "which entities changed," so two unrelated edits in one non-interactive
  frame could land in one step. Rare in practice.
- **Editor-camera framing** isn't part of a step (selection is). Minor.
- **The other subsystems** — material editor, project/renderer settings, Content Browser file ops —
  aren't on the history yet (Phase 6).

---

## The phased completion plan

Each phase is independently shippable and leaves the editor working. Phases 3+ change the *storage*
model behind the same `Ctrl+Z` surface, so users see continuity while the internals get better.

### Phase 1 — Snapshot foundation ✅ (done, this change)

Whole-scene snapshots; the `EditorStack` signal + `ImGuiEx` hook; commit-on-edit-finish coalescing;
`Ctrl+Z` / `Ctrl+Shift+Z` / `Ctrl+Y` + Edit-menu items; reset on scene load; Edit-mode gating.
Coverage per the table above.

### Phase 2 — Complete scene-structural coverage ✅ (done)

The remaining **scene** mutations now raise the signal, so nothing structural is missed:

- Entity **create** — one `MarkSceneEdited("Create Entity")` in the `createEntity` lambda, which is the
  choke every Create-menu item runs through.
- **Reparent** via drag-drop — both `AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")` targets (root and
  onto-entity).
- **Prefab** drag-into-scene — after `InstantiatePrefab`, on both targets.

Multi-select structural ops already flow through these same choke points.

### Phase 3 — Command model

**Part A — command framework + labels ✅ (done).** The two raw `std::vector<std::string>` stacks are
now `std::vector<UndoCommand>` where `UndoCommand { Label; State; }`. `EditorStack` carries a pending
**label** set by each edit (`MarkSceneEdited("Move")`, `"Delete Entity"`, …; field edits default to
`"Edit"`). `EditorLayer` records the label with each commit and the Edit menu shows "Undo &lt;label&gt;" /
"Redo &lt;label&gt;". The command *shape* is the seam the granular payload (below) and the other
subsystems (Phase 6) plug into without touching the `Ctrl+Z` surface.

**Part B — granular O(change) storage ✅ (done).** History steps now store only the changed entities'
YAML (before/after), not the whole scene — see [The history](#3-the-history--granular-per-entity-diffs).

The design sidesteps the hierarchy hazard rather than fighting it. This scene graph uses **recursive
`DestroyEntity`** and **two-way parent/child links** (`RelationshipComponent` on both ends), so the
"obvious" granular restore — destroy + recreate the changed entity by UUID — would orphan children and
desync parents' `Children` lists. The in-place alternative would need an *idempotent* per-component
deserialize (a `SceneSerializer` refactor across every component type, with side effects like
script-storage re-sync). **So granularity was put in the storage, not the restore:** the diff is
per-entity, but restore **reassembles the full scene and runs the normal whole-scene deserialize**
(`SerializeEntitySnapshots` / `DeserializeFromSnapshots` in `SceneSerializer`) — the same proven path
that loads a `.luxscene`, so it can't corrupt the links. A round-trip self-test (parent + two children,
transform + point/directional lights) confirmed split → reassemble → deserialize → re-split reproduces
every entity's YAML and the scene metadata exactly.

Remaining optional refinement (not required for the memory win): serialize only the *affected* entities
at commit (known from the selection) to make commit compute O(change) too; and, if ever wanted,
true in-place field commands. Neither is needed now.

### Phase 4 — Selection restore ✅ (done)

After a step is applied, `EditorLayer::RestoreSelection` selects the entities that step **touched**
(the `EntityDelta` handles) that still exist, through `SelectionManager`. This is derived from the
command rather than stored as a separate before/after set — it handles multi-select naturally and does
the intuitive thing for structural edits (undo a delete → the recreated entity is selected; undo a
create → nothing to select). Editor-camera framing was left out (low value).

### Phase 5 — History panel ✅ (done); transactions n/a

- **History panel** — `UndoHistoryPanel` (View → History), a header-only `EditorPanel` that reads the
  stack through function bindings (so it's decoupled from `EditorLayer`) and renders it
  Photoshop-style: past states top, current in the middle, redoable states below (dimmed). Clicking a
  row jumps straight to that state (undoes/redoes the right number of steps).
- **Transactions turned out to be unnecessary** for the common case: because a commit diffs the *whole*
  scene at edit-completion, any single action that changes several entities in one go (paste, a
  multi-select edit) is **already one step**. Explicit grouping would only matter for merging several
  *separate* edit-completions, which no current workflow needs — deferred until one does.

### Phase 6 — Beyond the scene

Two things enabled this: a **hybrid command** and the discovery that much of "beyond the scene" was
already covered.

**The foundation.** `UndoCommand` now carries optional `CustomUndo` / `CustomRedo` closures. When set,
`Undo`/`RedoSceneEdit` call them instead of applying a scene diff — so the one stack (and the one
`Ctrl+Z`, menu, and History panel) holds heterogeneous commands. `EditorLayer::PushUndoCommand(label,
undo, redo)` is the entry point any subsystem uses. The scene path is untouched (closures are simply
absent on scene commands).

**Already covered by the scene diff (no work needed):** scene **post-processing** rides the scene
`meta`; **material assignment** (which material a mesh uses) is a component field. Both undo already.

**Renderer / project settings ✅ (done).** These aren't scene state, so they get a closure command.
On edit-completion the poll runs `CommitRendererSettings`: it snapshots the renderer as a
`ProjectSceneRendererSettings` (via `SceneRenderer::WriteProjectSettings`, which **excludes the
transient debug-view toggles** by construction, so toggling the grid never pollutes history), compares
it to a baseline (memcmp of zero-padded structs), and if it changed pushes a command whose closures
call `SceneRenderer::ApplyProjectSettings` — the same canonical apply-and-refresh used on project load.
A capture→change→restore self-test confirmed detection and exact restore.

**Content Browser asset ops ✅ (done).** Rename / move / delete of an asset are now reversible closure
commands (`ContentBrowserPanel::SetUndoPush` routes them to `PushUndoCommand`). The reversible
primitives are static `RawRenameAsset` / `RawMoveAsset` / `TrashAsset` / `RestoreAsset`; the public
`RenameAsset` / `MoveAsset` / `DeleteAsset` call the primitive then push {undo, redo} closures that call
it back the other way and `Refresh()` the browser. **Delete is fail-safe: it never calls
`FileSystem::DeleteFile` — it moves the file to `<project>/.trash/<handle>__<name>`** (outside the
scanned asset dir, so `ProcessDirectory` won't re-import it), removes the handle from the registry, and
undo moves it back and re-registers the **same handle** (so scene references still resolve). The trash
is never auto-purged, so even a bug can't lose data. *Directory* delete/move/rename are **not** covered
(they use a separate permanent-delete path) and remain a follow-up.

**Deferred:** **material-*asset* properties** (only reachable via the unused `MaterialEditorPanel` —
low value), and Content Browser **directory** ops. **Node graphs / animation** register their own
providers if/when they exist.

### Phase 7 — Play / Simulate undo ✅ (done)

Play/Simulate now has a **separate transient history** (`m_PlayUndoStack` / `m_PlayRedoStack`,
baselined by `BeginPlayUndoHistory` on Play/Simulate, cleared on Stop). Undo/redo route through
`ActiveUndoStack()` — the edit stack in Edit mode, the play stack otherwise — so the same `Ctrl+Z`,
Edit menu, and History panel drive both, and edit-mode history is completely untouched by a play
session.

The catch (as designed): a play step is a closure command whose restore rebuilds the runtime scene
from the snapshot and **restarts its runtime** (`OnRuntimeStop` → `DeserializeFromSnapshots` →
`OnRuntimeStart`, via `AdoptRuntimeScene`). So undoing a tweak during Play **resets physics/scripts to
that point** and continues — the unavoidable consequence of not having granular in-place restore.
A headless self-test (enter Play → create entity → commit → undo → verify gone → redo → verify back)
confirmed the round-trip, including the mid-play runtime restart.

### Phase 8 — Robustness & budget ✅ (done)

- **Memory budget.** Each command stores its heap payload size (`UndoCommand::ApproxBytes` — the
  entity/meta YAML), and `TrimUndoStack` evicts the oldest steps until the stack is under **both** a
  step cap (`s_MaxUndoDepth = 256`) and a byte budget (`s_MaxUndoBytes = 128 MB`), always keeping at
  least one step. Applies to the edit and play stacks alike. So a handful of huge diffs can't blow up
  memory, and small edits keep a long history.
- **Round-trip test.** `SceneSerializer::RunRoundTripSelfTests` builds a scene with a hierarchy and
  components, does split → reassemble → re-split, and confirms every entity's YAML and the metadata
  are reproduced exactly. It's surfaced as a button in the **Renderer Debugger** (next to the
  render-graph self-tests), so a regression in the snapshot path the undo system depends on is one
  click away from being caught.

**Still optional (deferred):** snapshot **compression**, and an on-disk **undo journal** for crash
recovery — neither needed yet.

### Phase 9 — Polish ✅ (done)

- **Undo/redo toasts** — after an undo or redo, a transient "Undo: &lt;label&gt;" / "Redo: &lt;label&gt;" pill
  fades in at the bottom of the window (`EditorLayer::UI_UndoToast`, ~1.6 s visible + 0.5 s fade), so
  the action registers even when the change is off-screen or subtle.

Intentionally skipped (low value / debatable): **held-`Ctrl+Z` key-repeat** (mass-undo-on-hold is
easy to trigger by accident — a discrete press per step is safer), **per-widget coalescing tuning**
(the commit-on-`IsAnyItemActive` rule already gives one step per drag), and extra **History-panel
accessibility** work. Revisit if a concrete need shows up.

---

## How to extend it today

- **Make a new scene mutation undoable (v1):** call `EditorStack::Get().MarkSceneEdited()` right after
  the mutation. The poll captures a snapshot when the active edit finishes. If your edit invalidates
  captured pointers elsewhere, that's already handled — snapshots hold no pointers.
- **A new `ImGuiEx::Property`-based field** is undoable automatically (the `#if UndoDo` hook).
- **Reset the history** if you swap the scene identity in new code: call
  `EditorLayer::ResetUndoHistory()` (as `NewScene` / `OpenScene` do).

See also: [Architecture → Undo/redo](Architecture.md), [Keybindings](Keybindings.md).
