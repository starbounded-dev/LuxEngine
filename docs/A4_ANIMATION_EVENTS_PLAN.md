# A4: animation events to C# and to audio

Planning document for sub-issue #68 (M2 skeletal animation). What exists is described in `.claude/docs/Architecture-LuxEngine.md`; this records the decisions and the state of the work.

**Goal.** A clip can mark moments (a footstep, a hit frame). When the playhead passes one in a running scene, the entity's script is told and, if the event names a Sound, it plays at the entity.

**Decisions**
| Decision | Choice | Why |
|---|---|---|
| Where events are stored | On the clip, in `.lanim` version 3 (the compressed layout followed by the events); versions 1 and 2 still load | A clip carries its events through export and reuse; no extra asset to collect |
| How they are authored | `<model>.events.yaml` beside the model, baked into the clips when they are extracted | Imported clips come from the model file, which has no events. A text sidecar survives re-extraction, diffs, and the animation tools (E8) will edit it |
| What an event holds | Time in seconds, Name, optional Argument string, optional Sound asset handle | Enough for footsteps and hit frames; the Argument carries which foot or which weapon |
| When one fires | The playhead passes it: (before, after] forward, [after, before) backward, across loop wraps in play order; the first advance of a clip also fires time 0; a held (not playing) animator fires nothing; the clip being faded out does not fire | Exactly once per pass, no double fire at a boundary, no sound while paused or scrubbing |
| Where they are delivered | The scene update only (running scenes), never the editor preview, so editing is silent | Matches root motion's rule |
| To C# | `OnAnimationEvent(string name, string argument)` on the entity's script, matched through the `OnAnimationEventInternal` bridge, under the collision hooks' rules (enabled, active, created) | Same pattern as the collision hooks |
| To audio | `PhysicsAudioSystem::OneShot` plays the Sound at the entity's world position, kept in the system's transient list so it is cleaned up | Reuses the one-shot voice handling the footsteps use |
| Packing | `Scene::GetAssetList` collects each packed clip's event sounds | An exported clip never plays a Sound that was not shipped |

**State**
- Done and tested headless: the sidecar parser (unknown clips, late events, bad files), the v3 round trip (v1 drops events), truncated files refused, the interval rules in every direction and across wraps, the animator firing footsteps in order over 2.5 loops, nothing fired while held.
- Done: the pack collector, the C# hook (`Entity.OnAnimationEvent`), the sound one-shot, the Walk clips of the sample rig carrying footsteps.
- Verified: the exported Release and Dist players log the footstep event (`run_export_rig.py`).
- Not done: authoring events in the editor (E8), an event-to-script check inside a running script (needs a built script project), Linux.
