# Skeleton

A Skeleton makes a rigged mesh follow its bones. Add it to an entity that has a [Static Mesh](static-mesh) whose model carries skin weights (a character or any other mesh exported with an armature). The mesh is deformed on the GPU each frame and drawn by every pass like any other mesh: it casts shadows, reflects, refracts through glass materials and gets outlined when selected.

## Getting a skeleton

Import a rigged model (glTF, or binary or text FBX, such as a Mixamo character). Right-click it in the Content Browser and choose **Extract Skeleton and Animations**. That writes a skeleton file and one animation file per clip next to the model and registers them.

## Settings

- **Skeleton** — the skeleton file the mesh is skinned to. Drag it from the Content Browser.
- **Enabled** (the header checkbox) — turn it off to draw the mesh unskinned.
- **Debug Bone** and **Debug Bend** — bend one bone by an angle to check that the mesh deforms. This is an editor-only check; animation playback arrives with the next animation feature.

## Notes

- A skinned mesh ignores its own scale and placement inside the model file and sits where the unskinned mesh would. Move, rotate and scale the entity instead.
- Until both the mesh and the skeleton have loaded, the mesh draws in its rest shape.
- Meshes that use skin weights need the skeleton's bone names to match the model's. Extracting both from the same file guarantees this.
