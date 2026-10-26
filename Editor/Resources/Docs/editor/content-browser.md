# Content Browser and Assets

The Content Browser shows the project's `Assets` folder. Everything a scene uses is an **asset**: meshes, textures, materials, scenes, prefabs, fonts and audio settings. Assets are tracked by an ID, so renaming or moving a file in the Content Browser does not break the scenes that use it. Move files inside the editor rather than in your file explorer.

## Bringing in files

Right-click empty space and choose **Import** to copy a file into the current folder (a mesh brings the textures it uses along), or copy files into the project's `Assets` folder yourself and choose **Refresh**.

- **Meshes:** glTF (`.gltf`, `.glb`), FBX and OBJ. Each mesh file becomes a mesh source, and its materials and textures are imported next to it. When several objects in the file share one mesh, every one of them appears.
- **Textures:** PNG and JPEG images.
- **Environment maps:** HDR (`.hdr`) images, used by a [Sky Light](../components/lights).

Distant objects automatically draw simpler versions of their meshes (levels of detail), built when the mesh loads. You do not need to make them yourself.

## Creating assets

Right-click empty space and use **New** to create a **Folder**, **Scene** or **Material**. Double-click a material to edit it in the **Material Editor**, a scene to open it, and a prefab to edit it (see [Prefabs](prefabs)).

## Using assets in a scene

- Drag a mesh into the Scene View to place it.
- Drag a texture or material onto the matching slot in the Properties panel.
- Drag a prefab into the Scene Hierarchy to add a copy of it.

Right-click an item for **Rename**, **Duplicate**, **Delete**, **Show In Explorer** and **Reload**.
