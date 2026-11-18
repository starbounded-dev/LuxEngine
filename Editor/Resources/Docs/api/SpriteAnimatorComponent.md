# SpriteAnimatorComponent

`public class SpriteAnimatorComponent : Component`

Plays the sprite sheet's animation clips on this entity's [`SpriteRendererComponent`](SpriteRendererComponent).

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Clip` | `string` | get | The current clip's name, empty for none. |
| `IsFinished` | `bool` | get | True once a clip that plays once has reached its last frame. |
| `IsPlaying` | `bool` | get | Whether a clip is playing. |
| `Speed` | `float` | get, set | Playback speed: 1 is the clip's own frames per second. |

## Methods

```
public void Play(string clip, bool restart = false)
```

Plays the named clip of the sprite renderer's sheet. A clip that is already playing keeps running unless `restart` is true; any other clip, or a stopped one, starts from its first frame.

```
public void Stop()
```

Stops on the current frame. [`SpriteAnimatorComponent.Play`](SpriteAnimatorComponent) starts again.

[All types](index)
