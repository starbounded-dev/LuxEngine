# Vector2

`public struct Vector2`

A 2D vector of floats: a 2D position, size or direction.

## Fields

| Name | Type | Description |
|---|---|---|
| `X` | `float` | The X component. |
| `Y` | `float` | The Y component. |

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `One` | `Vector2` | get | (1, 1). |
| `Right` | `Vector2` | get | (1, 0). |
| `Up` | `Vector2` | get | (0, 1). |
| `Zero` | `Vector2` | get | (0, 0). |

## Methods

```
public Vector2(float scalar)
```

A vector with every component set to `scalar`.

```
public Vector2(float x, float y)
```

A vector from its components.

```
public static float Distance(Vector2 a, Vector2 b)
```

The distance between two points.

```
public static float Dot(Vector2 a, Vector2 b)
```

The dot product: the cosine of the angle between two unit vectors.

```
public bool Equals(Vector2 other)
```

Whether the components are exactly equal.

```
public float Length()
```

The vector's length.

```
public float LengthSquared()
```

The squared length, cheaper than [`Vector2.Length`](Vector2) for comparisons.

```
public static Vector2 Lerp(Vector2 a, Vector2 b, float t)
```

The point `t` of the way from `a` to `b`, with `t` limited to 0..1.

```
public Vector2 Normalized()
```

The same direction with length 1, or [`Vector2.Zero`](Vector2) for a zero vector.

```
public static Vector2 operator +(Vector2 a, Vector2 b)
```

Component-wise sum.

```
public static Vector2 operator *(Vector2 vector, float scalar)
```

The vector scaled by `scalar`.

```
public static Vector2 operator -(Vector2 a, Vector2 b)
```

Component-wise difference.

```
public static Vector2 operator -(Vector2 vector)
```

The vector pointing the opposite way.

```
public static Vector2 operator *(float scalar, Vector2 vector)
```

The vector scaled by `scalar`.

```
public static Vector2 operator /(Vector2 vector, float scalar)
```

The vector divided by `scalar`.

```
public static bool operator ==(Vector2 a, Vector2 b)
```

Whether the components are exactly equal.

```
public static bool operator !=(Vector2 a, Vector2 b)
```

Whether any component differs.

[All types](index)
