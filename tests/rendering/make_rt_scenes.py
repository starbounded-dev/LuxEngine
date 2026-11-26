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


def nosky():
    return entity("No Sky", (0, 0, 0), body="""    SkyLightComponent:
      EnvironmentMap: 0
      Intensity: 0
      Lod: 0
      DynamicSky: true
      TurbidityAzimuthInclination: [2, 0, 0.2]
""")


def mesh(tag, asset, pos, scale, material, rot=(0, 0, 0), folder="Default"):
    return entity(tag, pos, scale, rot, body=f"""    StaticMeshComponent:
      AssetID: Meshes/Source/{folder}/{asset}.gltf
      MaterialTable:
        0: Materials/GI/{material}.lmat
      Visible: true
""")


def write_material(name, albedo, metalness, roughness, extra=None, *, emission=0):
    lines = ["FormatVersion: 1", "Material:", "  Transparent: false", f"  AlbedoColor: [{albedo[0]}, {albedo[1]}, {albedo[2]}]", f"  Emission: {emission}",
             "  UseNormalMap: false", f"  Metalness: {metalness}", f"  Roughness: {roughness}", "  AlbedoMap: 0", "  NormalMap: 0", "  MetalnessMap: 0", "  RoughnessMap: 0"]
    for key, value in (extra or {}).items():
        lines.append(f"  {key}: {value}")
    lines.append("  MaterialFlags: 66")
    (MATERIALS / f"{name}.lmat").write_text(chr(10).join(lines), encoding="utf-8", newline=chr(10))
    print("wrote", name)


def write(name, entities):
    text = f"FormatVersion: 1\nScene: {name}\nEntities:\n" + "".join(entities)
    (SCENES / f"{name}.luxscene").write_text(text.rstrip("\n"), encoding="utf-8", newline="\n")
    print("wrote", name)


def write_fog_sun_scene():
    # The sphere sits on the floor-to-sun ray, outside the camera-to-floor ray.
    sun = entity("Sun", (0, 0, 0), rot=(-math.pi / 2, 0, 0), body="""    DirectionalLightComponent:
      Intensity: 2
      Radiance: [1, 1, 1]
      Unit: 0
      UseColorTemperature: false
      CastShadows: false
""")
    write("GI_FogSun", [nosky(), sun, floor("Materials/GI/GI_White.lmat"),
                       camera((0, 4, 6), (-math.atan2(4, 6), 0, 0))])


def write_rough_reflection_scene():
    write_material("GI_RoughMetal", (0.9, 0.9, 0.9), 1.0, 0.32)
    write_material("GI_ReflectionEmitter", (1.0, 0.45, 0.12), 0.0, 0.7, emission=2.5)
    write("GI_RoughReflection", [
        nosky(),
        floor("Materials/GI/GI_RoughMetal.lmat", (16, 0.1, 14)),
        mesh("Emitter", "Cube", (0, 1.45, -1.2), (1.4, 0.65, 0.4), "GI_ReflectionEmitter"),
        camera((0, 2.1, 5.5), (-0.30, 0, 0)),
    ])


def write_many_lights_scene(name, copies):
    # Split each light's power across colocated emitters so both scenes have the same reference energy.
    lights = []
    for i in range(24):
        angle = 2 * math.pi * i / 24
        distance = 2.0 + 0.65 * (i % 5)
        position = (round(distance * math.cos(angle), 3), 1.4, round(distance * math.sin(angle), 3))
        radiance = [(1, 0.8, 0.6), (0.6, 0.8, 1), (0.8, 1, 0.7)][i % 3]
        power = 0.4 * 2 ** (i % 12)
        for j in range(copies):
            lights.append(point(f"Light {i}-{j}", position, radiance, power / copies, 8.0))
    write(name, [
        nosky(),
        floor("Materials/GI/GI_White.lmat"),
        mesh("Emissive mesh", "Cube", (0.5, 1.6, 0), (0.8, 0.3, 0.8), "GI_ReflectionEmitter"),
        camera((0, 9, 7), (-1.0, 0, 0)),
        *lights,
    ])


def write_mesh_emitter_scene():
    write("GI_MeshEmitter", [
        nosky(),
        floor("Materials/GI/GI_White.lmat"),
        mesh("Small emitter", "Cube", (0.7, 1.2, -0.3), (0.38, 0.04, 0.38), "GI_Emit"),
        camera((0, 2.4, 4.4), (-0.50, 0, 0)),
    ])


def write_rt_visibility_scenes():
    sun = entity("Sun", (0, 0, 0), rot=(-0.84, 0, 0), body="""    DirectionalLightComponent:
      Intensity: 3
      Radiance: [1, 1, 1]
      Unit: 0
      UseColorTemperature: false
      CastShadows: true
      SoftShadows: false
      LightSize: 0.25
      ShadowAmount: 1
      ShadowDistance: 3
      ShadowResolutionTier: 1
""")
    def geometry():
        return [
            floor("Materials/GI/GI_White.lmat", (12, 0.1, 12)),
            mesh("Low blocker", "Cube", (-1.15, 0.28, -0.35), (0.85, 0.18, 0.6), "GI_RedBall"),
            mesh("High blocker", "Cube", (1.15, 1.40, -0.35), (0.85, 0.18, 0.6), "GI_RedBall"),
            camera((0, 4.4, 5.0), (-0.722, 0, 0)),
        ]

    write("GI_RTContact", [nosky(), sun, *geometry()])
    sky = nosky().replace("Tag: No Sky", "Tag: Sky").replace("Intensity: 0", "Intensity: 1.25")
    write("GI_RTAmbient", [sky, *geometry()])


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

    # Lobed materials on a no-sky scene with point lights: the raster frame and the path tracer see the same direct lighting.
    write_material("GI_Coat", (0.7, 0.05, 0.05), 0.0, 0.55, {"Clearcoat": 1.0, "ClearcoatRoughness": 0.06})
    write_material("GI_Cloth", (0.15, 0.2, 0.5), 0.0, 0.85, {"SheenColor": "[0.9, 0.9, 1.0]", "SheenRoughness": 0.45})
    write_material("GI_Brushed", (0.9, 0.7, 0.4), 1.0, 0.35, {"Anisotropy": 0.85})
    write("GI_Lobes", [nosky(), floor("Materials/GI/GI_White.lmat", (14, 0.1, 10)),
                       mesh("Coat", "Sphere", (-2.6, 1.0, 0), (1.0, 1.0, 1.0), "GI_Coat"),
                       mesh("Cloth", "Sphere", (0, 1.0, 0), (1.0, 1.0, 1.0), "GI_Cloth"),
                       mesh("Brushed", "Sphere", (2.6, 1.0, 0), (1.0, 1.0, 1.0), "GI_Brushed"),
                       point("Key", (-3.5, 4.0, 3.0), (1, 0.95, 0.9), 120, 20),
                       point("Rim", (3.5, 3.0, -3.0), (0.8, 0.9, 1.0), 70, 20),
                       point("Fill", (0.5, 2.0, 4.5), (1, 1, 1), 40, 20),
                       camera((0, 3.2, 7.5), (-0.33, 0, 0))])

    # Glass: a clear sphere in the furnace, and an absorbing slab with index 1 (an invisible interface) for Beer-Lambert.
    glass = ["FormatVersion: 1", "Material:", "  Transparent: true", "  AlbedoColor: [1, 1, 1]", "  Emission: 0", "  UseNormalMap: false", "  Roughness: 0", "  AlbedoMap: 0", "  NormalMap: 0",
             "  MetalnessMap: 0", "  RoughnessMap: 0", "  Transparency: 1", "  Transmission: 1", "  IOR: 1.5", "  Thickness: 1", "  MaterialFlags: 69"]
    (MATERIALS / "GI_Glass.lmat").write_text(chr(10).join(glass), encoding="utf-8", newline=chr(10))
    slab = [line.replace("IOR: 1.5", "IOR: 1.0") for line in glass[:-1]] + ["  AttenuationColor: [0.5, 0.5, 0.5]", "  AttenuationDistance: 1", "  MaterialFlags: 69"]
    (MATERIALS / "GI_GlassSlab.lmat").write_text(chr(10).join(slab), encoding="utf-8", newline=chr(10))
    base = (SCENES / "GI_Furnace.luxscene").read_text(encoding="utf-8")
    for scene_name, material_name in (("GI_GlassFurnace", "GI_Glass"), ("GI_GlassSlab", "GI_GlassSlab")):
        text = base.replace("GI_Furnace.lmat", f"{material_name}.lmat").replace("Scene: GI_Furnace", f"Scene: {scene_name}")
        if scene_name == "GI_GlassSlab":
            text = text.replace("Sphere.gltf", "Cube.gltf").replace("Scale: [3, 3, 3]", "Scale: [6, 6, 0.5]")
        (SCENES / f"{scene_name}.luxscene").write_text(text, encoding="utf-8", newline=chr(10))
    # A sun, a box and a sphere on a white floor, no sky: shadows only. The sun's shadow distance is 3 m, so the shadow maps end well before the objects (10 m from the camera).
    sun = entity("Sun", (0, 0, 0), rot=(-0.85, 0.6, 0), body="""    DirectionalLightComponent:
      Intensity: 3
      Radiance: [1, 0.95, 0.9]
      Unit: 0
      ColorTemperature: 6500
      UseColorTemperature: false
      CastShadows: true
      SoftShadows: true
      LightSize: 0.5
      ShadowAmount: 1
      ShadowDistance: 3
      ShadowResolutionTier: 2
""")
    write("GI_Shadows", [nosky(), sun, floor("Materials/GI/GI_White.lmat", (20, 0.1, 14)),
                         mesh("Box", "Cube", (-1.6, 1.0, 0), (1.6, 2.0, 1.6), "GI_White"),
                         mesh("Ball", "Sphere", (2.2, 0.8, 0.6), (1.6, 1.6, 1.6), "GI_White"),
                         camera((0, 5.5, 9), (-0.5, 0, 0))])

    # A polished metal floor under a red sphere and two coloured lamps (no sky): its reflection is what ray-traced reflections add.
    write_material("GI_Mirror", (0.95, 0.95, 0.95), 1.0, 0.04)
    write_material("GI_RedBall", (0.9, 0.1, 0.08), 0.0, 0.6)
    write("GI_Mirror", [nosky(), floor("Materials/GI/GI_Mirror.lmat", (16, 0.1, 12)),
                        mesh("Ball", "Sphere", (0, 1.0, 0), (2, 2, 2), "GI_RedBall"),
                        mesh("Blue Box", "Cube", (-3.2, 0.75, -1.0), (1.5, 1.5, 1.5), "GI_White"),
                        point("Key", (-2.5, 4.5, 3.0), (1, 0.95, 0.9), 150, 25),
                        point("Rim", (3.5, 3.0, -2.0), (0.5, 0.7, 1.0), 90, 25),
                        camera((0, 3.4, 8.5), (-0.28, 0, 0))])

    # The same polished floor under a bright sky: a reflection ray that hits the ball must replace the sky reflection, not add to it.
    mirror_text = (SCENES / "GI_Mirror.luxscene").read_text(encoding="utf-8").replace("Scene: GI_Mirror", "Scene: GI_MirrorSky")
    mirror_text = mirror_text.replace("Intensity: 0\n      Lod: 0", "Intensity: 1.5\n      Lod: 0", 1)
    (SCENES / "GI_MirrorSky.luxscene").write_text(mirror_text, encoding="utf-8", newline=chr(10))

    # A tinted glass plate beside a point light: its shadow on the floor must carry the glass colour (the camera never looks through the plate).
    tint = [line.replace("AttenuationDistance: 1", "AttenuationDistance: 0.2").replace("[0.5, 0.5, 0.5]", "[0.9, 0.15, 0.1]") for line in slab]
    (MATERIALS / "GI_GlassTint.lmat").write_text(chr(10).join(tint), encoding="utf-8", newline=chr(10))
    write("GI_GlassShadow", [nosky(), floor("Materials/GI/GI_White.lmat", (14, 0.1, 10)),
                             mesh("Plate", "Cube", (-1.8, 1.2, 0), (1.5, 0.2, 2.5), "GI_GlassTint"),
                             point("Key", (-4.5, 3.2, 0), (1, 1, 1), 160, 30),
                             camera((0, 4.5, 6.5), (-0.62, 0, 0))])

    # A floor and a wall in ONE submesh whose UVs are both the full 0..1 square: their lightmap UVs overlap completely until the mesh is unwrapped.
    import base64, json, struct
    pos = [(-4, 0, -3), (4, 0, -3), (4, 0, 3), (-4, 0, 3), (-4, 0, -3), (4, 0, -3), (4, 4, -3), (-4, 4, -3)]
    nrm = [(0, 1, 0)] * 4 + [(0, 0, 1)] * 4
    uvs = [(0, 0), (1, 0), (1, 1), (0, 1)] * 2
    idx = [0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7]
    blob = b"".join(struct.pack("<3f", *v) for v in pos) + b"".join(struct.pack("<3f", *v) for v in nrm) + b"".join(struct.pack("<2f", *v) for v in uvs) + struct.pack("<12H", *idx)
    gltf = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3}]}],
            "buffers": [{"byteLength": len(blob), "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode()}],
            "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 96}, {"buffer": 0, "byteOffset": 96, "byteLength": 96}, {"buffer": 0, "byteOffset": 192, "byteLength": 64}, {"buffer": 0, "byteOffset": 256, "byteLength": 24}],
            "accessors": [{"bufferView": 0, "componentType": 5126, "count": 8, "type": "VEC3", "min": [-4, 0, -3], "max": [4, 4, 3]}, {"bufferView": 1, "componentType": 5126, "count": 8, "type": "VEC3"},
                          {"bufferView": 2, "componentType": 5126, "count": 8, "type": "VEC2"}, {"bufferView": 3, "componentType": 5123, "count": 12, "type": "SCALAR"}]}
    gi_meshes = ROOT / "Editor/LuxSampleProject/Assets/Meshes/Source/GI"
    gi_meshes.mkdir(parents=True, exist_ok=True)
    (gi_meshes / "LightmapOverlap.gltf").write_text(json.dumps(gltf), encoding="utf-8", newline=chr(10))
    # The low emissive box lights the nearby floor more than the upper wall.
    # Keep it left of the wall and floor regions sampled by the check.
    write_material("GI_Emit", (1.0, 0.85, 0.6), 0.0, 0.5)
    emit = (MATERIALS / "GI_Emit.lmat").read_text(encoding="utf-8").replace("Emission: 0", "Emission: 30")
    (MATERIALS / "GI_Emit.lmat").write_text(emit, encoding="utf-8", newline=chr(10))
    write("GI_OverlapUV", [nosky(), mesh("Room", "LightmapOverlap", (0, 0, 0), (1, 1, 1), "GI_White", folder="GI"),
                           mesh("Lamp", "Cube", (-2.3, 1.1, 1.6), (2.2, 0.12, 2.0), "GI_Emit"), camera((0, 3.0, 8.0), (-0.2, 0, 0))])

    # Subsurface: a white-scattering sphere in the furnace, and a thin warm slab lit only from behind.
    write_material("GI_SSS", (1.0, 1.0, 1.0), 0.0, 0.5, {"SubsurfaceColor": "[1, 1, 1]", "SubsurfaceThickness": 0.0})
    base_furnace = (SCENES / "GI_Furnace.luxscene").read_text(encoding="utf-8")
    (SCENES / "GI_SSSFurnace.luxscene").write_text(base_furnace.replace("GI_Furnace.lmat", "GI_SSS.lmat").replace("Scene: GI_Furnace", "Scene: GI_SSSFurnace").replace("Scale: [3, 3, 3]", "Scale: [0.8, 0.8, 0.8]"), encoding="utf-8", newline=chr(10))
    write_material("GI_Wax", (0.9, 0.75, 0.55), 0.0, 0.6, {"SubsurfaceColor": "[0.95, 0.8, 0.6]", "SubsurfaceThickness": 0.2})
    write("GI_SSSBacklit", [nosky(), mesh("Wax Slab", "Cube", (0, 0, 0), (4, 4, 0.3), "GI_Wax"), point("Backlight", (0, 0, -1.5), (1, 1, 1), 200, 30), camera((0, 0, 6), (0, 0, 0))])

    # An empty-looking scene (one cube far below the view, so the ray-traced scene exists) for the fog checks: its centre ray crosses
    # only the fog sphere, its corner rays miss it.
    # A point light inside a fog sphere on a black background: the fog glows around it (next-event estimation in the medium).
    write("GI_FogLight", [nosky(), mesh("Far Cube", "Cube", (0, -100, 0), (1, 1, 1), "GI_White"), point("Lamp", (0, 0, 0), (1, 1, 1), 60, 30), camera((0, 0, 8), (0, 0, 0))])
    write("GI_FogEmpty", [mesh("Far Cube", "Cube", (0, -100, 0), (1, 1, 1), "GI_White"), camera((0, 0, 8), (0, 0, 0))])
    write_fog_sun_scene()
    write_rough_reflection_scene()
    write_rt_visibility_scenes()

    # The white-furnace sphere with a clearcoat layer: still must not exceed the environment.
    furnace = (SCENES / "GI_Furnace.luxscene").read_text(encoding="utf-8").replace("GI_Furnace.lmat", "GI_FurnaceCoat.lmat").replace("Scene: GI_Furnace", "Scene: GI_FurnaceCoat")
    (SCENES / "GI_FurnaceCoat.luxscene").write_text(furnace, encoding="utf-8", newline=chr(10))
    coat = (MATERIALS / "GI_Furnace.lmat").read_text(encoding="utf-8").rstrip() + chr(10) + "  Clearcoat: 1.0" + chr(10) + "  ClearcoatRoughness: 0.2"
    (MATERIALS / "GI_FurnaceCoat.lmat").write_text(coat, encoding="utf-8", newline=chr(10))

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
