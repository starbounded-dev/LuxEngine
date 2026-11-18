# Random

`public static class Random`

Random numbers for gameplay. Seed it with [`Random.InitState`](Random) to repeat a sequence. A script that has both `using System;` and `using Lux;` must write `Lux.Random` or `System.Random` in full.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `InsideUnitCircle` | `Vector2` | get | A random point on or inside a circle of radius 1. |
| `InsideUnitSphere` | `Vector3` | get | A random point on or inside a sphere of radius 1. |
| `OnUnitSphere` | `Vector3` | get | A random point on the surface of a sphere of radius 1. |
| `Rotation` | `Quaternion` | get | A random rotation, evenly spread over all orientations. |
| `Value` | `float` | get | A random float from 0 up to, not including, 1. |

## Methods

```
public static Color ColorHSV(float saturationMin = 1f, float saturationMax = 1f, float valueMin = 1f, float valueMax = 1f)
```

A random opaque color with full saturation and value, or the ranges given, for varied but even tints.

```
public static void InitState(int seed)
```

Restarts the sequence from a seed, so the same seed gives the same numbers.

```
public static float Range(float min, float max)
```

A random float from `min` up to, not including, `max`.

```
public static int Range(int min, int max)
```

A random whole number from `min` up to, not including, `max`.

[All types](index)
