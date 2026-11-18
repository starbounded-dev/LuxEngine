# LightComponent

`public abstract class LightComponent : Component`

The base of the lights that shine from an entity: [`DirectionalLightComponent`](DirectionalLightComponent), [`PointLightComponent`](PointLightComponent), [`SpotLightComponent`](SpotLightComponent) and [`AreaLightComponent`](AreaLightComponent). [`Component.Enabled`](Component) switches a light off without losing its settings.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `CastShadows` | `bool` | get, set | Whether the light casts shadows. |
| `Color` | `Color` | get, set | The color of the light. Components are limited to 0 or more; alpha is ignored. |
| `ColorTemperature` | `float` | get, set | The tint of the light as a black-body temperature in Kelvin, when [`LightComponent.UseColorTemperature`](LightComponent) is on. |
| `Intensity` | `float` | get, set | How bright the light is, in the unit chosen in the inspector. Negative values become 0. |
| `SoftShadows` | `bool` | get, set | Whether its shadows have soft edges. |
| `UseColorTemperature` | `bool` | get, set | Whether [`LightComponent.ColorTemperature`](LightComponent) replaces [`LightComponent.Color`](LightComponent) as the light's tint. |

[All types](index)
