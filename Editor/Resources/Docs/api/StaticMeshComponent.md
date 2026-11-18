# StaticMeshComponent

`public class StaticMeshComponent : Component`

A mesh drawn at the entity. A mesh can have several material slots, one per material of the model; a script can swap the mesh, assign a material to a slot, or change a slot's values for this entity alone. [`Component.Enabled`](Component) shows or hides the mesh.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `MaterialCount` | `int` | get | The number of material slots the mesh has; 0 without a mesh. |
| `StaticMesh` | `StaticMesh?` | get, set | The mesh asset, or null when none is set. Assigning one that is not a static mesh does nothing. |

## Methods

```
public Material? GetMaterial(int slot)
```

The material a slot draws with: this entity's own, or the mesh's. Null for a slot the mesh does not have.

- `slot`: The slot, 0 to [`StaticMeshComponent.MaterialCount`](StaticMeshComponent) - 1.

```
public Color GetMaterialColor(int slot)
```

The color of a slot's material.

- `slot`: The slot, 0 to [`StaticMeshComponent.MaterialCount`](StaticMeshComponent) - 1.

```
public float GetMaterialValue(int slot, MaterialValue which)
```

A scalar value of a slot's material.

- `slot`: The slot, 0 to [`StaticMeshComponent.MaterialCount`](StaticMeshComponent) - 1.
- `which`: Which value to read.

```
public void SetMaterial(int slot, Material? material)
```

Makes a slot draw with a material, for this entity only. Null goes back to the mesh's own material.

- `slot`: The slot, 0 to [`StaticMeshComponent.MaterialCount`](StaticMeshComponent) - 1.
- `material`: The material asset.

```
public void SetMaterialColor(int slot, Color color)
```

Changes the color of a slot's material for this entity alone. The first change gives the entity its own copy of the material, so other entities that share the material keep theirs.

- `slot`: The slot, 0 to [`StaticMeshComponent.MaterialCount`](StaticMeshComponent) - 1.
- `color`: The new color; alpha is ignored.

```
public void SetMaterialValue(int slot, MaterialValue which, float value)
```

Changes a scalar value of a slot's material for this entity alone, like [`StaticMeshComponent.SetMaterialColor`](StaticMeshComponent). Values are limited to their range.

- `slot`: The slot, 0 to [`StaticMeshComponent.MaterialCount`](StaticMeshComponent) - 1.
- `which`: Which value to change.
- `value`: The new value.

[All types](index)
