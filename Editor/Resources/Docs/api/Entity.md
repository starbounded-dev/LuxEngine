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
| `Active` | `bool` | get, set | Whether the entity is switched on, the inspector's checkbox. Switching it off also switches off every child: renderers, lights, audio sources and scripts skip it, and its scripts get no `OnUpdate`. It takes effect at once. Read [`Entity.ActiveInHierarchy`](Entity) to know whether it is on in effect. |
| `ActiveInHierarchy` | `bool` | get | Whether the entity and every one of its parents are switched on. |
| `Children` | `Entity[]` | get | The entity's direct children, in hierarchy order. |
| `Forward` | `Vector3` | get | The direction the entity faces in the world: -Z rotated by [`Entity.WorldRotationQuaternion`](Entity). |
| `Name` | `string` | get, set | The entity's name, as shown in the Scene Hierarchy. |
| `Parent` | `Entity` | get, set | The parent entity, or null for a root. Setting it keeps [`Entity.Translation`](Entity), [`Entity.Rotation`](Entity) and [`Entity.Scale`](Entity) as they are, so they become relative to the new parent; null detaches the entity. A change that would make the entity its own ancestor is ignored. |
| `Persistent` | `bool` | get, set | Whether the entity survives [`Scene.Load`](Scene): it moves to the new scene with its children, its script (which keeps running, without a new `OnCreate`) and the script's fields. Its physics body, audio and animation start again in the new scene. A persistent child is carried as a root, where it stands in the world. Set it from `OnCreate`. |
| `Right` | `Vector3` | get | The entity's right in the world: +X rotated by [`Entity.WorldRotationQuaternion`](Entity). |
| `Rotation` | `Vector3` | get, set | Rotation relative to the parent, as Euler angles in radians. |
| `RotationQuaternion` | `Quaternion` | get, set | Rotation relative to the parent as a [`Quaternion`](Quaternion); the same rotation as [`Entity.Rotation`](Entity). |
| `Scale` | `Vector3` | get, set | Scale relative to the parent. |
| `ScriptEnabled` | `bool` | get, set | Whether this entity's script runs. A script switched off gets no `OnUpdate` or `OnFixedUpdate`; its `OnCreate` waits until it is first switched on. False for an entity with no script. |
| `Tag` | `string` | get, set | The entity's tag: free text set in the inspector (or here) that scripts use to find a group of entities with [`Entity.FindWithTag`](Entity). Empty when the entity has none. Not the same as [`Entity.Name`](Entity). |
| `Translation` | `Vector3` | get, set | Position relative to the parent (to the world, without one), in meters. |
| `Up` | `Vector3` | get | The entity's up in the world: +Y rotated by [`Entity.WorldRotationQuaternion`](Entity). |
| `WorldRotationQuaternion` | `Quaternion` | get | Rotation in the world, through every parent. Read only. |
| `WorldTranslation` | `Vector3` | get | Position in the world, through every parent. Read only: set [`Entity.Translation`](Entity) to move the entity. |

## Methods

```
public T AddComponent<T>() where T : Component, new()
```

Adds a component of type `T` with its default settings.

Returns: The new component.

```
public Timer After(float seconds, Action action, bool unscaled = false)
```

Runs `action` once, `seconds` from now. It is cancelled when this entity is destroyed and waits while the entity is switched off.

- `seconds`: The delay.
- `action`: What to run.
- `unscaled`: Count real time instead of game time, so [`Time.TimeScale`](Time) does not slow it.

```
public T As<T>() where T : Entity, new()
```

The script running on this entity, as script class `T`.

Returns: The script, or null when the entity has no script or its script is not a `T`.

```
public void CancelTimers()
```

Cancels every timer this entity started with [`Entity.After`](Entity) or [`Entity.Every`](Entity).

```
public bool CompareTag(string tag)
```

Whether the entity's [`Entity.Tag`](Entity) is exactly `tag`.

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
public Timer Every(float seconds, Action action, bool unscaled = false)
```

Runs `action` every `seconds`, until cancelled. See [`Entity.After`](Entity).

- `seconds`: The time between calls, above 0.
- `action`: What to run.
- `unscaled`: Count real time instead of game time.

```
public static Entity Find(string name, bool includeInactive = false)
```

The first entity, in the order they were created, whose [`Entity.Name`](Entity) is exactly `name`.

- `name`: The name to look for.
- `includeInactive`: Also look at switched-off entities and children of one. Default: skip them.

Returns: The entity, or null.

```
public static Entity[] FindAll(string name, bool includeInactive = false)
```

Every entity whose [`Entity.Name`](Entity) is exactly `name`, in the order they were created.

- `name`: The name to look for.
- `includeInactive`: Also include switched-off entities and children of one. Default: skip them.

```
public static Entity[] FindAllWithComponent<T>(bool includeInactive = false) where T : Component, new()
```

Every entity that has a component of type `T`, in the order they were created.

- `includeInactive`: Also include switched-off entities and children of one. Default: skip them.

```
public static Entity[] FindAllWithTag(string tag, bool includeInactive = false)
```

Every entity whose [`Entity.Tag`](Entity) is `tag`, in the order they were created. An empty tag matches nothing.

- `tag`: The tag to look for.
- `includeInactive`: Also include switched-off entities and children of one. Default: skip them.

```
public Entity FindEntityByName(string name)
```

Finds an entity in the scene by its [`Entity.Name`](Entity).

Returns: An entity with exactly that name, or null.

```
public static Entity FindWithComponent<T>(bool includeInactive = false) where T : Component, new()
```

The first entity, in the order they were created, that has a component of type `T`.

- `includeInactive`: Also look at switched-off entities and children of one. Default: skip them.

Returns: The entity, or null.

```
public static Entity FindWithTag(string tag, bool includeInactive = false)
```

The first entity, in the order they were created, whose [`Entity.Tag`](Entity) is `tag`.

- `tag`: The tag to look for.
- `includeInactive`: Also look at switched-off entities and children of one. Default: skip them.

Returns: The entity, or null. An empty tag matches nothing.

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
public Coroutine StartCoroutine(IEnumerator routine)
```

Starts a coroutine owned by this entity: a method that returns `System.Collections.IEnumerator` and pauses at each `yield return` (see [`Coroutine`](Coroutine)). It runs up to its first `yield` before this call returns. It stops when the entity is destroyed, and is paused while the entity is switched off.

- `routine`: The routine, for example `StartCoroutine(Flash())`.

```
public void StopAllCoroutines()
```

Stops every coroutine this entity started.

```
public void StopCoroutine(Coroutine coroutine)
```

Stops a coroutine started by [`Entity.StartCoroutine`](Entity). Null, or one that already ended, is ignored.

- `coroutine`: The coroutine to stop.

```
protected virtual void OnAnimationEvent(string name, string argument)
```

Called when a playing animation clip passes one of its events (a footstep, a hit frame). Events are authored in the model's `.events.yaml` file and travel with the clip.

- `name`: The event's name.
- `argument`: The event's free-text argument, empty when it has none.

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
