# Gizmos

`public static class Gizmos`

Debug lines drawn in the world while the game runs, in the editor and in the player (not in Dist builds).

A drawing belongs to the update method that made it and stays until that method runs again, so draw it every `OnUpdate` (or every `OnFixedUpdate`) to keep it. With a duration it stays that many seconds instead.

## Fields

| Name | Type | Description |
|---|---|---|
| `White` | `Vector4` | Opaque white, the default color. |

## Methods

```
public static void DrawLine(Vector3 start, Vector3 end)
```

Draws a white line between two world positions until the drawing method runs again.

```
public static void DrawLine(Vector3 start, Vector3 end, Vector4 color, float duration = 0f, bool depthTest = true)
```

Draws a line between two world positions.

- `color`: RGBA, components from 0 to 1.
- `duration`: Seconds the drawing stays; 0 keeps it until the drawing method runs again.
- `depthTest`: False draws it on top of everything.

```
public static void DrawRay(Vector3 origin, Vector3 direction, Vector4 color, float duration = 0f, bool depthTest = true)
```

Draws a line from `origin` to `origin + direction`.

- `color`: RGBA, components from 0 to 1.
- `duration`: Seconds the drawing stays; 0 keeps it until the drawing method runs again.
- `depthTest`: False draws it on top of everything.

```
public static void DrawWireBox(Vector3 center, Vector3 halfExtents, Vector4 color, float duration = 0f, bool depthTest = true)
```

Draws the twelve edges of an axis-aligned box.

- `center`: The box's center.
- `halfExtents`: Half the box's size on each axis.
- `color`: RGBA, components from 0 to 1.
- `duration`: Seconds the drawing stays; 0 keeps it until the drawing method runs again.
- `depthTest`: False draws it on top of everything.

```
public static void DrawWireSphere(Vector3 center, float radius, Vector4 color, float duration = 0f, bool depthTest = true)
```

Draws a sphere as three circles, one around each axis.

- `center`: The sphere's center.
- `radius`: The sphere's radius.
- `color`: RGBA, components from 0 to 1.
- `duration`: Seconds the drawing stays; 0 keeps it until the drawing method runs again.
- `depthTest`: False draws it on top of everything.

[All types](index)
