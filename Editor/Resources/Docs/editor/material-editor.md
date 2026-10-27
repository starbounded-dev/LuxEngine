# Material Editor

The Material Editor edits [materials](materials). Open a material by double-clicking it in the Content Browser, or with **Edit** next to a material slot in the Properties panel.

## The window

- **Tabs** — each open material has a tab. A dot on the tab means unsaved changes. Closing a tab with unsaved changes asks whether to **Save** or **Discard**.
- **Preview** — the material on a shape under neutral lighting. Drag to orbit and scroll to zoom; pick the shape (**Sphere**, **Cube**, **Cylinder**, **Plane** or **Torus**) below it, and **Reset view** to go back.
- **Properties** — the material's settings, in collapsible sections.
- **Save** writes the material to disk; **Revert** goes back to the saved version. Every change is one step on the editor's undo history (**Ctrl+Z**), and dragging a slider counts as a single step.

Changes show at once in the Scene View and the Game View, in every scene that uses the material, even before you save.

## Sections

| Section | Settings |
|---|---|
| **Surface** | **Base Color**, **Metallic**, **Roughness**, **Specular**; **Alpha Mode** and **Alpha Cutoff** for opaque materials, **Opacity** for transparent ones. |
| **Emission** | **Emissive Color**, **Intensity** (0 turns emission off) and an **Emissive Map**. |
| **Refraction** | Transparent materials only. **Transmission** is how much light passes through and bends (keep **Opacity** at 1 for clear glass), **IOR** how strongly it bends (1.33 water, 1.5 glass), **Thickness** how far light travels inside (0 for a thin sheet), and **Attenuation Color** and **Distance** for tinted glass. |
| **Translucency** | Opaque materials only. **Transmission** lets light through to the far side, like a leaf lit from behind, tinted by **Transmission Color** and masked by a **Transmission Map**. |
| **Texture Maps** | The texture slots, **Normal Strength** (0 flattens the normal map), the channel each packed map is read from, **Occlusion Strength**, and **Bump Height** for the height map in metres. |
| **UV** | **Tiling**, **Offset** and **Rotation** (degrees) for every map. |
| **Rendering** | **Cast Shadows**, and **Two Sided** for single-sheet geometry such as leaves and cloth. |
| **Acoustics** | **Acoustic Material**: what surfaces with this material sound like. **None** leaves each collider's own setting. |

Settings with help text show it when you hover their name.
