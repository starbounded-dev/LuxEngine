# Game View

The **Game View** shows the scene through its primary [camera](../components/camera), the way the player will see it. The **Scene View** is where you work; the Game View is the preview of the game.

## What it shows

- The scene through the camera marked **Primary**. Without one it says **No Camera**: add a Camera component to an entity and tick **Primary**. A scene that has a [UI canvas](../components/ui) and no camera (a menu) shows its interface over an empty frame instead.
- The [game UI](../components/ui) over the picture, laid out for the Game View's size.
- The project's renderer settings (**Project Settings → Renderer**), as the exported game uses them. Debug views, the grid, selection outlines and gizmos from the Scene View never appear here.

The Game View renders only while it is open and visible, so closing it costs nothing.
The no-camera guidance wraps to remain readable when the panel is narrow.

## Editing the UI

While the game is not running and the scene has a UI canvas, the Game View is a UI editor. Tick **Edit UI** (top left of the picture, on by default) and:

- **Click** an element to select it (the Hierarchy and Inspector follow); the one under the pointer is outlined faintly.
- **Drag** the selected element to move it, or drag one of the eight handles on its outline to resize it. Moves and resizes snap to whole UI units and change the element's **Offset Min** and **Offset Max**, never its anchors, so it keeps following its parent as the screen changes. Each drag is one step of undo.
- **Arrow keys** nudge the selected element by one unit, ten with **Shift**, while the Game View has the focus.
- An element placed by its parent's **Layout** shows an orange outline and cannot be dragged: change the layout, or the element's size in the Inspector.

Create elements from **Create UI** in the Scene Hierarchy's menu. Switch **Edit UI** off to see the interface without the outlines.

## While the game plays

**Play** opens the Game View and brings it to the front; **Stop** returns to the Scene View. While the game runs:

- Scripts read the keyboard, mouse and gamepads as in the exported game (see [Input](../scripting/input)).
- The game UI works with the mouse over the picture, and with the keyboard and a gamepad while the Game View has the focus (click it first); typing in the Inspector does not move a menu.
- Subtitles, captions and visual sound cues are drawn over it, and **F10** opens the player's audio accessibility menu.
- Cameras take their aspect ratio from the Game View, so scripts see the same aspect as the player will.

Dock the Game View next to the Scene View to watch both at once: the Scene View keeps the editor camera, so you can fly around a running game while the Game View shows what the player sees.

## Opening it

The Game View is open by default. If you closed it, reopen it from the **View** menu, or press **Play**.
