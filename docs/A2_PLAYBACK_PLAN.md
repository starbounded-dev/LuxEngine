# A2: clip playback, blending, cross-fade and root motion

Planning document for sub-issue #66 (M2 skeletal animation). What exists is described in `.claude/docs/Architecture-LuxEngine.md`; this records the decisions and the state of the work.

**Goal.** A rigged mesh plays an animation clip in the editor and in an exported game, can cross-fade to another clip, and can carry its root motion on the entity.

**Decisions**
| Decision | Choice | Why |
|---|---|---|
| Where playback state lives | Extra fields on `SkeletonComponent` (Clip, Time, Speed, Loop, Playing, FadeSeconds, RootMotion, RootBone) | One component per rigged mesh, no new component path to wire; the animation graph (A3) will drive these fields |
| Where poses are evaluated | `Animation/AnimationPose` (pure functions: sample, blend, model pose, root motion), called from `Scene::SyncRenderSceneMirror` | Testable without a GPU; the render side already takes a palette per proxy |
| Where time advances | `AdvanceAnimators` at the top of the editor, simulation and runtime scene updates | A clip previews in the editor viewport; paused play freezes it |
| Cross-fade | Smoothstep weight over `FadeSeconds`, both clips keep advancing | No pop when switching; fade state is runtime-only |
| Root motion | Horizontal (model X, Z) travel of the root bone, removed from the pose and added to the entity transform | Works for any model-space up axis the skeleton root carries |
| Compressed clips | Own format, no dependency: `.lanim` version 2 drops keys the neighbours reproduce (Douglas-Peucker, 0.05 degrees / 0.1 mm) and quantizes the rest (16-bit times and vectors, smallest-three rotations); clips stay raw in memory; version 1 still loads | ACL is large for what clips this size need; LuxBiped Idle goes 57 KB to 2.8 KB, Walk 29 KB to 5 KB, worst error 0.07 degrees. ACL can replace it later behind the same Serialize |

**State**
- Done and headless-tested (`tests/animation/run_windows.py`): sampling, wrap and clamp, blend, model pose, root-motion offset and removal.
- Done, built: component fields and their YAML round trip, inspector, per-frame advance, pack collector adds the clip and its skeleton.
- Done: exported-game check (`tests/animation/run_export_rig.py`): the packaged Release and Dist players load the binary FBX LuxBiped, its skeleton and the Walk clip from `AssetPack.lap` and log "playing clip"; the glTF rig still passes.
- Done: compressed clips (`tests/animation/run_windows.py`: sizes, sampled error, edge cases; Walk golden within 0.007% of pixels; Dist export plays the compressed clip).
- Not done: Linux, a root-motion golden scene.
