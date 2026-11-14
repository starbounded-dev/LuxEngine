# Entity

`public class Entity`

An object in the running scene, and the base class of every entity script.

A script derives from `Entity` and the engine calls its hooks by name, so they need no `override`: `void OnCreate()` once when the entity starts (or is spawned), `void OnUpdate(float ts)` every frame with the frame time in seconds, `void OnFixedUpdate(float dt)` before every 3D physics step with the fixed timestep, and `void OnDestroy()` when it goes away. The collision and trigger hooks are overridden instead. An `Entity` is a handle: two instances with the same [`Entity.ID`](Entity) are the same entity. A public field of type `Entity` is set in the inspector.

## Fields

| Name | Type | Description |
|---|---|---|
| `ID` | `ulong` | The entity's unique ID (its UUID), stable across saves. |

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Children` | `Entity[]` | get | The entity's direct children, in hierarchy order. |
| `Name` | `string` | get, set | The entity's name, as shown in the Scene Hierarchy. |
| `Parent` | `Entity` | get, set | The parent entity, or null for a root. Setting it keeps [`Entity.Translation`](Entity), [`Entity.Rotation`](Entity) and [`Entity.Scale`](Entity) as they are, so they become relative to the new parent; null detaches the entity. A change that would make the entity its own ancestor is ignored. |
| `Rotation` | `Vector3` | get, set | Rotation relative to the parent, as Euler angles in radians. |
| `Scale` | `Vector3` | get, set | Scale relative to the parent. |
| `Translation` | `Vector3` | get, set | Position relative to the parent (to the world, without one), in meters. |

## Methods

```
public T AddComponent<T>() where T : Component, new()
```

Adds a component of type `T` with its default settings.

Returns: The new component.

```
public T As<T>() where T : Entity, new()
```

The script running on this entity, as script class `T`.

Returns: The script, or null when the entity has no script or its script is not a `T`.

```
public static Entity Create(string name = "Entity")
```

Creates an empty entity (a transform only) at the scene's root.

- `name`: The new entity's [`Entity.Name`](Entity).

```
public void Destroy()
```

Destroys this entity and its children at the end of the current update, so a script may destroy itself.

```
public static void Destroy(Entity entity)
```

Destroys an entity at the end of the current update, like [`Entity.Destroy`](Entity). Null is ignored.

```
public Entity FindEntityByName(string name)
```

Finds an entity in the scene by its [`Entity.Name`](Entity).

Returns: An entity with exactly that name, or null.

```
public T GetComponent<T>() where T : Component, new()
```

The entity's component of type `T`.

Returns: The component, or null when the entity has none.

```
public bool HasComponent<T>() where T : Component, new()
```

Whether the entity has a component of type `T`.

```
public static Entity Instantiate(Prefab prefab)
```

Spawns an instance of a prefab where the prefab was saved.

Returns: The instance's root, or null when `prefab` is unset or missing. Its scripts' `OnCreate` has run when this returns.

```
public static Entity Instantiate(Prefab prefab, Vector3 position, Vector3 rotation = default)
```

Spawns an instance of a prefab at a position and rotation.

- `prefab`: The prefab to spawn.
- `position`: The root's position.
- `rotation`: The root's rotation, Euler angles in radians like [`Entity.Rotation`](Entity).

Returns: The instance's root, or null when `prefab` is unset or missing. Its scripts' `OnCreate` has run when this returns.

```
public void RemoveComponent<T>() where T : Component, new()
```

Removes the entity's component of type `T`, if it has one.

```
protected virtual void OnCollisionBegin(Entity other)
```

Called when this entity's body starts touching another one (3D or 2D). Both entities' scripts are told, and a character controller touching static geometry counts.

- `other`: The entity on the other side of the contact.

```
protected virtual void OnCollisionEnd(Entity other)
```

Called when a contact reported by [`Entity.OnCollisionBegin`](Entity) ends.

- `other`: The entity on the other side of the contact.

```
protected virtual void OnTriggerBegin(Entity other)
```

Called when a 3D trigger (a rigid body with Is Trigger set) and another body start overlapping. Both entities' scripts are told; character controllers count.

- `other`: The entity on the other side of the overlap.

```
protected virtual void OnTriggerEnd(Entity other)
```

Called when an overlap reported by [`Entity.OnTriggerBegin`](Entity) ends.

- `other`: The entity on the other side of the overlap.

[All types](index)
