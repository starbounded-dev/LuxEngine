# A5: C# animation API

Planning document for sub-issue #69 (M2 skeletal animation). What exists is described in `.claude/docs/Architecture-LuxEngine.md`; this records the decisions and the state of the work.

**Goal.** A script can play and cross-fade animation, drive an animation graph with parameters, read a bone's transform and make another entity follow a bone.

**Decisions**
| Decision | Choice | Why |
|---|---|---|
| Shape of the API | A `SkeletonComponent` class in `Lux`, found with `GetComponent<SkeletonComponent>()` like every component | The native struct is already named that, so the existing component registration finds the managed type; no new access pattern to learn |
| Playback | `Playing`, `Time`, `Speed`, `Loop` properties; `CrossFade(clip, seconds)`; `PlayState(state, seconds)`; `CurrentState` | Covers play, pause, scrub, speed, switch clip, switch graph state |
| Graph parameters | `SetFloat`, `SetBool`, `SetTrigger`, `GetFloat`, each returning false for an unknown name or a parameter of another type | Same rules as the native setters; no exceptions for a typo in a game script |
| Reading a bone | `GetBoneTransform(bone, out position, out rotation)` in world space, rotation as Euler radians like `TransformComponent` | Matches what scripts already read; comes from the pose evaluated once per frame |
| Attaching | `AttachToBone(entity, bone)` / `Detach(entity)`; the scene moves attached entities right after the animators advance, in running scenes only; the child keeps its scale | A weapon in a hand needs nothing more; applying it after the pose and before scripts and rendering keeps it from lagging a frame |
| Clip handles | `CrossFade` takes a `ulong` asset handle, not a typed asset field | A typed `AnimationClip` field needs a new script field kind through the script engine, the inspector and serialization; that is a separate change. Graph states cover the common case without handles |
| Errors | Main thread only, checked in every binding; a missing component logs and returns a default or false | Same contract as the audio API |

**State**
- Done and tested without a native host (`tests/animation/run_managed.py`): every member sends the right arguments, reads the answer, refuses bad input; the `OnAnimationEvent` bridge.
- Done: the native bindings (`AnimationScriptBindings.cpp`), the attachment system (`Scene::ApplyBoneAttachments`), the API reference regenerated.
- Verified: golden scene `LuxBipedAttach` (a ball follows a hand bone in Play).
- Not done: a typed clip field, calling the API from a running script project, Linux.
