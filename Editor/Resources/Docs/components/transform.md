# Transform

Every entity has a Transform: its **Translation** (position), **Rotation** and **Scale**, relative to its parent. An entity without a parent is placed in the world directly.

- Edit the values in the Properties panel, or drag the gizmo in the Scene View (**W** move, **E** rotate, **R** scale).
- The coloured **X**, **Y** and **Z** buttons reset that axis.
- Drag an entity onto another in the Scene Hierarchy to parent it. A child keeps its place in the world and then moves, rotates and scales with its parent.

Units are metres. +Y is up and the camera looks along -Z by default.

## From a script

```
Translation += new Vector3(0.0f, 1.0f * ts, 0.0f); // rise one metre per second
```

`Translation`, `Rotation` and `Scale` are properties of every [script](../scripting/csharp-scripts). `Rotation` is in radians.
