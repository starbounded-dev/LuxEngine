# The Editor at a Glance

The editor is made of dockable panels. Use **View → Workspaces** or **Ctrl+1–9** to switch between
**Layout**, **Shading**, **Audio**, **Scripting**, **Debugging** and your personal layouts, in menu order.
There is no workspace bar above the panels. The first five shortcuts select the default layouts
on a fresh setup; the remaining shortcuts select personal layouts. Saved workspace order is retained.
Drag panel tabs and splitters to arrange your tools; the arrangement adapts when you resize the
editor or move it between monitors.

**Default layouts** are protected starting points. Changes to them last for this session. To keep
an arrangement, choose **View → Workspaces → Save as Personal Layout...**, enter a name and create your personal copy.
**Personal layouts** remember their changes automatically and follow you between projects on this
computer. Switching layouts keeps your scene, selection, open documents and unsaved work.

**View → Workspaces** contains the active layout's rename, delete and reset actions, plus copying,
switching, undoing the last reset and restoring recently
deleted personal layouts. The latest 32 deletions remain recoverable. Existing saved layouts are
imported as personal layouts and their original files are kept.

Use **View → Panels** to browse panel categories or search by name. Panels also appear in the command
palette (**Tools → Command Palette**, **Ctrl+Shift+P**). Hierarchy, Properties, Scene View and Game
View can each be opened or closed independently. **View → Workspaces** provides save errors and
retry/recovery actions. A damaged layout file is
preserved, and a previous good version can be recovered explicitly. Scene and asset files are
unaffected by layout recovery.

## The main panels

| Panel | What it is for |
|---|---|
| **Scene View** | Edit the scene with the editor camera: select, move, rotate and scale. See [Scene View and Camera](viewport). |
| **Game View** | The scene through its primary camera, as the game will see it. Play opens it. See [Game View](game-view). |
| **Scene Hierarchy** | Every entity in the scene. Drag to re-parent; use **Create** or right-click to create entities. |
| **Properties** | The components of the selected entity. **Add Component** adds more. |
| **Content Browser** | The project's assets: scenes, meshes, textures, materials, prefabs. See [Content Browser and Assets](content-browser). |
| **Log** | Messages from the engine and from your scripts (`Log.Info`, `Log.Warn`, `Log.Error`). |
| **Help** | This manual. **F1** opens it. |

## Entities and components

A scene is a list of **entities**. An entity is only a name and a [Transform](../components/transform); what it does comes from its **components**: a [Static Mesh](../components/static-mesh) draws a model, a [Rigidbody](../components/physics) moves it with physics, a [Script](../scripting/csharp-scripts) runs your code. Entities can be parented to each other: a child moves with its parent.

Most components have a checkbox in their header that switches them off without removing them. A whole entity can be switched off too, which also switches off its children.

Hierarchy search stays above the scrolling tree. **Ctrl+F** focuses it while the hierarchy has
focus. Hover a clipped entity name for its full name and ID. Properties wraps its empty/locked
messages and keeps **Add component** below the name so both remain usable in narrow panels.
Search the component menu by name or category; components already on every selected entity are
omitted. Adding a component or renaming multiple selected entities keeps the normal undo behavior.

Transform fields stack when the panel cannot fit all three axes. Click an axis label to reset
that axis, Ctrl-click its number to type, or right-click for Reset/Copy/Paste. Editing one axis of
a multi-selection preserves the other axes on each entity. Long property labels clip within their
column; hover to read the full label. Help and component settings remain at the section's edge.

## Play, Simulate and Stop

The buttons in the middle of the title bar control the scene:

- **Play** runs the game: scripts, physics and audio start, and the Game View shows the primary camera.
- **Simulate** runs physics only, so you can watch objects settle while still using the editor camera.
- **Stop** returns to editing. Changes made while playing are thrown away, so you can experiment freely.

## Undo

**Ctrl+Z** undoes and **Ctrl+Shift+Z** (or **Ctrl+Y**) redoes edits to the scene and to settings. **View → Panels → Diagnostics → History** lists the steps.
