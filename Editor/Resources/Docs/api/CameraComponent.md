# CameraComponent

`public class CameraComponent : Component`

A camera the game can render from.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `FarClip` | `float` | get, set | The distance to the farthest thing the camera draws, in meters. A perspective camera needs more than its near clip. |
| `FieldOfView` | `float` | get, set | The vertical field of view of a perspective camera, in degrees, above 0 and below 180. |
| `NearClip` | `float` | get, set | The distance to the nearest thing the camera draws, in meters. A perspective camera needs more than 0. |
| `Orthographic` | `bool` | get, set | Whether the camera is orthographic (flat, as 2D games use) instead of perspective. |
| `OrthographicSize` | `float` | get, set | The height of the view of an orthographic camera, in meters, above 0. |
| `Primary` | `bool` | get, set | Whether this is the camera the game renders from. |

[All types](index)
