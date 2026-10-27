# SpriteRendererComponent

`public class SpriteRendererComponent : Component`

Draws a textured or colored quad (2D).

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Color` | `Vector4` | get, set | Tint (RGBA, 0 to 1), multiplied with the texture. |
| `Texture` | `Texture2D` | get, set | The texture, or null for a plain colored quad. |
| `TilingFactor` | `float` | get, set | How many times the texture repeats across the quad. |

[All types](index)
