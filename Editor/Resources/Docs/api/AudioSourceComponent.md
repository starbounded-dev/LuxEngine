# AudioSourceComponent

`public class AudioSourceComponent : Component`

Plays an FMOD Studio event from the entity.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `DistanceCulling` | `bool` | get, set | Release instances beyond every listener's authored event range. Loops suspend and resume; inaudible one-shots are discarded. Default false. |
| `IsCulled` | `bool` | get | Whether [`AudioSourceComponent.DistanceCulling`](AudioSourceComponent) has released the event right now. Main thread only. |
| `IsPaused` | `bool` | get, set | Pauses or resumes the event from the script, on top of the scene's pause. |
| `IsPlaying` | `bool` | get | Whether the event is playing. |
| `OcclusionGain` | `float` | get | Low-frequency occlusion applied to this source's Occlusion parameter this frame: 1 is a clear path, 0 fully blocked. 1 outside Play and while culled. Read-only; useful for gameplay such as AI hearing. |
| `Pitch` | `float` | get, set | Pitch multiplier (1 is unchanged). |
| `Priority` | `int` | get, set | FMOD voice priority: 0 is highest, 256 is lowest. |
| `TimelinePosition` | `int` | get, set | The event's timeline position in milliseconds; setting it seeks. |
| `Volume` | `float` | get, set | Volume multiplier on top of the event's authored volume (1 is unchanged). |

## Methods

```
public float GetParameter(string name)
```

Reads one of the event's parameters.

```
public void Play()
```

Starts the event.

```
public void Restart()
```

Stops the event at once and starts it again.

```
public void SetEvent(string guidOrPath)
```

Assigns a different event; it plays on the next [`AudioSourceComponent.Play`](AudioSourceComponent).

- `guidOrPath`: The event's GUID or path (`event:/...`). An unknown event leaves the current one.

```
public void SetParameter(string name, float value)
```

Sets one of the event's parameters.

```
public void SetParameterLabel(string name, string label)
```

Sets a labeled parameter by its label.

```
public void Stop(bool allowFadeOut = true)
```

Stops the event.

- `allowFadeOut`: True lets the event's authored fade-out play; false cuts it.

[All types](index)
