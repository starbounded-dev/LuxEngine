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
- Works in **Edit mode** on the **scene** you're editing.
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
push UndoCommand{ label, metaDelta, entityDeltas };   // (capped at s_MaxUndoDepth = 64)
baseline = current;
```

So each history step is **O(change)** in memory — a two-entity edit stores two small YAML blocks, not
the whole scene. (Commit still serializes the scene once to compute the diff; it's the stored *history*
that shrank, which is what bounds memory across 64 steps.)

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

`ResetUndoHistory()` rebaselines and clears both stacks at every point a snapshot's identity would no
longer apply: **`OnAttach`** (initial load), **`NewScene`**, **`OpenScene`**. Play/Simulate are
handled by the Edit-mode gate — undo/redo are inert outside Edit, and any signal raised there is
discarded.

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
| Scene Renderer / Project settings | ✘ | not scene state — separate stack, Phase 6 |
| Material asset editor | ✘ | Phase 6 |
| Content Browser file ops | ✘ | Phase 6 |
| Play/Simulate edits | ✘ (by design) | history is Edit-mode only |

Each undo step now carries a **label** — the Edit menu shows "Undo Move", "Undo Delete Entity",
"Redo Add Component", etc. The label comes from the edit that triggered the commit (the gizmo op,
the structural hook's string, or a generic "Edit" for field changes).

### Known limitations (addressed by the remaining plan)

- **Commit compute:** a commit still *serializes* the whole scene once to compute the diff (O(scene)),
  even though only the changed entities are *stored*. A future optimization could serialize just the
  affected entities (known from the selection), but the memory win — the thing that mattered across 64
  steps — is already done (Phase 3B).
- **Granularity:** the unit is "which entities changed," so two unrelated edits in one non-interactive
  frame could land in one step. Rare in practice.
- **Selection/camera** aren't part of a step, so undo doesn't restore what was selected (Phase 4).
- **No transaction grouping / History panel** yet (Phase 5).

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

### Phase 4 — Selection & view state

Store the selection (and optionally the editor-camera framing) with each command so undo restores
*what was selected*, matching user expectation. A command carries a `before`/`after` selection set;
`RestoreSelection` applies it through `SelectionManager`.

### Phase 5 — Transactions & a History panel

Labels themselves landed in Phase 3A; what remains:

- **Grouping:** a `ScopedTransaction("Paste 5 entities")` merges several commands into one labelled
  step (today each committed edit is one step).
- **History panel:** a dockable list of the stack with click-to-jump and per-step memory, following
  the panel recipe in [Extending](Extending.md#recipe-add-a-new-panel).

### Phase 6 — All subsystems

Give each editing subsystem its own command provider feeding the one `EditorStack`:

- **Material editor** — material-asset property commands (+ mark the asset dirty).
- **Scene post-processing / Scene Renderer** — scene-owned settings already ride scene snapshots;
  project-level renderer settings get their **own** stack (they're not scene state).
- **Project Settings** — a separate settings stack.
- **Content Browser** — filesystem ops (rename/move/delete) via a trash-backed, reversible model
  (delete → move to a `.trash`, undo → restore); the riskiest, so it lands last with explicit
  confirmations.
- **Node graphs / animation** (if/when added) — register their own providers.

### Phase 7 — Play / Simulate policy

Currently cleared on transitions. Optionally give Play mode a **separate transient stack** for
tweaks that is discarded on Stop, so experimenting during Play doesn't touch the edit-mode history.

### Phase 8 — Robustness & budget

- Memory **budget** for the stack (bytes, not just depth) with oldest-eviction.
- Snapshot **compression** while snapshots remain (Phases 1–2).
- **Round-trip tests** for serializer fidelity (serialize → deserialize → serialize is stable).
- Optional on-disk **undo journal** for crash recovery.

### Phase 9 — Polish

Undo/redo toasts, coalescing tuning per widget type, keyboard-repeat handling for held `Ctrl+Z`, and
accessibility of the History panel.

---

## How to extend it today

- **Make a new scene mutation undoable (v1):** call `EditorStack::Get().MarkSceneEdited()` right after
  the mutation. The poll captures a snapshot when the active edit finishes. If your edit invalidates
  captured pointers elsewhere, that's already handled — snapshots hold no pointers.
- **A new `ImGuiEx::Property`-based field** is undoable automatically (the `#if UndoDo` hook).
- **Reset the history** if you swap the scene identity in new code: call
  `EditorLayer::ResetUndoHistory()` (as `NewScene` / `OpenScene` do).

See also: [Architecture → Undo/redo](Architecture.md), [Keybindings](Keybindings.md).
</content>
