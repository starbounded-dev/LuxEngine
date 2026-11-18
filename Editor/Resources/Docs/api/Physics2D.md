# Physics2D

`public static class Physics2D`

Questions about the 2D physics world: what a ray hits and which colliders overlap a shape. They see Box Collider 2D, Circle Collider 2D, Polygon, Capsule and Edge colliders while the game is playing, and answer "nothing" otherwise. Call them from `OnUpdate` or `OnFixedUpdate`. By default they skip triggers, and every `ignore` parameter leaves out one entity (usually the caller), which is how a ray from inside a character avoids hitting it.

## Methods

```
public static Entity[] OverlapBox(Vector2 center, Vector2 halfExtents, float angle = 0f, Entity? ignore = null, bool includeTriggers = false)
```

The entities whose colliders touch a box, each once.

- `center`: The box's center, in world space.
- `halfExtents`: Half the box's width and height, in meters.
- `angle`: The box's rotation, in radians.
- `ignore`: An entity to leave out, or null.
- `includeTriggers`: Whether trigger colliders count.

```
public static Entity[] OverlapCircle(Vector2 center, float radius, Entity? ignore = null, bool includeTriggers = false)
```

The entities whose colliders touch a circle, each once.

- `center`: The circle's center, in world space.
- `radius`: The circle's radius, in meters.
- `ignore`: An entity to leave out, or null.
- `includeTriggers`: Whether trigger colliders count.

```
public static Entity[] OverlapPoint(Vector2 point, Entity? ignore = null, bool includeTriggers = false)
```

The entities whose colliders contain a point, each once.

- `point`: The point, in world space.
- `ignore`: An entity to leave out, or null.
- `includeTriggers`: Whether trigger colliders count.

```
public static bool Raycast(Vector2 origin, Vector2 direction, float maxDistance = 1000f, Entity? ignore = null, bool includeTriggers = false)
```

Casts a ray and reports only whether it hits anything.

- `origin`: Where the ray starts, in world space.
- `direction`: The way it goes; it need not be a unit vector.
- `maxDistance`: How far the ray reaches, in meters.
- `ignore`: An entity the ray passes through, or null.
- `includeTriggers`: Whether trigger colliders count as hits.

```
public static bool Raycast(Vector2 origin, Vector2 direction, ref RaycastHit2D hit, float maxDistance = 1000f, Entity? ignore = null, bool includeTriggers = false)
```

Casts a ray and reports the nearest collider it enters.

- `origin`: Where the ray starts, in world space.
- `direction`: The way it goes; it need not be a unit vector. A zero direction hits nothing.
- `hit`: The nearest hit, when there is one.
- `maxDistance`: How far the ray reaches, in meters.
- `ignore`: An entity the ray passes through, or null.
- `includeTriggers`: Whether trigger colliders count as hits.

Returns: True when something was hit.

```
public static RaycastHit2D[] RaycastAll(Vector2 origin, Vector2 direction, float maxDistance = 1000f, Entity? ignore = null, bool includeTriggers = false)
```

Casts a ray and reports every entity it enters, nearest first, each once.

- `origin`: Where the ray starts, in world space.
- `direction`: The way it goes; it need not be a unit vector.
- `maxDistance`: How far the ray reaches, in meters.
- `ignore`: An entity the ray passes through, or null.
- `includeTriggers`: Whether trigger colliders count as hits.

Returns: The hits, possibly none.

[All types](index)
