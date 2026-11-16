# RigidBody2DComponent

`public class RigidBody2DComponent : Component`

A 2D physics body (Box2D). Its shape comes from the entity's 2D colliders. The body exists only while the scene runs; outside Play the runtime values read as zero.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `AngularVelocity` | `float` | get, set | Spin in radians per second. |
| `GravityScale` | `float` | get, set | Multiplier on world gravity for this body. |
| `LinearVelocity` | `Vector2` | get, set | Velocity in meters per second. |
| `Mass` | `float` | get | The mass in kilograms, from the colliders' density and area. |
| `Position` | `Vector2` | get, set | The body's world position; setting it teleports the body. |
| `Rotation` | `float` | get, set | The body's angle in radians; setting it teleports the body. |
| `Type` | `RigidBody2DComponent.BodyType` | get, set | Static, dynamic (moved by forces) or kinematic (moved by velocity only). |

## Methods

```
public void ApplyForce(Vector2 force, bool wake)
```

Applies a force in newtons at the center of mass for this step.

- `wake`: Whether to wake the body if it is asleep.

```
public void ApplyForce(Vector2 force, Vector2 worldPosition, bool wake)
```

Applies a force in newtons at a world point for this step.

- `wake`: Whether to wake the body if it is asleep.

```
public void ApplyLinearImpulse(Vector2 impulse, bool wake)
```

Applies an instant change of momentum at the center of mass.

- `wake`: Whether to wake the body if it is asleep.

```
public void ApplyLinearImpulse(Vector2 impulse, Vector2 worldPosition, bool wake)
```

Applies an instant change of momentum at a world point (which can also spin the body).

- `wake`: Whether to wake the body if it is asleep.

```
public void ApplyTorque(float torque, bool wake)
```

Applies a torque in newton-meters for this step.

- `wake`: Whether to wake the body if it is asleep.

## BodyType

`public enum BodyType`

How a 2D body moves.

### Values

| Name | Value | Description |
|---|---|---|
| `Static` | 0 |  |
| `Dynamic` | 1 |  |
| `Kinematic` | 2 |  |

[All types](index)
