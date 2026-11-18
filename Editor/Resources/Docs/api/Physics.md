# Physics

`public static class Physics`

Questions about the physics world: what a ray or a moving shape would hit, and what overlaps a shape. They see colliders and character controllers while the game is playing, and answer "nothing" otherwise. Call them from `OnUpdate` or `OnFixedUpdate`. By default they skip triggers, and every `ignore` parameter leaves out one entity (usually the caller), which is how a ray from inside a character avoids hitting it.

A `layerMask` has one bit for each physics layer of the project, numbered by its position in the layer list in Project Settings: layer 0 is bit 0. Build one with [`Physics.GetLayerMask`](Physics) or use [`Physics.AllLayers`](Physics).

## Fields

| Name | Type | Description |
|---|---|---|
| `AllLayers` | `uint` | A layer mask that includes every physics layer. |

## Methods

```
public static bool BoxCast(Vector3 center, Vector3 halfExtents, Quaternion orientation, Vector3 direction, ref RaycastHit hit, float maxDistance = 1000f, uint layerMask = 4294967295, Entity? ignore = null, bool includeTriggers = false)
```

Sweeps a box along a direction and reports the first collider it touches.

- `center`: The center of the box at the start, in world space.
- `halfExtents`: Half the box's size along each of its own axes, in meters.
- `orientation`: The box's rotation.
- `direction`: The way it sweeps; it need not be a unit vector.
- `hit`: The first hit: [`RaycastHit.Point`](RaycastHit) is where the box touches, [`RaycastHit.Distance`](RaycastHit) how far its center moved.
- `maxDistance`: How far the box sweeps, in meters.
- `layerMask`: Which physics layers the box can hit.
- `ignore`: An entity the box passes through, or null.
- `includeTriggers`: Whether trigger volumes count as hits.

Returns: True when something was hit.

```
public static bool CapsuleCast(Vector3 center, float radius, float halfHeight, Quaternion orientation, Vector3 direction, ref RaycastHit hit, float maxDistance = 1000f, uint layerMask = 4294967295, Entity? ignore = null, bool includeTriggers = false)
```

Sweeps a capsule along a direction and reports the first collider it touches. The capsule stands along Y before `orientation` turns it.

- `center`: The center of the capsule at the start, in world space.
- `radius`: The radius of the capsule and of its rounded ends, in meters.
- `halfHeight`: Half the length of the straight part between the rounded ends, in meters.
- `orientation`: The capsule's rotation.
- `direction`: The way it sweeps; it need not be a unit vector.
- `hit`: The first hit: [`RaycastHit.Point`](RaycastHit) is where the capsule touches, [`RaycastHit.Distance`](RaycastHit) how far its center moved.
- `maxDistance`: How far the capsule sweeps, in meters.
- `layerMask`: Which physics layers the capsule can hit.
- `ignore`: An entity the capsule passes through, or null.
- `includeTriggers`: Whether trigger volumes count as hits.

Returns: True when something was hit.

```
public static bool CheckSphere(Vector3 center, float radius, uint layerMask = 4294967295, Entity? ignore = null, bool includeTriggers = false)
```

Whether any collider touches a sphere.

- `center`: The sphere's center, in world space.
- `radius`: The sphere's radius, in meters.
- `layerMask`: Which physics layers count.
- `ignore`: An entity to leave out, or null.
- `includeTriggers`: Whether trigger volumes count.

```
public static uint GetLayerMask(string[] layerNames)
```

A layer mask for the physics layers with the given names.

- `layerNames`: Names as written in the project's physics layers. A name the project does not have is an error.

```
public static Entity[] OverlapBox(Vector3 center, Vector3 halfExtents, Quaternion orientation, uint layerMask = 4294967295, Entity? ignore = null, bool includeTriggers = false)
```

The entities whose colliders touch a box, each once.

- `center`: The box's center, in world space.
- `halfExtents`: Half the box's size along each of its own axes, in meters.
- `orientation`: The box's rotation.
- `layerMask`: Which physics layers count.
- `ignore`: An entity to leave out, or null.
- `includeTriggers`: Whether trigger volumes count.

```
public static Entity[] OverlapCapsule(Vector3 center, float radius, float halfHeight, Quaternion orientation, uint layerMask = 4294967295, Entity? ignore = null, bool includeTriggers = false)
```

The entities whose colliders touch a capsule, each once. The capsule stands along Y before `orientation` turns it.

- `center`: The capsule's center, in world space.
- `radius`: The radius of the capsule and of its rounded ends, in meters.
- `halfHeight`: Half the length of the straight part between the rounded ends, in meters.
- `orientation`: The capsule's rotation.
- `layerMask`: Which physics layers count.
- `ignore`: An entity to leave out, or null.
- `includeTriggers`: Whether trigger volumes count.

```
public static Entity[] OverlapSphere(Vector3 center, float radius, uint layerMask = 4294967295, Entity? ignore = null, bool includeTriggers = false)
```

The entities whose colliders touch a sphere, each once.

- `center`: The sphere's center, in world space.
- `radius`: The sphere's radius, in meters.
- `layerMask`: Which physics layers count.
- `ignore`: An entity to leave out, or null.
- `includeTriggers`: Whether trigger volumes count.

```
public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance = 1000f, uint layerMask = 4294967295, Entity? ignore = null, bool includeTriggers = false)
```

Casts a ray and reports only whether it hits anything.

- `origin`: Where the ray starts, in world space.
- `direction`: The way it goes; it need not be a unit vector.
- `maxDistance`: How far the ray reaches, in meters.
- `layerMask`: Which physics layers the ray can hit.
- `ignore`: An entity the ray passes through, or null.
- `includeTriggers`: Whether trigger volumes count as hits.

```
public static bool Raycast(Vector3 origin, Vector3 direction, ref RaycastHit hit, float maxDistance = 1000f, uint layerMask = 4294967295, Entity? ignore = null, bool includeTriggers = false)
```

Casts a ray and reports the nearest collider it enters.

- `origin`: Where the ray starts, in world space.
- `direction`: The way it goes; it need not be a unit vector. A zero direction hits nothing.
- `hit`: The nearest hit, when there is one.
- `maxDistance`: How far the ray reaches, in meters.
- `layerMask`: Which physics layers the ray can hit.
- `ignore`: An entity the ray passes through, or null.
- `includeTriggers`: Whether trigger volumes count as hits.

Returns: True when something was hit.

```
public static RaycastHit[] RaycastAll(Vector3 origin, Vector3 direction, float maxDistance = 1000f, uint layerMask = 4294967295, Entity? ignore = null, bool includeTriggers = false)
```

Casts a ray and reports every collider it enters, nearest first. A collider the ray starts inside is not reported.

- `origin`: Where the ray starts, in world space.
- `direction`: The way it goes; it need not be a unit vector.
- `maxDistance`: How far the ray reaches, in meters.
- `layerMask`: Which physics layers the ray can hit.
- `ignore`: An entity the ray passes through, or null.
- `includeTriggers`: Whether trigger volumes count as hits.

Returns: The hits, possibly none.

```
public static bool SphereCast(Vector3 origin, float radius, Vector3 direction, ref RaycastHit hit, float maxDistance = 1000f, uint layerMask = 4294967295, Entity? ignore = null, bool includeTriggers = false)
```

Sweeps a sphere along a direction and reports the first collider it touches.

- `origin`: The center of the sphere at the start, in world space.
- `radius`: The sphere's radius, in meters.
- `direction`: The way it sweeps; it need not be a unit vector.
- `hit`: The first hit: [`RaycastHit.Point`](RaycastHit) is where the sphere touches, [`RaycastHit.Distance`](RaycastHit) how far its center moved.
- `maxDistance`: How far the sphere sweeps, in meters.
- `layerMask`: Which physics layers the sphere can hit.
- `ignore`: An entity the sphere passes through, or null.
- `includeTriggers`: Whether trigger volumes count as hits.

Returns: True when something was hit.

[All types](index)
