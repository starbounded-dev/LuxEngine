# AudioListenerComponent

`public class AudioListenerComponent : Component`

Where the game hears from. Up to eight listeners (split screen) are blended by [`AudioListenerComponent.Weight`](AudioListenerComponent); ray-traced acoustics use the heaviest one.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Active` | `bool` | get, set | Whether this listener is in use. |
| `AttenuationTarget` | `Entity?` | get, set | The entity distance attenuation is measured from (a third-person character, say) while the listener stays on the camera, or null. |
| `ListenerIndex` | `int` | get, set | The FMOD listener slot, 0 to 7. |
| `UseAttenuationTarget` | `bool` | get, set | Whether distance attenuation is measured from [`AudioListenerComponent.AttenuationTarget`](AudioListenerComponent). |
| `Weight` | `float` | get, set | How much this listener counts when blending listeners, 0 to 1. |

[All types](index)
