# Vector3

`public struct Vector3`

A 3D vector of floats: a position, direction, scale or Euler rotation.

## Fields

| Name | Type | Description |
|---|---|---|
| `X` | `float` | The X component. |
| `Y` | `float` | The Y component (up, in the world). |
| `Z` | `float` | The Z component. |

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Back` | `Vector3` | get | (0, 0, 1), the world's back. |
| `Down` | `Vector3` | get | (0, -1, 0), down in the world. |
| `Forward` | `Vector3` | get | (0, 0, -1), the world's forward: the way an entity with no rotation faces. |
| `Left` | `Vector3` | get | (-1, 0, 0), the world's left. |
| `One` | `Vector3` | get | (1, 1, 1). |
| `Right` | `Vector3` | get | (1, 0, 0), the world's right. |
| `Up` | `Vector3` | get | (0, 1, 0), up in the world. |
| `XY` | `Vector2` | get, set | The X and Y components. |
| `Zero` | `Vector3` | get | (0, 0, 0). |

## Methods

```
public Vector3(float scalar)
```

A vector with every component set to `scalar`.

```
public Vector3(float x, float y, float z)
```

A vector from its components.

```
public Vector3(Vector2 xy, float z)
```

A vector from a 2D vector and a Z component.

```
public static float Angle(Vector3 a, Vector3 b)
```

The angle between two directions, in radians.

```
public static Vector3 ClampMagnitude(Vector3 vector, float maxLength)
```

The vector with its length limited to `maxLength`.

```
public static Vector3 Cross(Vector3 a, Vector3 b)
```

The cross product: a vector at right angles to both, following the right-hand rule.

```
public static float Distance(Vector3 a, Vector3 b)
```

The distance between two points.

```
public static float Dot(Vector3 a, Vector3 b)
```

The dot product: the cosine of the angle between two unit vectors.

```
public bool Equals(Vector3 other)
```

Whether the components are exactly equal.

```
public float Length()
```

The vector's length.

```
public float LengthSquared()
```

The squared length, cheaper than [`Vector3.Length`](Vector3) for comparisons.

```
public static Vector3 Lerp(Vector3 a, Vector3 b, float t)
```

The point `t` of the way from `a` to `b`, with `t` limited to 0..1.

```
public static Vector3 Max(Vector3 a, Vector3 b)
```

The larger of each component.

```
public static Vector3 Min(Vector3 a, Vector3 b)
```

The smaller of each component.

```
public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDistance)
```

Moves `current` toward `target` by at most `maxDistance`.

```
public Vector3 Normalized()
```

The same direction with length 1, or [`Vector3.Zero`](Vector3) for a zero vector.

```
public static Vector3 Project(Vector3 vector, Vector3 onNormal)
```

The part of `vector` that lies along `onNormal`.

```
public static Vector3 ProjectOnPlane(Vector3 vector, Vector3 planeNormal)
```

The part of `vector` that lies in the plane with the unit normal `planeNormal`.

```
public static Vector3 Reflect(Vector3 direction, Vector3 normal)
```

The direction after bouncing off a surface with the unit normal `normal`.

```
public static Vector3 operator +(Vector3 a, Vector3 b)
```

Component-wise sum.

```
public static Vector3 operator -(Vector3 a, Vector3 b)
```

Component-wise difference.

```
public static Vector3 operator -(Vector3 vector)
```

The vector pointing the opposite way.

```
public static Vector3 operator *(Vector3 vector, float scalar)
```

The vector scaled by `scalar`.

```
public static Vector3 operator *(float scalar, Vector3 vector)
```

The vector scaled by `scalar`.

```
public static Vector3 operator *(Vector3 a, Vector3 b)
```

Component-wise product.

```
public static Vector3 operator /(Vector3 vector, float scalar)
```

The vector divided by `scalar`.

```
public static bool operator ==(Vector3 a, Vector3 b)
```

Whether the components are exactly equal.

```
public static bool operator !=(Vector3 a, Vector3 b)
```

Whether any component differs.

[All types](index)
