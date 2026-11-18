# E8: animation editor tools

Planning document for sub-issue #119 (M2 skeletal animation). What exists is described in `.claude/docs/Architecture-LuxEngine.md`; this records the decisions and the state of the work.

**Goal.** In the editor, see a skeleton, preview a clip and test an animation graph without running the game.

**Decisions**
| Decision | Choice | Why |
|---|---|---|
| Where the tools live | Under the Skeleton section of the inspector, not a new dock panel | The entity and component are already selected there; no new panel to register, persist or dock, and nothing new to keep in step with the selection |
| Skeleton view | Bones as viewport lines for every selected skinned entity (`Scene::CaptureSkeletonLines`, into the frame packet like the audio zone lines), from the animated pose or the rest pose | Editor overlay only, zero cost with nothing selected, checkable in a golden capture with `Select=` |
| Clip preview | Play, Pause and a time slider writing the runtime `AnimatorState` (`PreviewOverride`), never the saved component; editing an authored setting hands the preview back | Previewing must not dirty or move anything, as for root motion |
| Graph view | States (current one marked), transitions with their conditions, and live parameter widgets writing the runtime parameter values | Reads and tests the state machine; building graphs by hand in YAML stays until a node editor is justified |
| Node graph editor | Not built. The graph file is text and the view shows everything in it | A node editor is its own feature (layout, undo, persistence); the material editor shows the cost |
| Checking the UI | ImGui scope and ID audit by reading (every `Push` has a `Pop`, `TreeNode` a `TreePop`, unique `##` labels, per-item `PushID`), the bone overlay in a golden capture; the widgets themselves were not driven in a running editor | The rules forbid launching the editor for a click test without an explicit go-ahead |

**State**
- Done: bone overlay (verified in the golden capture: bones follow the walk pose), preview transport, graph view and parameter test widgets, docs.
- Not done: driving the inspector widgets in a running editor, a node graph editor, a dedicated event editor, Linux.
