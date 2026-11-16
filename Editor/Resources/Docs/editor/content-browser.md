# Content Browser and Assets

The Content Browser shows the project's `Assets` folder. Everything a scene uses is an **asset**: meshes, textures, materials, scenes, prefabs, fonts and audio settings. Assets are tracked by an ID, so renaming or moving a file in the Content Browser does not break the scenes that use it. Move files inside the editor rather than in your file explorer.

**New** stays at the front of the toolbar. Navigation, filters, grid/list view, sorting and view
options move into **More** when space is limited. **Ctrl+F** focuses the search, which filters as
you type. The location button shows the current path; hover for the full path or click to choose
a parent folder. **Alt+Left/Right** goes back to a parent folder and forward again.

**Folders** shows or hides the resizable folder pane. Narrow panels replace it with **Browse
folders** in the toolbar or More menu, preserving your preferred pane width for when there is
room again. This choice belongs to the workspace. The footer remains below the item list and
shows the visible item count or selected asset details; hover clipped text to read it fully.
Thumbnail size remains available in View options when the footer slider cannot fit.

## Bringing in files

Right-click empty space and choose **Import** to copy a file into the current folder (a mesh brings the textures it uses along), or copy files into the project's `Assets` folder yourself and choose **Refresh**.

- **Meshes:** glTF (`.gltf`, `.glb`), FBX and OBJ. Each mesh file becomes a mesh source, and its materials and textures are imported next to it. When several objects in the file share one mesh, every one of them appears.
- **Textures:** PNG and JPEG images.
- **Environment maps:** HDR (`.hdr`) images, used by a [Sky Light](../components/lights).

Distant objects automatically draw simpler versions of their meshes (levels of detail), built when the mesh loads. You do not need to make them yourself.

## Creating assets

Right-click empty space and use **New** to create a **Folder**, **Scene** or **Material**. Double-click a material to edit it in the [Material Editor](material-editor), a scene to open it, and a prefab to edit it (see [Prefabs](prefabs)).

## Using assets in a scene

- Drag a mesh into the Scene View to place it.
- Drag a texture or material onto the matching slot in the Properties panel.
- Drag a prefab into the Scene Hierarchy to add a copy of it.

Right-click an item for **Rename**, **Duplicate**, **Delete**, **Show In Explorer** and **Reload**.

## Choosing an asset for a property

Property asset pickers show each asset's name and project-relative path, so assets with the same
name can be distinguished. Search matches the path as well as the name. **Enter** chooses the
first matching result while the search field is active; **Escape** cancels. Clear selection is
available when the property permits it. An open picker refreshes when registry metadata changes.
