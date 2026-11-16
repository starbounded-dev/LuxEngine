# Lights

Create lights from **Create 3D → Lights** in the Scene Hierarchy's menu. Every light has a colour (**Radiance**, or **Use Color Temperature** with a **Temperature (K)**), an **Intensity**, and a header checkbox that switches it off, shadows included.

| Light | What it is | Shape and range |
|---|---|---|
| **Directional Light** | The sun: parallel light from one direction | Follows the entity's rotation; reaches everywhere |
| **Point Light** | A bulb shining in every direction | **Radius**: the light fades to nothing there |
| **Spot Light** | A cone, like a torch | **Angle** and **Range**; points along the entity's -Z |
| **Area Light** | A light with a surface: rectangle, disk, sphere or tube | Soft, realistic highlights; **Range** limits its reach |
| **Sky Light** | Light and reflections from the sky | An HDR environment map; **Rotation** turns it |

Start a scene with a Directional Light and a Sky Light; most outdoor scenes need nothing else.

## Units

**Intensity** can be a plain multiplier (**Unitless**) or a physical unit: **Lux** for the sun, **Lumens** or **Candela** for point, spot and area lights. Physical units make lights from real-world references look right next to each other.

## Shadows

- **Cast Shadows** turns a light's shadows on. Directional lights cast shadows by default; spot, area and point lights cost more, so turn them on where they matter.
- **Soft Shadows** blurs the shadow edge by the light's size (**Light Size**), as real shadows do.
- **Shadow Distance** limits how far from the camera a directional light's shadows reach; shorter is sharper.

Overall shadow quality (cascades, filtering, resolution) is set in the **Shadows** section of the **Scene Renderer** panel.
