# EventInstance

`public class EventInstance`

An owned Studio event. Dispose on the main thread; scene teardown also releases it.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `IsPlaying` | `bool` | get | Whether the event is playing. |
| `IsValid` | `bool` | get | Whether the instance still exists (it was not disposed and Play has not stopped). |

## Events

| Name | Type | Description |
|---|---|---|
| `Marker` | `Action<string>` | Raised on the main thread when playback passes a timeline marker, with the marker's name. |
| `Stopped` | `Action` | Raised on the main thread when the event stops. |

## Methods

```
public void Dispose()
```

Stops the event at once and releases it. Call on the main thread.

```
public void Set3DAttributes(Vector3 position, Vector3 velocity, Vector3 forward, Vector3 up)
```

Places the event in the world.

- `position`: World position.
- `velocity`: Velocity in meters per second, for doppler.
- `forward`: The sound's forward direction.
- `up`: The sound's up direction.

```
public void SetParameter(string name, float value)
```

Sets one of the event's parameters.

```
public void SetPitch(float pitch)
```

Sets the pitch multiplier (1 is unchanged).

```
public void SetSnapshotIntensity(float intensity)
```

Normalized snapshot intensity, 0–1. This does not change event volume.

```
public void SetVolume(float volume)
```

Sets the volume multiplier (1 is unchanged).

```
public void Start()
```

Starts (or restarts) the event.

```
public void Stop(bool allowFadeOut = true)
```

Stops the event; the instance stays usable.

- `allowFadeOut`: True lets the event's authored fade-out play; false cuts it.

[All types](index)
