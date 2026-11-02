# Game View

The **Game View** shows the scene through its primary [camera](../components/camera), the way the player will see it. The **Scene View** is where you work; the Game View is the preview of the game.

## What it shows

- The scene through the camera marked **Primary**. Without one it says **No Camera**: add a Camera component to an entity and tick **Primary**.
- The project's renderer settings (**Project Settings → Renderer**), as the exported game uses them. Debug views, the grid, selection outlines and gizmos from the Scene View never appear here.

The Game View renders only while it is open and visible, so closing it costs nothing.

## While the game plays

**Play** opens the Game View and brings it to the front; **Stop** returns to the Scene View. While the game runs:

- Scripts read the keyboard, mouse and gamepads as in the exported game (see [Input](../scripting/input)).
- Subtitles, captions and visual sound cues are drawn over it, and **F10** opens the player's audio accessibility menu.
- Cameras take their aspect ratio from the Game View, so scripts see the same aspect as the player will.

Dock the Game View next to the Scene View to watch both at once: the Scene View keeps the editor camera, so you can fly around a running game while the Game View shows what the player sees.

## Opening it

The Game View is open by default. If you closed it, reopen it from the **View** menu, or press **Play**.
