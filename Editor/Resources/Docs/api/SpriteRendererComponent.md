# SpriteRendererComponent

`public class SpriteRendererComponent : Component`

Draws a textured or colored quad (2D).

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Color` | `Vector4` | get, set | Tint (RGBA, 0 to 1), multiplied with the texture. |
| `OrderInLayer` | `int` | get, set | The draw order inside the sorting layer: higher draws later, on top. |
| `SortingLayer` | `string` | get, set | The sorting layer's name, as listed in Project Settings > 2D. Setting a name the project does not have logs a warning. |
| `SpriteIndex` | `int` | get, set | The frame of the sprite sheet this sprite shows (the Frame in the Sprite Renderer's inspector). A [`SpriteAnimatorComponent`](SpriteAnimatorComponent) changes it while a clip plays. |
| `Texture` | `Texture2D` | get, set | The texture, or null for a plain colored quad. |
| `TilingFactor` | `float` | get, set | How many times the texture repeats across the quad. |

[All types](index)
