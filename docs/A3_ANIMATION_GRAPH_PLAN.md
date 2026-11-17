# A3: animation graph (state machine)

Planning document for sub-issue #67 (M2 skeletal animation). What exists is described in `.claude/docs/Architecture-LuxEngine.md`; this records the decisions and the state of the work.

**Goal.** Gameplay sets parameters (speed, grounded, a jump trigger); a graph asset turns them into which clip plays, with cross-fades, on any rigged mesh, in the editor and in an exported game.

**Decisions**
| Decision | Choice | Why |
|---|---|---|
| Shape | One flat state machine per graph: parameters, states (one clip each, speed, loop), ordered transitions (conditions, exit time, fade, `Any` source) | Covers locomotion, one-shots and interrupts. Blend trees and layers are separate, later features; the file has a `Version` for them |
| File format | YAML text (`.lanimgraph`), the same text in asset packs | Diffs and merges; hand-editable until the graph editor panel exists (E8); the asset type, extension and icon were already declared |
| Where the logic lives | `Animation/AnimationGraph` (asset, `Validate`, `UpdateGraph`, parameter setters), no scene or YAML dependency in the runtime half | Tested headless in `run_windows.py`; the YAML half (`AnimationGraphYaml.cpp`) is tested by `RunAnimationGraphSelfTests` from the golden capture |
| How it drives playback | `SkeletonComponent::Graph`; `AdvanceAnimators` asks `UpdateGraph` once per update, a taken transition starts a cross-fade through the existing animator | One playback path for clips and graphs; A5 reads and sets parameters through `AnimatorState::Graph` |
| Transition rules | First matching transition in file order; at most one per update; triggers cleared only by the transition that tests them; a Trigger needs a `Triggered` condition and nothing else may use it | Predictable, no hidden priorities, no lost or duplicated triggers |
| Validation | A graph that does not validate is refused at load, save and pack time, with the reason logged | An exported game never plays a half-valid graph |
| Packing | `Scene::GetAssetList` collects the graph, every state clip and each clip's skeleton | A packed graph never ships without what it plays |

**State**
- Done and tested headless: parameters, all condition operators, exit time, `Any`, one transition per update, trigger consumption, setters refusing bad names, types and non-finite values, validation failures.
- Done: asset type wiring (importer, serializer, pack, Content Browser "Animation Graph" create item), component field `Graph` with serialization, inspector and pack collection, YAML round trip and bad-file tests.
- Verified: see the PR checklist for the golden scene `LuxBipedGraph` and the exported-game check (`run_export_rig.py --graph`).
- Not done: blend trees, layers and masks, a graph editor panel (E8), Linux.
