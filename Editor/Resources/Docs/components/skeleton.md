# Skeleton

A Skeleton makes a rigged mesh follow its bones. Add it to an entity that has a [Static Mesh](static-mesh) whose model carries skin weights (a character or any other mesh exported with an armature). The mesh is deformed on the GPU each frame and drawn by every pass like any other mesh: it casts shadows, reflects, refracts through glass materials and gets outlined when selected.

## Getting a skeleton

Import a rigged model (glTF, or binary or text FBX, such as a Mixamo character). Right-click it in the Content Browser and choose **Extract Skeleton and Animations**. That writes a skeleton file and one animation file per clip next to the model and registers them.

## Settings

- **Skeleton** — the skeleton file the mesh is skinned to. Drag it from the Content Browser.
- **Enabled** (the header checkbox) — turn it off to draw the mesh unskinned.
- **Clip** — the animation file to play. Changing it while the clip plays fades from the old clip to the new one over **Fade Seconds**.
- **Graph** — an animation graph file (create one with **Create → Animation Graph** in the Content Browser). It replaces **Clip**: the graph chooses the clip from parameters that scripts set. See [Animation Graph](animation-graph).
- **Play On Start** — play when the scene starts. Off holds the pose at **Start Time** until a script or animation graph plays it; in the editor, changing **Start Time** scrubs the pose in the viewport.
- **Loop**, **Speed** — wrap at the end of the clip or hold the last pose; play faster, slower or backwards.
- **Root Motion** and **Root Bone** — while the scene runs, move the entity by the root bone's horizontal travel instead of animating the mesh in place. **Root Bone** names the bone; empty picks the first bone with a position track. In the editor the mesh is pinned in place and the entity never moves, so editing a scene does not change it.
- **Debug Bone** and **Debug Bend** — bend one bone by an angle to check that the mesh deforms (used when no clip is set).

## Notes

- A skinned mesh ignores its own scale and placement inside the model file and sits where the unskinned mesh would. Move, rotate and scale the entity instead.
- Clips and the skeleton must come from the same model file, or their bone names will not match.
- Until both the mesh and the skeleton have loaded, the mesh draws in its rest shape.
- Meshes that use skin weights need the skeleton's bone names to match the model's. Extracting both from the same file guarantees this.

## In scripts

`GetComponent<SkeletonComponent>()` gives a script control of the animation: `Playing`, `Time`, `Speed` and `Loop`; `CrossFade(clipHandle, seconds)` to switch clips; `PlayState("Walk")`, `SetFloat("Speed", v)`, `SetBool`, `SetTrigger("Jump")` and `CurrentState` when the mesh uses an [Animation Graph](animation-graph); `GetBoneTransform("Hand.R", out position, out rotation)` for a bone's world transform; and `AttachToBone(entity, "Hand.R")` to make another entity, such as a weapon, follow a bone. The members are listed in the [C# API reference](../api/SkeletonComponent).
