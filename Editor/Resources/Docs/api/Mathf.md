# Mathf

`public static class Mathf`

Single-precision maths helpers for gameplay: clamping, interpolation, angles and smoothing.

## Fields

| Name | Type | Description |
|---|---|---|
| `Deg2Rad` | `float` | Multiply degrees by this to get radians. |
| `Epsilon` | `float` | The smallest value that is not zero, for tolerant comparisons. |
| `Infinity` | `float` | Positive infinity. |
| `PI` | `float` | The ratio of a circle's circumference to its diameter. |
| `Rad2Deg` | `float` | Multiply radians by this to get degrees. |

## Methods

```
public static float Abs(float value)
```

The absolute value.

```
public static float Acos(float value)
```

The angle in radians whose cosine is `value`.

```
public static bool Approximately(float a, float b)
```

Whether two floats are equal within rounding error.

```
public static float Asin(float value)
```

The angle in radians whose sine is `value`.

```
public static float Atan(float value)
```

The angle in radians whose tangent is `value`.

```
public static float Atan2(float y, float x)
```

The angle in radians of the point (`x`, `y`).

```
public static float Ceil(float value)
```

The smallest whole number not below `value`.

```
public static float Clamp(float value, float min, float max)
```

`value` limited to the range from `min` to `max`.

```
public static int Clamp(int value, int min, int max)
```

`value` limited to the range from `min` to `max`.

```
public static float Clamp01(float value)
```

`value` limited to the range from 0 to 1.

```
public static float Cos(float radians)
```

The cosine of an angle in radians.

```
public static float DeltaAngle(float current, float target)
```

The shortest signed difference in degrees from angle `current` to `target`, in -180..180.

```
public static float Floor(float value)
```

The largest whole number not above `value`.

```
public static float InverseLerp(float a, float b, float value)
```

Where `value` lies between `a` and `b`, as 0..1. Returns 0 when they are equal.

```
public static float Lerp(float a, float b, float t)
```

The point `t` of the way from `a` to `b`, with `t` limited to 0..1.

```
public static float LerpAngle(float a, float b, float t)
```

Like [`Mathf.Lerp`](Mathf) for angles in degrees: takes the short way round.

```
public static float LerpUnclamped(float a, float b, float t)
```

Like [`Mathf.Lerp`](Mathf) but `t` is not limited, so the result can pass either end.

```
public static float Max(float a, float b)
```

The larger of two values.

```
public static int Max(int a, int b)
```

The larger of two whole numbers.

```
public static float Min(float a, float b)
```

The smaller of two values.

```
public static int Min(int a, int b)
```

The smaller of two whole numbers.

```
public static float MoveTowards(float current, float target, float maxDelta)
```

Moves `current` toward `target` by at most `maxDelta`.

```
public static float MoveTowardsAngle(float current, float target, float maxDelta)
```

Like [`Mathf.MoveTowards`](Mathf) for angles in degrees: takes the short way round.

```
public static float PingPong(float t, float length)
```

Bounces `t` back and forth between 0 and `length`.

```
public static float Pow(float value, float power)
```

`value` raised to `power`.

```
public static float Repeat(float t, float length)
```

Wraps `t` into 0..`length`, so it loops.

```
public static float Round(float value)
```

The nearest whole number, halves going to the even one.

```
public static float Sign(float value)
```

-1 for a negative value, 1 for zero or a positive one.

```
public static float Sin(float radians)
```

The sine of an angle in radians.

```
public static float SmoothDamp(float current, float target, ref float velocity, float smoothTime, float deltaTime)
```

Eases `current` toward `target` like a spring that never overshoots. Call it every frame, keeping `velocity` between calls.

- `current`: The value now.
- `target`: The value to reach.
- `velocity`: The rate of change, updated by each call. Start it at 0.
- `smoothTime`: Roughly how many seconds to take; smaller is faster.
- `deltaTime`: The time since the last call, usually [`Time.DeltaTime`](Time).

Returns: The new value.

```
public static float SmoothStep(float edge0, float edge1, float x)
```

Eases from 0 to 1 as `x` goes from `edge0` to `edge1`, flat at both ends.

```
public static float Sqrt(float value)
```

The square root.

```
public static float Tan(float radians)
```

The tangent of an angle in radians.

[All types](index)
