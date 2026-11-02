# Material Graphs

A material graph computes a material's inputs from nodes: noise, textures, maths, the surface's position. Use it for looks a texture alone cannot give, such as procedural rust, rock or wood, or a pattern that never repeats.

A graph adds to the standard material rather than replacing it. Everything in the material's **Properties** still applies; the graph sets only the inputs its **Material Output** node connects, and the others keep their values.

## Making one

Open a material in the [Material Editor](material-editor) and press **Make Graph Material** in the toolbar. Switch between **Properties** and **Graph** above the right-hand column. **Remove Graph** turns it back into a standard material.

## The canvas

- **Add a node:** right-click the canvas, press **Space**, or use **Add Node**. Type to search, then press **Enter** to add the first match.
- **Connect:** drag from an output (right) to an input (left). An input takes one link, so a new link replaces the old one. Drag a link into empty space to add a node already connected to it.
- **Preview a node:** select it and press **Preview This Node**. The preview shape then shows that node's output, as light over black, until you press **Stop Preview**. It recompiles once.
- **Edit a node:** select it. Its settings show on the right: values for inputs with nothing connected, its mode, its texture, and its parameter name.
- **Delete** removes the selection. **Ctrl+C**, **Ctrl+V** and **Ctrl+D** copy, paste and duplicate.
- **Comments** (from the node list) are boxes to group and label part of a graph. Set their text and colour on the right.
- **Fit** shows the whole graph. **Find in graph** jumps to a node by name or parameter. The minimap in the corner shows where you are.
- The toolbar counts the graph's nodes, texture samples and parameters, and lists any errors. A node with an error has a red border.

Every edit is one step on the undo history. When an edit changes what the graph computes, the editor recompiles it, which takes a moment. Moving nodes around does not recompile.

## Nodes

| Group | Nodes |
|---|---|
| **Input** | UV, UV 2, Vertex Color, World Position, Object Position (moves with the object), Normal, View Direction, Camera Position, Time |
| **Constant** | Float, Color, Vector |
| **Math** | Add, Subtract, Multiply, Divide, Power, Minimum, Maximum, Absolute, Floor, Fraction, Square Root, Sine, Cosine, Saturate, One Minus, Negate, Clamp, Step, Smoothstep, Mix, Remap |
| **Vector** | Dot Product, Cross Product, Length, Distance, Normalize, Reflect, Split, Combine |
| **Texture** | Sample Texture (**Color** or **Data**), Normal Map, Triplanar Texture, Bump |
| **Procedural** | Noise (**fBm**, **Multifractal**, **Ridged**, **Hybrid**), Voronoi (**F1**, **F2**, **Edge**), Color Ramp |
| **Function** | Material Function, Function Input (Float), Function Input (Vector) |

Maths nodes work on numbers and on vectors: connect a colour and they work on colours. A number connected to a vector input fills every component.

- **Noise:** **Detail** is how many octaves it adds up, **Roughness** how much each finer octave contributes, and **Lacunarity** how much finer each octave is.
- **Normal Map** and **Bump:** both give a normal for the Output's **Normal** input.
- **Color Ramp:** maps a 0–1 value to colours between stops.

## Material functions

Any graph material can be used inside another graph, to reuse a look such as a rust layer or a noise setup.

1. In the graph you want to reuse, add **Function Input** nodes for the values the user should set, and give each an **Input Name** and a default.
2. In another graph, add a **Material Function** node and pick that material as its **Function**.

The node gets one input per Function Input, and one output per **Material Output** input (Base Color, Roughness, and so on), carrying what the function's graph connects there. Changing the function updates every graph that uses it. A function cannot use itself.

## Vertex colours and a second UV set

**Vertex Color** reads colours painted on the mesh's vertices (in Blender, *Color Attributes*; the first set is used), and **UV 2** its second UV map, for example for a blend mask or a decal sheet. They are imported with the mesh. A mesh without them reads white and its first UV map.

## Parameters

Give a **Float**, **Color** or texture node a **Parameter** name to expose it. Exposed values appear under **Graph Parameters** in the material's **Properties**, and changing them is instant: no recompile.

They also show in the **Properties** panel under a mesh's material, for quick tweaks in the scene; changes there are saved to the material, so every object using it changes.

Every [instance](material-editor) of a graph material shares its graph, and can set its own parameter values. So one rust graph can make ten rusts in different colours.

## Limits

- A graph can carry 32 numbers' worth of parameters and textures: a colour takes 4, a number or texture 1.
- Area lights do not show [Advanced Shading](material-editor) layers.
- A Cutout graph material's shadow and depth pass see white vertex colours, so an Opacity driven by Vertex Color cuts out the visible surface but not its shadow.
