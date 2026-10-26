# Static Mesh

A Static Mesh draws a 3D model. Create one with **Create 3D → Meshes** in the Scene Hierarchy's menu, or drag a mesh from the Content Browser into the Scene View.

## Settings

- **Mesh** — the model to draw. Meshes come from files you import (see [Content Browser and Assets](../editor/content-browser)).
- **Materials** — one slot per material of the model. Leave a slot empty to use the material that came with the file, or drop a material from the Content Browser to replace it for this entity only. **Edit** opens the slot's material in the Material Editor.
- The header checkbox hides the mesh without removing it.

## Materials

A material sets the surface: base colour, metalness, roughness, emission, normal map and more. Opaque, cutout (alpha-tested, for leaves and fences) and transparent materials are supported, and transparent materials can refract like glass. Edit materials in the **Material Editor**; changes show immediately in every scene that uses them.

## Performance

Distant meshes automatically draw simpler versions of themselves (levels of detail). They are built when the mesh loads and keep the model's silhouette, hard edges and texture seams. Nothing needs setting up.

A mesh can also give an entity a collision shape: add a **Mesh Collider** (see [Physics](physics)).
