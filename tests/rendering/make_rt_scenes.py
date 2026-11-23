#!/usr/bin/env python3
"""Writes the sample scenes the ray-tracing sampling checks need (run_rt_sampling_check.py):

  GI_PowerLights  a grey floor lit by 24 point lights whose power spans three orders of magnitude
                  (uniform light choice wastes most samples; power sampling / ReSTIR DI should not)
  GI_GlossyArea   a glossy floor under one large rect area light plus a small bright one (MIS territory)

    python tests/rendering/make_rt_scenes.py
"""
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCENES = ROOT / "Editor/LuxSampleProject/Assets/Scenes"
MATERIALS = ROOT / "Editor/LuxSampleProject/Assets/Materials/GI"

NEXT = [17000000000000000]


def eid():
    NEXT[0] += 1
    return NEXT[0]


def entity(tag, pos, scale=(1, 1, 1), rot=(0, 0, 0), body=""):
    return f"""  - Entity: {eid()}
    TagComponent:
      Tag: {tag}
    Parent: 0
    Children:
      []
    TransformComponent:
      Position: [{pos[0]}, {pos[1]}, {pos[2]}]
      Rotation: [{rot[0]}, {rot[1]}, {rot[2]}]
      Scale: [{scale[0]}, {scale[1]}, {scale[2]}]
{body}"""


def floor(material, scale=(14, 0.1, 14)):
    return entity("Floor", (0, -0.05, 0), scale, body=f"""    StaticMeshComponent:
      AssetID: Meshes/Source/Default/Cube.gltf
      MaterialTable:
        0: {material}
      Visible: true
""")


def point(tag, pos, colour, intensity, radius):
    return entity(tag, pos, body=f"""    PointLightComponent:
      Radiance: [{colour[0]}, {colour[1]}, {colour[2]}]
      Intensity: {intensity}
      Unit: 0
      ColorTemperature: 6500
      UseColorTemperature: false
      CastShadows: false
      SoftShadows: false
      MinRadius: 0.2
      Radius: {radius}
      LightSize: 0.2
      Falloff: 1
""")


def spot(tag, pos, rot, intensity, angle):
    return entity(tag, pos, rot=rot, body=f"""    SpotLightComponent:
      Radiance: [1, 0.95, 0.85]
      Angle: {angle}
      AngleAttenuation: 2
      CastsShadows: false
      SoftShadows: false
      Falloff: 1
      Intensity: {intensity}
      Unit: 0
      ColorTemperature: 6500
      UseColorTemperature: false
      Range: 30
      ShadowDistance: 0
      ShadowResolutionTier: 1
""")


def wall(tag, pos, scale):
    return entity(tag, pos, scale, body="""    StaticMeshComponent:
      AssetID: Meshes/Source/Default/Cube.gltf
      MaterialTable:
        0: Materials/GI/GI_White.lmat
      Visible: true
""")


def area(tag, pos, rot, shape, intensity, width=1.0, height=1.0):
    return entity(tag, pos, rot=rot, body=f"""    AreaLightComponent:
      Shape: {shape}
      Radiance: [1, 0.95, 0.85]
      Intensity: {intensity}
      Unit: 0
      ColorTemperature: 6500
      UseColorTemperature: false
      Width: {width}
      Height: {height}
      Radius: 0.5
      Length: 1.6
      TwoSided: false
      Range: 30
      CastsShadows: true
""")


def camera(pos, rot):
    return entity("GI Camera", pos, rot=rot, body="""    CameraComponent:
      Camera:
        ProjectionType: 0
        PerspectiveFOV: 50
        PerspectiveNear: 0.1
        PerspectiveFar: 100
        OrthographicSize: 10
        OrthographicNear: -1
        OrthographicFar: 1
      Primary: true
      FixedAspectRatio: false
""")


def write(name, entities):
    text = f"FormatVersion: 1\nScene: {name}\nEntities:\n" + "".join(entities)
    (SCENES / f"{name}.luxscene").write_text(text.rstrip("\n"), encoding="utf-8", newline="\n")
    print("wrote", name)


def main():
    # Power spans ~3 decades: intensity = 0.4 * 2^(k mod 12), a ring of 24 lights over the floor.
    lights = []
    for i in range(24):
        angle = 2 * math.pi * i / 24
        radius = 2.0 + 3.5 * ((i * 7) % 5) / 4
        intensity = round(0.4 * (2 ** (i % 12)) * (0.5 if i >= 12 else 1.0), 3)
        colour = [(1, 0.8, 0.6), (0.6, 0.8, 1), (1, 1, 1), (0.8, 1, 0.7)][i % 4]
        lights.append(point(f"Light {i}", (round(radius * math.cos(angle), 3), 1.4, round(radius * math.sin(angle), 3)), colour, intensity, 9))
    write("GI_PowerLights", [floor("Materials/GI/GI_White.lmat")] + lights + [camera((0, 9, 7), (-1.0, 0, 0))])

    # A narrow spot lights a patch of the back wall; the floor only sees that patch indirectly (high-variance GI: reuse territory).
    write("GI_BounceSpot", [
        floor("Materials/GI/GI_White.lmat", (14, 0.1, 14)),
        wall("Back Wall", (0, 3, -5), (14, 6, 0.2)),
        spot("Spot", (0, 3, 4), (0, 0, 0), 900, 10),
        camera((0, 2.2, 8), (-0.12, 0, 0)),
    ])

    material = MATERIALS / "GI_Glossy.lmat"
    material.write_text("""FormatVersion: 1
Material:
  Transparent: false
  AlbedoColor: [0.6, 0.6, 0.62]
  Emission: 0
  UseNormalMap: false
  Metalness: 0
  Roughness: 0.18
  AlbedoMap: 0
  NormalMap: 0
  MetalnessMap: 0
  RoughnessMap: 0
  MaterialFlags: 66""", encoding="utf-8", newline="\n")
    print("wrote", material.name)
    write("GI_GlossyArea", [
        floor("Materials/GI/GI_Glossy.lmat", (14, 0.1, 10)),
        area("Large Rect", (-1.5, 3.0, 0), (-1.570796, 0, 0), 0, 18, 3.5, 2.0),
        area("Small Rect", (4.0, 2.2, -1.0), (-1.570796, 0, 0), 0, 400, 0.25, 0.25),
        camera((0, 5.5, 7), (-0.62, 0, 0)),
    ])


if __name__ == "__main__":
    main()
