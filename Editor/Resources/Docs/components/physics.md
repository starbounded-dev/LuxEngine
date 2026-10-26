# Physics

Physics runs while the game plays or while you **Simulate** (see [The Editor at a Glance](../editor/interface)). Two kinds of components work together: **colliders** give an entity a shape, and a **Rigidbody** decides how it moves.

## Colliders

| Collider | Shape |
|---|---|
| **Box Collider** | A box: **Half Size** and **Offset** |
| **Sphere Collider** | A sphere: **Radius** and **Offset** |
| **Capsule Collider** | A capsule, good for characters: **Radius**, **Half Height** |
| **Mesh Collider** | The shape of a mesh, for level geometry |
| **Compound Collider** | Combines the colliders of an entity's children into one body |

Each collider has a material: **Friction** (how much it grips), **Restitution** (how much it bounces) and **Density**. A Mesh Collider that uses the full triangle mesh only works on a static body; the engine makes such a body static and says so in the Log.

## Rigidbody

An entity with a collider and **no** Rigidbody is static: walls and floors that never move. Add a **Rigidbody** to choose:

- **Body Type** — **Static** never moves; **Dynamic** falls and is pushed around; **Kinematic** is moved by your scripts or animation and pushes dynamic bodies, but nothing pushes it.
- **Mass**, **Linear Drag** and **Angular Drag**.
- **Disable Gravity**, **Is Trigger** (detects overlaps without colliding), locked axes, and **Collision Detection** (use continuous for fast, small objects).

## Character Controller

For a player that walks: a **Character Controller** climbs steps (**Step Offset**) and slopes up to **Slope Limit**, and is moved from a script with `Move` and `Jump`.

## From a script

```
RigidBodyComponent body = GetComponent<RigidBodyComponent>();
body.AddForce(new Vector3(0.0f, 10.0f, 0.0f), ForceMode.Impulse);

CharacterControllerComponent controller = GetComponent<CharacterControllerComponent>();
if (controller.IsGrounded && Input.IsKeyPressed(KeyCode.Space))
	controller.Jump(5.0f);
```

`OnCollisionBegin` and `OnCollisionEnd` tell a [script](../scripting/csharp-scripts) when its entity touches another.
