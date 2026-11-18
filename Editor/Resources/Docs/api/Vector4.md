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
public bool Equals(Vector4 other)
```

Whether the components are exactly equal.

```
public static Vector4 Lerp(Vector4 a, Vector4 b, float t)
```

The point `t` of the way from `a` to `b`, with `t` limited to 0..1.

```
public static Vector4 operator +(Vector4 a, Vector4 b)
```

Component-wise sum.

```
public static Vector4 operator *(Vector4 vector, float scalar)
```

The vector scaled by `scalar`.

```
public static Vector4 operator -(Vector4 a, Vector4 b)
```

Component-wise difference.

```
public static Vector4 operator -(Vector4 vector)
```

The vector pointing the opposite way.

```
public static Vector4 operator *(float scalar, Vector4 vector)
```

The vector scaled by `scalar`.

```
public static Vector4 operator /(Vector4 vector, float scalar)
```

The vector divided by `scalar`.

```
public static bool operator ==(Vector4 a, Vector4 b)
```

Whether the components are exactly equal.

```
public static bool operator !=(Vector4 a, Vector4 b)
```

Whether any component differs.

[All types](index)
