# Camera

A Camera is the player's view. While the game plays, the **primary** camera renders the Game View and the exported game. The Scene View always uses the editor camera instead.

## Settings

- **Primary** — the camera the game uses. Keep exactly one primary camera per scene; a camera on a switched-off entity is skipped.
- **Projection** — **Perspective** for 3D, with **Near** and **Far** clipping planes, or **Orthographic** for 2D, with a **Size** as well.
- **Fixed Aspect Ratio** — keep the camera's own aspect instead of following the window.

Create a camera with **Create Utility → Camera** in the Scene Hierarchy's menu, then move it with the gizmo. The Game View shows the result live.

## From a script

Move the camera's entity like any other with its `Translation` and `Rotation` (see [Transform](transform)). To follow the player, give the camera a script that copies the player's position each frame, or make the camera a child of the player.
