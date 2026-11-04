# RangeAttribute

`public class RangeAttribute : Attribute`

Draws a numeric field as a slider clamped to [[`RangeAttribute.Min`](RangeAttribute), [`RangeAttribute.Max`](RangeAttribute)].

## Fields

| Name | Type | Description |
|---|---|---|
| `Max` | `float` | The largest value the slider allows. |
| `Min` | `float` | The smallest value the slider allows. |

## Methods

```
public RangeAttribute(float min, float max)
```

A slider from `min` to `max`.

[All types](index)
