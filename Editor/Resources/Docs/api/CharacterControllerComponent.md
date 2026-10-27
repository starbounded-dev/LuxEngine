# CharacterControllerComponent

`public class CharacterControllerComponent : Component`

A character that walks on the world instead of being pushed around by it (a Jolt virtual character): it climbs steps and slopes, rides moving platforms and stops at walls. It runs before each 3D physics step.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `IsGrounded` | `bool` | get | Whether the character stands on something walkable. |
| `LinearVelocity` | `Vector3` | get, set | The character's velocity in meters per second. Setting it replaces the velocity for the next physics step. |

## Methods

```
public void Jump(float power)
```

Jumps on the next physics step, if the character is grounded.

- `power`: The upward speed in meters per second.

```
public void Move(Vector3 displacement)
```

Moves the character this physics step, in meters (world space); call it from `OnFixedUpdate` with `speed * dt`. While gravity is on only the horizontal part counts. In the air it is ignored unless the controller allows air control.

```
public void SetGravityEnabled(bool enabled)
```

Turns gravity on or off for the character (off: it flies).

[All types](index)
