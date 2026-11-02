# AudioZoneComponent

`public class AudioZoneComponent : Component`

Scene-owned ambience and snapshot zone. Properties are main-thread only. Collider mode uses one box, sphere or capsule on this entity; its dimensions override the zone's.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `BlendDistance` | `float` | get, set | Distance in meters over which the zone fades in at its edge. |
| `Enabled` | `bool` | get, set | Whether the zone is active. |
| `FadeTime` | `float` | get, set | Seconds the zone takes to fade its ambience and snapshot in or out. |
| `HalfExtents` | `Vector3` | get, set | Half the size of a box zone. |
| `Offset` | `Vector3` | get, set | The zone's center relative to the entity. |
| `Priority` | `float` | get, set | Higher-priority zones claim the listener first where zones overlap. |
| `Radius` | `float` | get, set | The radius of a sphere zone. |
| `Shape` | `AudioZoneShape` | get, set | The zone's shape. |
| `Volume` | `float` | get, set | Ambience volume multiplier. |
| `Weight` | `float` | get | How much the zone is active right now, 0 to 1. |

## Methods

```
public void SetAmbience(string reference)
```

Assign a looping event GUID/path; an empty string clears it.

```
public void SetSnapshot(string reference)
```

Assign a snapshot GUID/path with exposed Intensity; an empty string clears it.

[All types](index)
