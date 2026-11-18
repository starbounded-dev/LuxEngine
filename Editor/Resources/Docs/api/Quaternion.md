# Quaternion

`public struct Quaternion`

A rotation in 3D. Compose rotations with `*` (the right one applies first), rotate a vector with `rotation * vector`, and convert from or to the Euler angles (radians) that [`Entity.Rotation`](Entity) uses.

## Fields

| Name | Type | Description |
|---|---|---|
| `W` | `float` | The real part. |
| `X` | `float` | The X component of the imaginary part. |
| `Y` | `float` | The Y component of the imaginary part. |
| `Z` | `float` | The Z component of the imaginary part. |

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Identity` | `Quaternion` | get | No rotation. |

## Methods

```
public Quaternion(float x, float y, float z, float w)
```

A quaternion from its four components. Prefer [`Quaternion.Euler`](Quaternion) or [`Quaternion.AngleAxis`](Quaternion).

```
public static float Angle(Quaternion a, Quaternion b)
```

The angle between two rotations, in radians.

```
public static Quaternion AngleAxis(float radians, Vector3 axis)
```

The rotation of `radians` about `axis`.

```
public static float Dot(Quaternion a, Quaternion b)
```

The cosine of half the angle between two rotations: 1 when they are the same.

```
public bool Equals(Quaternion other)
```

Whether the components are exactly equal.

```
public static Quaternion Euler(Vector3 radians)
```

The rotation for Euler angles in radians, applied about X, then Y, then Z (the order [`Entity.Rotation`](Entity) uses).

```
public static Quaternion FromToRotation(Vector3 from, Vector3 to)
```

The rotation that turns the direction `from` onto `to` by the shortest way.

```
public Quaternion Inverse()
```

The rotation that undoes this one.

```
public float Length()
```

The length of the four components. A rotation has length 1.

```
public static Quaternion LookRotation(Vector3 forward, Vector3 up)
```

The rotation that makes -Z (an entity's forward) point along `forward` with +Y as near `up` as possible.

```
public Quaternion Normalized()
```

The same rotation with length 1, or [`Quaternion.Identity`](Quaternion) for a zero quaternion.

```
public static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxRadians)
```

Turns `from` toward `to` by at most `maxRadians`.

```
public static Quaternion Slerp(Quaternion a, Quaternion b, float t)
```

Blends from `a` to `b` along the shortest arc at constant speed; `t` is limited to 0..1.

```
public Vector3 ToEuler()
```

The rotation as Euler angles in radians, in the form [`Entity.Rotation`](Entity) uses.

```
public static Quaternion operator *(Quaternion a, Quaternion b)
```

Applies `b` first, then `a`.

```
public static Vector3 operator *(Quaternion q, Vector3 v)
```

Rotates `v` by `q`.

```
public static bool operator ==(Quaternion a, Quaternion b)
```

Whether the components are exactly equal.

```
public static bool operator !=(Quaternion a, Quaternion b)
```

Whether any component differs.

[All types](index)
