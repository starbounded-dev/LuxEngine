# Scene View and Camera

The Scene View shows the scene through the **editor camera**, which is not part of your game. It keeps showing the editor camera while you play; the Game View shows what the player sees.

## Moving the camera

The camera moves only while the mouse is over the Scene View.

| Input | Action |
|---|---|
| Hold **right mouse button** + **W A S D** | Fly forward, left, back, right |
| Hold **right mouse button** + **Q / E** | Fly down / up |
| Hold **right mouse button** + move the mouse | Look around |
| **Right mouse button** + scroll | Change the flying speed |
| **Shift** / **Ctrl** while flying | Faster / slower |
| **Alt** + left / middle / right drag | Orbit / pan / dolly |
| Scroll | Zoom |

## Selecting

Click an object to select it; it is selected in the Scene Hierarchy as well. Dragging a mesh from the Content Browser into the Scene View creates an entity with a [Static Mesh](../components/static-mesh). Dropping a scene opens it.

## Gizmos

| Key | Gizmo |
|---|---|
| **Q** | None |
| **W** | Move |
| **E** | Rotate |
| **R** | Scale |

Hold **Ctrl** while dragging a gizmo to snap: half a unit when moving, 45 degrees when rotating.
