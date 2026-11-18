# Color

`public struct Color`

A linear RGBA color with components from 0 to 1. Converts to and from [`Vector4`](Vector4), so a color can be assigned to any component property that takes one (`sprite.Color = Color.Red`).

## Fields

| Name | Type | Description |
|---|---|---|
| `A` | `float` | The alpha component: 0 is transparent, 1 opaque. |
| `B` | `float` | The blue component. |
| `G` | `float` | The green component. |
| `R` | `float` | The red component. |

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Black` | `Color` | get | Opaque black. |
| `Blue` | `Color` | get | Opaque blue. |
| `Clear` | `Color` | get | Fully transparent black. |
| `Cyan` | `Color` | get | Opaque cyan. |
| `Gray` | `Color` | get | Opaque mid gray. |
| `Green` | `Color` | get | Opaque green. |
| `Magenta` | `Color` | get | Opaque magenta. |
| `Red` | `Color` | get | Opaque red. |
| `White` | `Color` | get | Opaque white. |
| `Yellow` | `Color` | get | Opaque yellow. |

## Methods

```
public Color(float r, float g, float b)
```

An opaque color.

```
public Color(float r, float g, float b, float a)
```

A color from its four components.

```
public bool Equals(Color other)
```

Whether the components are exactly equal.

```
public static Color FromHSV(float hue, float saturation, float value, float alpha = 1f)
```

A color from hue, saturation and value, each 0..1 (hue wraps).

```
public static Color FromHex(string hex, ref bool success)
```

Parses "RRGGBB" or "RRGGBBAA" (a leading # is allowed), for example "#ff8800".

Returns: The color, or false in `success` when the text is not a color.

```
public static Color Lerp(Color a, Color b, float t)
```

The color `t` of the way from `a` to `b`, with `t` limited to 0..1.

```
public void ToHSV(ref float hue, ref float saturation, ref float value)
```

The color as hue, saturation and value, each 0..1.

```
public string ToHex()
```

The color as "RRGGBBAA" hex digits.

```
public Color WithAlpha(float alpha)
```

The color with its alpha replaced.

```
public static Color operator +(Color a, Color b)
```

Component-wise sum.

```
public static Color operator -(Color a, Color b)
```

Component-wise difference.

```
public static Color operator *(Color a, Color b)
```

Component-wise product, for tinting one color by another.

```
public static Color operator *(Color color, float scalar)
```

Every component scaled by `scalar`.

```
public static bool operator ==(Color a, Color b)
```

Whether the components are exactly equal.

```
public static bool operator !=(Color a, Color b)
```

Whether any component differs.

```
public static Color operator implicit(Vector4 v)
```

Reads a [`Vector4`](Vector4) as red, green, blue, alpha.

```
public static Vector4 operator implicit(Color c)
```

Writes the color as a [`Vector4`](Vector4) of red, green, blue, alpha.

[All types](index)
