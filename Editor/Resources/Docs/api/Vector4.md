# Vector4

`public struct Vector4`

A 4D vector of floats, most often an RGBA color with components from 0 to 1.

## Fields

| Name | Type | Description |
|---|---|---|
| `W` | `float` | The W component (alpha, for a color). |
| `X` | `float` | The X component (red, for a color). |
| `Y` | `float` | The Y component (green, for a color). |
| `Z` | `float` | The Z component (blue, for a color). |

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `XY` | `Vector2` | get, set | The X and Y components. |
| `XYZ` | `Vector3` | get, set | The X, Y and Z components. |
| `Zero` | `Vector4` | get | (0, 0, 0, 0). |

## Methods

```
public Vector4(float scalar)
```

A vector with every component set to `scalar`.

```
public Vector4(float x, float y, float z, float w)
```

A vector from its components.

```
public Vector4(Vector3 xyz, float w)
```

A vector from a 3D vector and a W component.

```
public static Vector4 operator +(Vector4 a, Vector4 b)
```

Component-wise sum.

```
public static Vector4 operator *(Vector4 vector, float scalar)
```

The vector scaled by `scalar`.

[All types](index)
