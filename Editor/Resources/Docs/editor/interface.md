# The Editor at a Glance

The editor is made of dockable panels. Use the workspace tabs below the titlebar to switch between
**Layout**, **Shading**, **Audio**, **Scripting** and **Debugging**, or one of your personal layouts.
Drag panel tabs and splitters to arrange your tools; the arrangement adapts when you resize the
editor or move it between monitors.

**Default layouts** are protected starting points. Changes to them last for this session. To keep
an arrangement, click **Save as Personal Layout...**, enter a name and create your personal copy.
**Personal layouts** remember their changes automatically and follow you between projects on this
computer. Switching layouts keeps your scene, selection, open documents and unsaved work.

Right-click a workspace tab to duplicate it, rename/delete a personal layout, or reset its
arrangement. **View → Workspaces** also switches layouts, undoes the last reset and restores recently
deleted personal layouts. The latest 32 deletions remain recoverable. Existing saved layouts are
imported as personal layouts and their original files are kept.

Use **View → Panels** to browse panel categories or search by name. Panels also appear in the command
palette (**Tools → Command Palette**, **Ctrl+Shift+P**). Hierarchy, Properties, Scene View and Game
View can each be opened or closed independently. A failed save is shown in the workspace status;
**View → Workspaces** provides the error and retry/recovery actions. A damaged layout file is
preserved, and a previous good version can be recovered explicitly. Scene and asset files are
unaffected by layout recovery.

## The main panels

| Panel | What it is for |
|---|---|
| **Scene View** | Edit the scene with the editor camera: select, move, rotate and scale. See [Scene View and Camera](viewport). |
| **Game View** | The scene through its primary camera, as the game will see it. Play opens it. See [Game View](game-view). |
| **Scene Hierarchy** | Every entity in the scene. Drag to re-parent, right-click to create entities. |
| **Properties** | The components of the selected entity. **Add Component** adds more. |
| **Content Browser** | The project's assets: scenes, meshes, textures, materials, prefabs. See [Content Browser and Assets](content-browser). |
| **Log** | Messages from the engine and from your scripts (`Log.Info`, `Log.Warn`, `Log.Error`). |
| **Help** | This manual. **F1** opens it. |

## Entities and components

A scene is a list of **entities**. An entity is only a name and a [Transform](../components/transform); what it does comes from its **components**: a [Static Mesh](../components/static-mesh) draws a model, a [Rigidbody](../components/physics) moves it with physics, a [Script](../scripting/csharp-scripts) runs your code. Entities can be parented to each other: a child moves with its parent.

Most components have a checkbox in their header that switches them off without removing them. A whole entity can be switched off too, which also switches off its children.

## Play, Simulate and Stop

The buttons in the middle of the title bar control the scene:

- **Play** runs the game: scripts, physics and audio start, and the Game View shows the primary camera.
- **Simulate** runs physics only, so you can watch objects settle while still using the editor camera.
- **Stop** returns to editing. Changes made while playing are thrown away, so you can experiment freely.

## Undo

**Ctrl+Z** undoes and **Ctrl+Shift+Z** (or **Ctrl+Y**) redoes edits to the scene and to settings. **View → Panels → Diagnostics → History** lists the steps.
