# RaycastHit2D

`public struct RaycastHit2D`

What a [`Physics2D`](Physics2D) ray hit.

## Fields

| Name | Type | Description |
|---|---|---|
| `Distance` | `float` | How far the ray travelled to reach the hit, in meters. |
| `Entity` | `Entity` | The entity whose collider was hit. |
| `Normal` | `Vector2` | The surface direction at [`RaycastHit2D.Point`](RaycastHit2D), a unit vector pointing away from the collider. |
| `Point` | `Vector2` | Where the ray crossed the collider's surface, in world space. |

[All types](index)
