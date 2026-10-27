# BoxCollider2DComponent

`public class BoxCollider2DComponent : Component`

A box shape for the entity's 2D rigid body.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Density` | `float` | get, set | Mass per area, in kilograms per square meter. |
| `Friction` | `float` | get, set | Friction coefficient, usually 0 to 1. |
| `Offset` | `Vector2` | get, set | The box's center relative to the entity. |
| `Restitution` | `float` | get, set | Bounciness: 0 doesn't bounce, 1 bounces back fully. |
| `Size` | `Vector2` | get, set | Half the box's size, scaled by the entity's scale. |

[All types](index)
