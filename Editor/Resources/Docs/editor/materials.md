# Materials

A **material** describes a surface: its colour, how metallic and rough it is, whether it glows, and the textures that add detail. Meshes draw with materials, and one material can be shared by any number of meshes and scenes. Edit a material and everything that uses it changes at once.

## Where materials come from

- **Imported with a mesh.** A glTF, FBX or OBJ file brings its materials and textures along; they are saved next to the mesh in the Content Browser.
- **Made by you.** Right-click empty space in the Content Browser and choose **New → Material**. A new material is a plain white, opaque surface.

Double-click a material to open it in the [Material Editor](material-editor).

## Using a material

A [Static Mesh](../components/static-mesh) has one **Materials** slot per material of its model. An empty slot uses the material that came with the model. Drag a material from the Content Browser onto a slot to replace it for that entity only; **Edit** next to a slot opens the material in the Material Editor.

## Opaque, cutout and transparent

| Kind | Use it for | How |
|---|---|---|
| **Opaque** | Most surfaces | The default. |
| **Cutout** | Leaves, fences, grates: hard-edged holes | **Alpha Mode → Cutout**. Pixels whose base colour alpha is below **Alpha Cutoff** are not drawn. The surface still lights and casts shadows like a solid one. |
| **Transparent** | Glass, water, smoke | Comes from the imported file (a glTF material with alpha **BLEND**). It has **Opacity** instead of Alpha Mode, and can refract like glass. |

A material made in the editor is opaque or cutout; there is no switch to make it transparent yet.

## The surface model

Materials are physically based: they describe what a surface is made of, and lighting does the rest.

- **Base Color** — the surface's own colour. For metals it is the colour of the reflection.
- **Metallic** — 0 for non-metals (wood, plastic, stone, skin), 1 for bare metal. Values in between are for blends such as dusty metal.
- **Roughness** — 0 is a mirror-like polish, 1 is completely matte.
- **Specular** — how much a non-metal reflects head-on. Leave it at 0.5 unless you are matching a measured material.
- **Emission** — light the surface gives off by itself, such as a screen or a lamp. It does not light other objects; use a [light](../components/lights) for that.

## Textures

Any of the values above can come from a texture instead. A texture slot multiplies or replaces its value:

| Map | Holds |
|---|---|
| **Base Color Map** | Colour, with alpha for cutout. |
| **Normal Map** | Small surface detail (bumps, scratches, seams) without extra geometry. |
| **Metallic Map**, **Roughness Map** | One channel each. |
| **Occlusion Map** | Ambient occlusion: darkens crevices. |
| **Height Map** | Greyscale height, used as a bump map. |
| **Emissive Map** | Where the surface glows. |

Many tools pack occlusion, roughness and metallic into one "ORM" texture (R, G and B). Use the same texture in all three slots: each slot's **Channel** already defaults to that layout (occlusion R, roughness G, metallic B). Change the channels for textures packed differently.

**Tiling**, **Offset** and **Rotation** in the **UV** section move every map across the surface together, for example to repeat a tile texture across a large floor.

## Sound

A material can also say what a surface sounds like (**Acoustic Material**). Mesh colliders on that geometry use it for ray-traced acoustics and footsteps; see [Audio](../components/audio).
