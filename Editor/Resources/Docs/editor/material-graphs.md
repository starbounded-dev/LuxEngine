# Material Graphs

A material graph computes a material's inputs from nodes: noise, textures, maths, the surface's position. Use it for looks a texture alone cannot give, such as procedural rust, rock or wood, or a pattern that never repeats.

A graph adds to the standard material rather than replacing it. Everything in the material's **Properties** still applies; the graph sets only the inputs its **Material Output** node connects, and the others keep their values.

## Making one

Open a material in the [Material Editor](material-editor) and press **Make Graph Material** in the toolbar. Switch between **Properties** and **Graph** above the right-hand column. **Remove Graph** turns it back into a standard material.

## The canvas

- **Add a node:** right-click the canvas, press **Space**, or use **Add Node**. Type to search, then press **Enter** to add the first match.
- **Connect:** drag from an output (right) to an input (left). An input takes one link, so a new link replaces the old one. Drag a link into empty space to add a node already connected to it.
- **Edit a node:** select it. Its settings show on the right: values for inputs with nothing connected, its mode, its texture, and its parameter name.
- **Delete** removes the selection. **Ctrl+C**, **Ctrl+V** and **Ctrl+D** copy, paste and duplicate.
- **Comments** (from the node list) are boxes to group and label part of a graph. Set their text and colour on the right.
- **Fit** shows the whole graph. **Find in graph** jumps to a node by name or parameter. The minimap in the corner shows where you are.
- The toolbar counts the graph's nodes, texture samples and parameters, and lists any errors. A node with an error has a red border.

Every edit is one step on the undo history. When an edit changes what the graph computes, the editor recompiles it, which takes a moment. Moving nodes around does not recompile.

## Nodes

| Group | Nodes |
|---|---|
| **Input** | UV, World Position, Object Position (moves with the object), Normal, View Direction, Camera Position, Time |
| **Constant** | Float, Color, Vector |
| **Math** | Add, Subtract, Multiply, Divide, Power, Minimum, Maximum, Absolute, Floor, Fraction, Square Root, Sine, Cosine, Saturate, One Minus, Negate, Clamp, Step, Smoothstep, Mix, Remap |
| **Vector** | Dot Product, Cross Product, Length, Distance, Normalize, Reflect, Split, Combine |
| **Texture** | Sample Texture (**Color** or **Data**), Normal Map, Triplanar Texture, Bump |
| **Procedural** | Noise (**fBm**, **Multifractal**, **Ridged**, **Hybrid**), Voronoi (**F1**, **F2**, **Edge**), Color Ramp |

Maths nodes work on numbers and on vectors: connect a colour and they work on colours. A number connected to a vector input fills every component.

- **Noise:** **Detail** is how many octaves it adds up, **Roughness** how much each finer octave contributes, and **Lacunarity** how much finer each octave is.
- **Normal Map** and **Bump:** both give a normal for the Output's **Normal** input.
- **Color Ramp:** maps a 0–1 value to colours between stops.

## Parameters

Give a **Float**, **Color** or texture node a **Parameter** name to expose it. Exposed values appear under **Graph Parameters** in the material's **Properties**, and changing them is instant: no recompile.

Every [instance](material-editor) of a graph material shares its graph, and can set its own parameter values. So one rust graph can make ten rusts in different colours.

## Limits

- A graph can carry 32 numbers' worth of parameters and textures: a colour takes 4, a number or texture 1.
- With a **Cutout** material, the graph's **Opacity** cuts out the visible surface, but shadows and the depth pre-pass still use the standard opacity.
- Area lights do not show [Advanced Shading](material-editor) layers.
