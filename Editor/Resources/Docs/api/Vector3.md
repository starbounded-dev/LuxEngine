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
public static Vector3 operator +(Vector3 a, Vector3 b)
```

Component-wise sum.

```
public static Vector3 operator *(Vector3 vector, float scalar)
```

The vector scaled by `scalar`.

[All types](index)
