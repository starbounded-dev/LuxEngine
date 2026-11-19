# Game UI

Menus, HUDs and dialogs are made of entities. Create them from **Create UI** in the Scene Hierarchy's menu. The user interface is drawn over the finished frame in Play mode and in an exported game; it is not shown in the Scene View.

| Component | What it is |
|---|---|
| **UI Canvas** | The root of a user interface. It turns its **Reference Resolution** into screen pixels (see [Scaling](#scaling)). A scene can hold several canvases; a higher **Sort Order** draws over a lower one. |
| **UI Element** | A rectangle inside its parent (the canvas or another element). It has anchors, offsets and an optional layout. |
| **UI Image** | Fills the element's rectangle with a **Color**, or with a **Texture** tinted by it. **Slice Border** (left, bottom, right, top, in texels of the texture) turns it into a 9-slice: the corners keep their size (drawn at **Slice Scale** UI units per texel), the edges and the middle stretch. |
| **UI Text** | Text in the element's rectangle: **Font**, **Color**, **Size** (a line's height in UI units), alignment and **Wrap**. |
| **UI Widget** | Makes the element a [button, toggle, slider, progress bar, text input or scroll list](#widgets). |

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

## Widgets

Add a **UI Widget** to an element, or use **Create UI**, which builds a ready one. The widget takes its look from the element's **UI Image** (the background, track or box) and **UI Text**, and tints the image by state with **Hover Tint**, **Pressed Tint** and **Disabled Tint**. Untick **Interactable** to turn a widget off. A widget at the top of the draw order under the pointer takes the mouse; ones behind it do not.

| Kind | Behaviour |
|---|---|
| **Button** | A click is a press and a release on the button. |
| **Toggle** | A click flips **Checked**; the check mark is drawn in **Accent Color**. |
| **Slider** | Dragging sets **Value** between **Min** and **Max** (whole steps with **Whole Numbers**). The fill and the handle are drawn in **Accent Color**. |
| **Progress Bar** | Draws **Value** between **Min** and **Max** as a fill in **Accent Color**. Scripts set it; the pointer does not. |
| **Text Input** | A click focuses it. Typing, Backspace, Delete, Left, Right, Home and End edit the element's **UI Text** (up to **Max Length** bytes); Enter submits and Escape or a click elsewhere ends editing. **Placeholder** shows in half strength while it is empty. Give the element a **UI Text** with **Wrap** off. |
| **Scroll List** | Lays out its children with its **Layout** (usually Vertical) in a content area taller than itself and scrolls it with the mouse wheel. Children are cut at the list's edge; a line of text is drawn only while all of it is inside. |

## Limits

Everything is screen space. Keyboard and controller navigation, the C# API and editing in the Game View are the next parts. A widget cannot be hidden from the pointer by a plain panel above it, only by another widget. World-space UI, rich text and tweens are planned for a later release.
