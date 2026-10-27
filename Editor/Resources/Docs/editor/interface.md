# The Editor at a Glance

The editor is one window made of dockable panels. Drag a panel's tab to move it; **View → Reset Layout** puts everything back. Every panel can be shown or hidden from the **View** menu or found in the command palette (**Tools → Command Palette**, **Ctrl+Shift+P**).

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

**Ctrl+Z** undoes and **Ctrl+Shift+Z** (or **Ctrl+Y**) redoes edits to the scene and to settings. **View → History** lists the steps.
