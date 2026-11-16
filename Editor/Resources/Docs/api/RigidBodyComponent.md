# RigidBodyComponent

`public class RigidBodyComponent : Component`

A 3D physics body (Jolt). Its shape comes from the entity's colliders. The body exists only while the scene runs; outside Play the values read as zero. Moving a body from a script works best in `OnFixedUpdate`.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `AngularVelocity` | `Vector3` | get, set | Spin in radians per second around each axis. |
| `IsSleeping` | `bool` | get | Whether the body is asleep (at rest and not simulated until something wakes it). |
| `LinearVelocity` | `Vector3` | get, set | Velocity in meters per second. |
| `Mass` | `float` | get, set | The mass in kilograms. |
| `Rotation` | `Vector3` | get, set | The body's world rotation as Euler angles in radians; setting it teleports the body. |
| `Translation` | `Vector3` | get, set | The body's world position; setting it teleports the body. |

## Methods

```
public void AddForce(Vector3 force, ForceMode mode = ForceMode.Force, bool wake = true)
```

Pushes the body through its center of mass.

- `force`: The vector, in world space; its unit depends on `mode`.
- `mode`: How the vector is applied.
- `wake`: Whether to wake the body if it is asleep.

```
public void AddForce(Vector3 force, Vector3 location, ForceMode mode = ForceMode.Force, bool wake = true)
```

Pushes the body at a world point, which can also spin it.

- `force`: The vector, in world space; its unit depends on `mode`.
- `location`: The world point to push at.
- `mode`: How the vector is applied.
- `wake`: Whether to wake the body if it is asleep.

```
public void AddTorque(Vector3 torque, bool wake = true)
```

Applies a torque in newton-meters (world axes).

- `wake`: Whether to wake the body if it is asleep.

```
public void SetGravityEnabled(bool enabled)
```

Turns gravity on or off for this body.

```
public void SetSleepState(bool sleep)
```

Puts the body to sleep (true) or wakes it (false).

[All types](index)
