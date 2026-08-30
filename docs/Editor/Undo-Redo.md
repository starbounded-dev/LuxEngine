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

### 3. The history — snapshots

`CommitSceneSnapshot` serializes the editor scene (`SceneSerializer::SerializeToString`) and pushes
the **previous** committed state:

```cpp
snapshot = CaptureSceneSnapshot();
if (snapshot.empty() || snapshot == m_UndoBaseline) return;   // nothing changed (e.g. a settings-panel edit)
m_UndoStack.push_back(m_UndoBaseline);                         // (capped at s_MaxUndoDepth = 64)
m_RedoStack.clear();
m_UndoBaseline = snapshot;
```

`UndoSceneEdit` / `RedoSceneEdit` move the baseline between the two stacks and call
`RestoreSceneSnapshot`, which deserializes the YAML into a **fresh `Scene`** and retargets everything
(`AdoptEditorScene` — panels, viewport, renderer). Selection is UUID-based (`SelectionManager`), so
it survives a restore where the entity still exists and is pruned where it doesn't.

The `snapshot == m_UndoBaseline` guard means a completed edit that **didn't change the scene** (e.g. a
Scene-Renderer or Project-Settings field, which isn't part of the scene YAML) is a no-op — safe to
over-signal.

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
| **Create entity** (Create-menu items) | ✘ | Phase 2 |
| **Reparent** (drag in hierarchy) | ✘ | Phase 2 |
| Scene Renderer / Project settings | ✘ | not scene state — separate stack, Phase 6 |
| Material asset editor | ✘ | Phase 6 |
| Content Browser file ops | ✘ | Phase 6 |
| Play/Simulate edits | ✘ (by design) | history is Edit-mode only |

### Known limitations of v1 (addressed by the plan)

- **Cost:** each committed edit serializes the whole scene (O(scene size)). Fine for typical scenes;
  Phase 3 replaces it with granular per-change commands.
- **Granularity:** the unit is "the scene changed," so two unrelated edits in one non-interactive
  frame could coalesce. Rare in practice.
- **Selection/camera** aren't part of a snapshot, so undo doesn't restore what was selected
  (Phase 4).
- **No labels / grouping** yet — the Edit menu just says "Undo" (Phase 5).

---

## The phased completion plan

Each phase is independently shippable and leaves the editor working. Phases 3+ change the *storage*
model behind the same `Ctrl+Z` surface, so users see continuity while the internals get better.

### Phase 1 — Snapshot foundation ✅ (done, this change)

Whole-scene snapshots; the `EditorStack` signal + `ImGuiEx` hook; commit-on-edit-finish coalescing;
`Ctrl+Z` / `Ctrl+Shift+Z` / `Ctrl+Y` + Edit-menu items; reset on scene load; Edit-mode gating.
Coverage per the table above.

### Phase 2 — Complete scene-structural coverage

Raise the signal for the remaining **scene** mutations so nothing structural is missed:

- Entity **create** (every Create-menu item, and the root/context-menu "Create Empty").
- **Reparent** via drag-drop (`SetParent` in the two `AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")`
  sites).
- Content-Browser **drag-into-scene** instantiation.
- **Prefab** instantiate / apply / revert.
- Multi-select structural ops.

Mechanical: a `MarkSceneEdited()` at each mutation site (or, better, a single choke — see Phase 3).

### Phase 3 — Granular command model (replace snapshots for scale)

Introduce `EditorCommand { std::function<void()> Undo, Redo; std::string Label; }` and make
`EditorStack` a real command stack (it already has the shape). Capture **values**, keyed to stable
identity (entity `UUID` + component type + field), committed on `IsItemDeactivatedAfterEdit`.

- Field edits become a `SetFieldCommand` storing before/after **values** (no pointers, no whole-scene
  serialize).
- Structural ops become explicit commands (`CreateEntity`, `DestroyEntity` with a serialized entity
  blob for redo, `AddComponent`, `Reparent`).
- Keep a **snapshot fallback** for anything not yet modeled, so coverage never regresses.
- **Single choke point:** connect entt `on_construct` / `on_destroy` / `on_update` signals (gated so
  scene *loading* doesn't record) to auto-generate component commands — removing most per-site hooks.

This is the big performance win: undo cost becomes O(change), not O(scene).

### Phase 4 — Selection & view state

Store the selection (and optionally the editor-camera framing) with each command so undo restores
*what was selected*, matching user expectation. A command carries a `before`/`after` selection set;
`RestoreSelection` applies it through `SelectionManager`.

### Phase 5 — Transactions, labels & a History panel

- **Grouping:** a `ScopedTransaction("Paste 5 entities")` merges several commands into one labelled
  step.
- **Labels in the menu:** "Undo Move", "Redo Add Point Light" (ImGui menu already supports the text).
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
