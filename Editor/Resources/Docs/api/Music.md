# Music

`public static class Music`

Scene-owned interactive music. All calls and callbacks run on the main thread. Author State labels, continuous Intensity (0–1), and Layer_name parameters (0–1) in FMOD.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `CurrentBar` | `int` | get | The current bar. |
| `CurrentBeat` | `int` | get | The beat within the current bar. |
| `Intensity` | `float` | get, set | The music's `Intensity` parameter, 0 to 1; kept across [`Music.Play`](Music) and transitions. |
| `IsPlaying` | `bool` | get | Whether music is playing. |

## Events

| Name | Type | Description |
|---|---|---|
| `Beat` | `Action<int, int>` | Raised on every beat of the playing music, with the bar and the beat in it. |
| `Marker` | `Action<string>` | Raised when the playing music passes a timeline marker, with the marker's name. |

## Methods

```
public static void Play(string eventRef)
```

Starts a music event as the bed, replacing what was playing.

- `eventRef`: The event's GUID or path (`event:/...`).

```
public static void PlayStinger(string eventRef)
```

Plays a short event over the music.

- `eventRef`: The event's GUID or path (`event:/...`).

```
public static void QueueTransition(string eventRef, MusicSync sync)
```

Replaces the bed on the main-thread notification of a boundary. NextSection uses markers beginning with "Section:". Use Studio transitions for sample-accurate musical timing.

```
public static void SetLayerEnabled(string layer, bool enabled)
```

Turns a layer on or off: sets the music's `Layer_name` parameter to 1 or 0.

```
public static void SetState(string state)
```

Sets the music's `State` labeled parameter (combat, explore...); kept across transitions.

```
public static void Stop(bool allowFadeOut = true)
```

Stops the music.

- `allowFadeOut`: True lets the event's authored fade-out play; false cuts it.

[All types](index)
