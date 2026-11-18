# RaycastHit

`public struct RaycastHit`

What a [`Physics`](Physics) ray or shape cast hit.

## Fields

| Name | Type | Description |
|---|---|---|
| `Distance` | `float` | How far the ray or the center of the shape travelled to reach the hit, in meters. |
| `Entity` | `Entity` | The entity whose collider was hit. |
| `Normal` | `Vector3` | The surface direction at [`RaycastHit.Point`](RaycastHit), a unit vector pointing away from the collider. |
| `Point` | `Vector3` | Where the ray crossed the surface, or where the cast shape touched it, in world space. |

[All types](index)
