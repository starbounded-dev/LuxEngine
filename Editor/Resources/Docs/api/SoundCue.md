# SoundCue

`public struct SoundCue`

A sound playing near the listener, for drawing visual sound cues.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Active` | `bool` | get | Whether the sound is playing (false: it ended). |
| `Category` | `AudioCategory` | get | The sound's category. |
| `Direction` | `Vector3` | get | Listener-relative direction: X right, Y up, Z forward. Zero for nonspatial sounds. |
| `Handle` | `ulong` | get | Identifies the sound across [`Accessibility.SoundEvent`](Accessibility) calls. |
| `Intensity` | `float` | get | Authored importance scaled by instance volume and linear cue range, not a loudness meter. |
| `Position` | `Vector3` | get | The sound's world position. |

[All types](index)
