# MusicDirectorComponent

`public class MusicDirectorComponent : Component`

The scene's music director as authored in the inspector: what plays when the game starts. During the game, drive the music with [`Music`](Music).

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `InitialState` | `string` | get, set | The State label the music starts in; empty for none. |
| `Intensity` | `float` | get, set | The music's Intensity at the start, 0 to 1. |
| `PlayOnAwake` | `bool` | get, set | Whether the music starts when the game does. |

[All types](index)
