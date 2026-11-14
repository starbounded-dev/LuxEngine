# MeshColliderComponent

`public class MeshColliderComponent : Component`

A collider shaped like a mesh; also acoustic geometry.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `AcousticMotion` | `AcousticGeometryMode` | get, set | How the mesh takes part in ray-traced acoustics. Main thread only. |
| `Material` | `AcousticMaterial` | get | Surface override, or this collider's authored acoustic material. |

[All types](index)
