# The Editor at a Glance

The editor is made of dockable panels. Use **View → Workspaces** or **Ctrl+1–9** to switch between
**Layout**, **Shading**, **Audio**, **Scripting**, **Debugging** and your personal layouts, in menu order.
There is no workspace bar above the panels. The first five shortcuts select the default layouts
on a fresh setup; the remaining shortcuts select personal layouts. Saved workspace order is retained.
Drag panel tabs and splitters to arrange your tools; the arrangement adapts when you resize the
editor or move it between monitors.
Scene View's corner controls and diagnostic overlays stay within its panel. Readouts wrap as
space shrinks; enlarge Scene View when you need to inspect the full diagnostic readout.

**Default layouts** are protected starting points. Changes to them last for this session. To keep
an arrangement, choose **View → Workspaces → Save as Personal Layout...**, enter a name and create your personal copy.
**Personal layouts** remember their changes automatically and follow you between projects on this
computer. Switching layouts keeps your scene, selection, open documents and unsaved work.

**View → Workspaces** contains the active layout's rename, delete and reset actions, plus copying,
switching, undoing the last reset and restoring recently
deleted personal layouts. The latest 32 deletions remain recoverable. Existing saved layouts are
imported as personal layouts and their original files are kept.
Long layout names stay within the menu; hover a row to read its full name and shortcut.

Use **View → Panels** to browse panel categories or search by name. If nothing matches, choose
**Clear search** to return to the categories. Panels also appear in the command
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

## Log and asset diagnostics

The **Log** action bar contains Clear, Follow latest, severity toggles, Tags and Recent searches.
Secondary actions move into **More** as the panel narrows; Clear on Play is there too. **Ctrl+F**
focuses search while Log has focus. Search includes the complete message and tag. Clear empties
the retained message list. Scroll up to inspect older output; **Follow latest** resumes live
following. The footer shows matching and retained message counts.

Click a message to inspect its complete text. Copy and Close stay above its scrolling content,
and Escape closes the dialog. Right-click a row to copy its full or short message. Narrow panels
combine the severity, timestamp and tag above the message preview.

**View → Panels → Diagnostics → Asset Manager** shows registry, loaded and memory-only asset counts.
Search by path, type, status or handle; **Only loaded** filters the list. Narrow panels show path
and status rows; click a path for all metadata and Copy path. Sync asset thread, Reload loaded
assets and Save registry remain available in the action bar or More. Use Content Browser to
import or author assets.

## Settings and Help

Application Settings shows a resizable page list when there is room and a page selector when
narrow. Your selected page and preferred sidebar proportion are remembered in layouts. Changes
keep their existing immediate or restart behavior, described beside the controls.

Project Settings keeps **Save project settings** above its scrolling content. Choose General,
Runtime Export, Renderer, Audio, Scripting, Physics or Log from the section selector. Project
changes still apply immediately and save when the panel closes; an unsuccessful save keeps the
unsaved indication. Leaving Audio stops its dialogue preview.

**Scene Renderer** keeps Save, the quality selector and search above its scrolling settings.
Use **More → Reset Debug Views** to clear editor overlays. Individual quality categories remain
available below the renderer status. Narrow panels move resolution presets into **More** and
clip long property headings; hover to read them in full. Search opens matching cards temporarily.
A failed save keeps your settings in memory and displays an error; repair the destination and
choose **Save Renderer Settings** again.

**F1** opens the offline manual. **Topics** shows or hides the topic list; narrow panels switch
between the full-width list and page. Search covers page titles and text, and **Ctrl+F** focuses
it while Help has focus. Selecting a result returns to its page. Back, Forward and Start page
remain available in the action bar or **More**. Hover a clipped topic title to read it in full.

## Play, Simulate and Stop

The buttons in the middle of the title bar control the scene:

- **Play** runs the game: scripts, physics and audio start, and the Game View shows the primary camera.
- **Simulate** runs physics only, so you can watch objects settle while still using the editor camera.
- **Stop** returns to editing. Changes made while playing are thrown away, so you can experiment freely.

## Undo

**Ctrl+Z** undoes and **Ctrl+Shift+Z** (or **Ctrl+Y**) redoes edits to the scene and to settings. **View → Panels → Diagnostics → History** lists the steps.

History provides **Undo**, **Redo** and **Current state** actions. Selecting an applied edit
undoes back to just before that edit; selecting a future edit redoes through it. Long labels have
complete hover text. History is available in Edit mode.
