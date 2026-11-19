# Game UI

Menus, HUDs and dialogs are made of entities. Create them from **Create UI** in the Scene Hierarchy's menu. The user interface is drawn over the finished frame in Play mode and in an exported game; it is not shown in the Scene View.

| Component | What it is |
|---|---|
| **UI Canvas** | The root of a user interface. It turns its **Reference Resolution** into screen pixels (see [Scaling](#scaling)). A scene can hold several canvases; a higher **Sort Order** draws over a lower one. |
| **UI Element** | A rectangle inside its parent (the canvas or another element). It has anchors, offsets and an optional layout. |
| **UI Image** | Fills the element's rectangle with a **Color**, or with a **Texture** tinted by it. |
| **UI Text** | Text in the element's rectangle: **Font**, **Color**, **Size** (a line's height in UI units), alignment and **Wrap**. |

The elements of a canvas are its child entities, in hierarchy order: a child draws over its parent, and a later sibling draws over an earlier one. Untick **Visible** to hide an element and everything under it.

## Scaling

The canvas works in *UI units*. A **Scale Mode** says how many pixels one unit is:

- **Scale With Screen** compares the screen to the **Reference Resolution** (1920 x 1080 by default). **Match Width / Height** picks which side wins when the shapes differ: 0 follows the width, 1 the height, 0.5 blends the two.
- **Fit Inside** keeps the whole reference area on screen. **Fill Screen** covers the whole screen with it.
- **Constant Pixel Size** is **Scale Factor** pixels per unit whatever the screen is.

## Anchors and offsets

An element's rectangle is set against its parent's. **Anchor Min** and **Anchor Max** are points on the parent, from (0, 0) at the bottom left to (1, 1) at the top right. **Offset Min** and **Offset Max** are distances in UI units from those points to the element's bottom-left and top-right corners.

- Both anchors at one point (a corner, an edge, the middle): the offsets give a fixed size that stays put as the screen changes shape. Anchors at (1, 0) and offsets (-110, 10) to (-10, 60) make a 100 x 50 box 10 units from the bottom-right corner.
- Anchors at (0, 0) and (1, 1): the offsets are margins, and the element stretches with its parent.

## Layout

A **Layout** on an element places its children and ignores their anchors, using each child's own size from its offsets.

- **Horizontal** runs children left to right, **Vertical** top to bottom, both with **Spacing** and **Padding**. **Align** sets where a child sits across the run. **Expand Children** shares the spare length equally.
- **Grid** fills rows from the top left with cells of **Cell Size**, **Columns** wide.

## Limits

This is the first part of the system. Buttons, sliders and other widgets, keyboard and controller navigation, the C# API and editing in the Game View follow. World-space UI is planned for a later release.
