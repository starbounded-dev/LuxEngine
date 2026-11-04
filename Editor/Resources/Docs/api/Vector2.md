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
public float Length()
```

The vector's length.

```
public float LengthSquared()
```

The squared length, cheaper than [`Vector2.Length`](Vector2) for comparisons.

```
public static Vector2 operator +(Vector2 a, Vector2 b)
```

Component-wise sum.

```
public static Vector2 operator *(Vector2 vector, float scalar)
```

The vector scaled by `scalar`.

[All types](index)
