# AudioPortalComponent

`public class AudioPortalComponent : Component`

Acoustic shutter and optional link between two AudioZone components. Main thread only. Open is 0 (closed) to 1 (open); geometry changes use the scene's update budget.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `BlendDistance` | `float` | get, set | Distance in meters over which the room's sound leaking through the opening fades. |
| `Enabled` | `bool` | get, set | Whether the portal is active. |
| `HalfExtents` | `Vector3` | get, set | Half the opening's size: X width, Y height, Z thickness. |
| `Material` | `AcousticMaterial` | get, set | What the shutter is made of. |
| `Open` | `float` | get, set | 0 closes the shutter (a shut door); 1 removes it. |
| `ZoneA` | `Entity?` | get, set | The audio zone on one side, or null. |
| `ZoneB` | `Entity?` | get, set | The audio zone on the other side, or null. |

[All types](index)
