"""Generate the LuxEngine benchmark scene: one scene that exercises every shipped system.

Writes into the sample project:
  Assets/Scenes/Benchmark.luxscene   the scene
  Assets/Materials/Benchmark/*.lmat  generated PBR materials (the draw-state variety bindless targets)
  Assets/AssetRegistry.lzr           upserts the generated materials under fixed handles

Everything is deterministic: the same arguments always write byte-identical files, so a scene
measured before a change is the scene measured after it. Re-running replaces the previous output.

What the scene holds, by district:
  Plaza (centre)        PBR spheres, glass pavilion (forward/transparent pass), emissive lamp posts,
                        spot lights (two shadowed), FMOD emitter + script, music director
  Material field (N)    --objects mixed primitives over --materials distinct materials
  Sponza (E)            the textured Sponza mesh, triangle mesh collider for acoustics, an ambience zone
  Village (W)           six houses: textured walls with acoustic mesh colliders, audio zones, doors with
                        audio portals + SlidingDoor, a shadowed point light and a looping emitter inside
  Physics yard (S)      --bodies dynamic boxes/spheres/capsules, a ramp, a compound-collider table,
                        a kinematic platform, a trigger volume, a character controller
  2D corner (far S)     Box2D sprites and circles, textured sprites
  Everywhere            --lights coloured point lights, sun with cascaded shadows, dynamic sky,
                        world- and screen-space text

Play mode drives a repeatable camera flight (CameraPath, PlayOnStart) through every district, so a
Play-mode capture is the reproducible measurement. In Edit mode, place the editor camera yourself.
"""

from __future__ import annotations

import argparse
import json
import math
import re
from dataclasses import dataclass, field
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROJECT_ASSETS = ROOT / "Editor" / "LuxSampleProject" / "Assets"
SCENE_PATH = PROJECT_ASSETS / "Scenes" / "Benchmark.luxscene"
MATERIAL_DIR_RELATIVE = "Materials/Benchmark"
REGISTRY_PATH = PROJECT_ASSETS / "AssetRegistry.lzr"

# Mesh sources already in the sample project's registry.
CUBE = 5047013130077771705
SPHERE = 9685142686327195147
CYLINDER = 12947748576387674464
CAPSULE = 3662333232878718086
SPONZA = 5094136855824747147

# Sample-project textures (1K PBR sets), by folder name.
TEXTURE_SETS = (
	("Bricks097", True, False, "Brick"),
	("Carpet016", True, False, "Carpet"),
	("Metal049A", False, True, "Metal"),
	("Plaster001", False, False, "Plaster"),
	("RoofingTiles013A", True, False, "Ceramic"),
	("WoodFloor043", True, False, "Wood"),
)
CHECKERBOARD_TEXTURE = 4617627115470858524
LOGO_TEXTURE = 3523050505664199236

# FMOD Studio events in the sample project's Master bank.
EVENT_FART = "{0e6cd051-9f6f-4b17-95e6-f2b1b5a4cc40}"
EVENT_MUSIC = "{755abc01-cd89-4daf-a27b-04d57b807b3d}"

# Stable id ranges: generated materials and scene entities never collide with editor-made UUIDs
# in practice, and regenerating keeps every reference valid.
MATERIAL_HANDLE_BASE = 16000000000000000000
ENTITY_UUID_BASE = 16100000000000000000

CAMERA_NAME = "Listener Camera - RMB + WASD"  # CameraPath's default Target; string fields don't serialize


def fnv1a(text: str) -> int:
	"""Hash::GenerateFNVHash: FNV-1a over the bytes plus the terminating NUL."""
	value = 2166136261
	for byte in text.encode("utf-8") + b"\0":
		value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
	return value


def num(value: float) -> str:
	text = f"{value:.6g}"
	return "0" if text == "-0" else text


def vec(values) -> str:
	return "[" + ", ".join(num(v) for v in values) + "]"


def text_value(value: str) -> str:
	"""A YAML scalar; JSON string syntax is valid YAML double-quoted."""
	if value == "":
		return '""'
	if re.fullmatch(r"[A-Za-z_][A-Za-z0-9 _.()+\-]*", value) and value == value.strip():
		return value
	return json.dumps(value)


def boolean(value: bool) -> str:
	return "true" if value else "false"


# --------------------------------------------------------------------------------------------------
# Materials


@dataclass
class Material:
	handle: int
	name: str
	lines: list[str]


def textured_material(handle: int, name: str, texture_set: tuple, variant: int) -> Material:
	folder, has_ao, has_metal, acoustic = texture_set
	base = f"Textures/{folder}/{folder}_1K-JPG"
	tint_steps = ((1.0, 1.0, 1.0), (1.0, 0.86, 0.72), (0.74, 0.86, 1.0), (0.86, 1.0, 0.78), (1.0, 0.78, 0.86), (0.92, 0.92, 0.92))
	tint = tint_steps[variant % len(tint_steps)]
	tiling = 1.0 + (variant % 4) * 0.5
	lines = [
		"  Transparent: false",
		f"  AlbedoColor: {vec(tint)}",
		"  Emission: 0",
		"  UseNormalMap: true",
		f"  Metalness: {num(1.0 if has_metal else 0.0)}",
		f"  Roughness: {num(1.0 - (variant % 3) * 0.15)}",
		f"  AlbedoMap: {base}_Color.jpg",
		f"  NormalMap: {base}_NormalGL.jpg",
		f"  MetalnessMap: {base + '_Metalness.jpg' if has_metal else '0'}",
		f"  RoughnessMap: {base}_Roughness.jpg",
	]
	if has_ao:
		lines.append(f"  OcclusionMap: {base}_AmbientOcclusion.jpg")
	lines.append(f"  UVTiling: {vec((tiling, tiling))}")
	if variant % 5 == 4:
		lines.append(f"  UVRotation: {num(0.785398)}")
	lines.append(f"  AcousticMaterial: {acoustic}")
	lines.append("  MaterialFlags: 6")
	return Material(handle, name, lines)


def sponza_material(handle: int, name: str, gltf_material: dict, images: list[str], variant: int) -> Material:
	pbr = gltf_material.get("pbrMetallicRoughness", {})

	def image(slot: dict | None) -> str:
		if not slot:
			return "0"
		return f"Meshes/Source/Sponza/{images[slot['index']]}"

	albedo = image(pbr.get("baseColorTexture"))
	normal = image(gltf_material.get("normalTexture"))
	packed = image(pbr.get("metallicRoughnessTexture"))
	hue = ((1.0, 1.0, 1.0), (1.0, 0.9, 0.8), (0.85, 0.9, 1.0))[variant % 3]
	lines = [
		"  Transparent: false",
		f"  AlbedoColor: {vec(hue)}",
		"  Emission: 0",
		f"  UseNormalMap: {boolean(normal != '0')}",
		"  Metalness: 1",
		"  Roughness: 1",
		f"  AlbedoMap: {albedo}",
		f"  NormalMap: {normal}",
		f"  MetalnessMap: {packed}",
		f"  RoughnessMap: {packed}",
	]
	if packed != "0":
		lines += ["  MetalnessChannel: B", "  RoughnessChannel: G"]
	lines += [f"  UVTiling: {vec((1.0 + variant % 2, 1.0 + variant % 2))}", "  AcousticMaterial: Rock", "  MaterialFlags: 6"]
	return Material(handle, name, lines)


def plain_material(handle: int, name: str, lines: list[str]) -> Material:
	return Material(handle, name, lines + ["  MaterialFlags: 6"])


def build_materials(count: int) -> tuple[list[Material], dict[str, int]]:
	"""`count` field materials (half sample PBR sets, half Sponza texture sets) plus named specials."""
	sponza = json.loads((PROJECT_ASSETS / "Meshes/Source/Sponza/Sponza.gltf").read_text(encoding="utf-8"))
	images = [entry["uri"] for entry in sponza["images"]]
	sponza_materials = [m for m in sponza["materials"] if m.get("pbrMetallicRoughness", {}).get("baseColorTexture")]

	materials: list[Material] = []
	for i in range(count):
		handle = MATERIAL_HANDLE_BASE + i
		name = f"Bench_{i:03d}"
		if i % 2 == 0 or not sponza_materials:
			materials.append(textured_material(handle, name, TEXTURE_SETS[(i // 2) % len(TEXTURE_SETS)], i // 12))
		else:
			source = sponza_materials[(i // 2) % len(sponza_materials)]
			materials.append(sponza_material(handle, name, source, images, i // (2 * len(sponza_materials))))

	special_base = MATERIAL_HANDLE_BASE + 900
	specials = {
		"Ground": textured_material(special_base + 0, "Bench_Ground", TEXTURE_SETS[3], 0),
		"Glass": plain_material(special_base + 1, "Bench_Glass", [
			"  Transparent: true", "  AlbedoColor: [0.75, 0.9, 1]", "  Emission: 0", "  UseNormalMap: false",
			"  Roughness: 0.05", "  Transparency: 0.3", "  AlbedoMap: 0", "  NormalMap: 0", "  MetalnessMap: 0",
			"  RoughnessMap: 0", "  AcousticMaterial: Glass"]),
		"GlassAmber": plain_material(special_base + 2, "Bench_GlassAmber", [
			"  Transparent: true", "  AlbedoColor: [1, 0.7, 0.35]", "  Emission: 0", "  UseNormalMap: false",
			"  Roughness: 0.1", "  Transparency: 0.45", "  AlbedoMap: 0", "  NormalMap: 0", "  MetalnessMap: 0",
			"  RoughnessMap: 0", "  AcousticMaterial: Glass"]),
		"Lamp": plain_material(special_base + 3, "Bench_Lamp", [
			"  Transparent: false", "  AlbedoColor: [1, 0.85, 0.6]", "  Emission: 6", "  UseNormalMap: false",
			"  Metalness: 0", "  Roughness: 0.4", "  AlbedoMap: 0", "  NormalMap: 0", "  MetalnessMap: 0",
			"  RoughnessMap: 0", "  EmissiveColor: [1, 0.8, 0.5]"]),
		"Neon": plain_material(special_base + 4, "Bench_Neon", [
			"  Transparent: false", "  AlbedoColor: [0.2, 0.6, 1]", "  Emission: 10", "  UseNormalMap: false",
			"  Metalness: 0", "  Roughness: 0.3", "  AlbedoMap: 0", "  NormalMap: 0", "  MetalnessMap: 0",
			"  RoughnessMap: 0", "  EmissiveColor: [0.2, 0.6, 1]"]),
		"Chrome": plain_material(special_base + 5, "Bench_Chrome", [
			"  Transparent: false", "  AlbedoColor: [0.95, 0.95, 0.95]", "  Emission: 0", "  UseNormalMap: false",
			"  Metalness: 1", "  Roughness: 0.08", "  AlbedoMap: 0", "  NormalMap: 0", "  MetalnessMap: 0",
			"  RoughnessMap: 0", "  AcousticMaterial: Metal"]),
		"Brick": textured_material(special_base + 6, "Bench_Brick", TEXTURE_SETS[0], 0),
		"Plaster": textured_material(special_base + 7, "Bench_Plaster", TEXTURE_SETS[3], 0),
		"Roof": textured_material(special_base + 8, "Bench_Roof", TEXTURE_SETS[4], 0),
		"Wood": textured_material(special_base + 9, "Bench_Wood", TEXTURE_SETS[5], 0),
		"Carpet": textured_material(special_base + 10, "Bench_Carpet", TEXTURE_SETS[1], 0),
		"Metal": textured_material(special_base + 11, "Bench_Metal", TEXTURE_SETS[2], 0),
	}
	# The ground is 260 m across: tile it far more than a wall.
	specials["Ground"].lines = [line if not line.startswith("  UVTiling") else "  UVTiling: [90, 90]" for line in specials["Ground"].lines]
	specials["Ground"].lines = [line if not line.startswith("  AcousticMaterial") else "  AcousticMaterial: Concrete" for line in specials["Ground"].lines]

	materials += specials.values()
	return materials, {key: value.handle for key, value in specials.items()}


def write_materials(materials: list[Material]) -> None:
	directory = PROJECT_ASSETS / MATERIAL_DIR_RELATIVE
	directory.mkdir(parents=True, exist_ok=True)
	wanted = {f"{m.name}.lmat" for m in materials}
	for stale in directory.glob("Bench_*.lmat"):
		if stale.name not in wanted:
			stale.unlink()
	for material in materials:
		content = "Material:\n" + "\n".join(material.lines)
		(directory / f"{material.name}.lmat").write_text(content, encoding="utf-8", newline="\n")


def update_registry(materials: list[Material]) -> None:
	"""Replace every Materials/Benchmark entry with the generated set; leave all other entries as-is."""
	text = REGISTRY_PATH.read_text(encoding="utf-8")
	header, _, body = text.partition("Assets:\n")
	blocks = re.split(r"(?m)^(?=  - Handle: )", body)
	kept = [b for b in blocks if b and f"FilePath: {MATERIAL_DIR_RELATIVE}/" not in b]
	if kept and not kept[-1].endswith("\n"):
		kept[-1] += "\n"
	for material in materials:
		kept.append(
			f"  - Handle: {material.handle}\n"
			f"    FilePath: {MATERIAL_DIR_RELATIVE}/{material.name}.lmat\n"
			f"    Type: Material\n")
	REGISTRY_PATH.write_text(header + "Assets:\n" + "".join(kept), encoding="utf-8", newline="\n")


# --------------------------------------------------------------------------------------------------
# Scene model


@dataclass
class Entity:
	uuid: int
	tag: str
	position: tuple = (0.0, 0.0, 0.0)
	rotation: tuple = (0.0, 0.0, 0.0)
	scale: tuple = (1.0, 1.0, 1.0)
	parent: "Entity | None" = None
	children: list["Entity"] = field(default_factory=list)
	folder: bool = False
	components: list[tuple[str, list[str]]] = field(default_factory=list)

	def add(self, name: str, lines: list[str]) -> "Entity":
		self.components.append((name, lines))
		return self


class Scene:
	def __init__(self, name: str) -> None:
		self.name = name
		self.entities: list[Entity] = []
		self.next_uuid = ENTITY_UUID_BASE
		self.stats: dict[str, int] = {}
		self.outdoor_zone: Entity | None = None  # set by add_environment; house portals open onto it

	def count(self, key: str, amount: int = 1) -> None:
		self.stats[key] = self.stats.get(key, 0) + amount

	def entity(self, tag: str, position=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), parent: Entity | None = None) -> Entity:
		entity = Entity(self.next_uuid, tag, tuple(position), tuple(rotation), tuple(scale), parent)
		self.next_uuid += 1
		if parent:
			parent.children.append(entity)
		self.entities.append(entity)
		return entity

	def folder(self, tag: str, parent: Entity | None = None) -> Entity:
		entity = self.entity(tag, parent=parent)
		entity.folder = True
		return entity

	def write(self, path: Path) -> None:
		out = [f"Scene: {self.name}", "PostProcess:"]
		out += [f"  {line}" for line in (
			"Exposure: 1", "ExposureMode: 0", "Aperture: 16", "ShutterSpeed: 0.008", "ISO: 100",
			"ExposureEV100: 12", "ExposureCompensation: 0", "AutoMinEV100: -2", "AutoMaxEV100: 16",
			"AutoAdaptationSpeedUp: 3", "AutoAdaptationSpeedDown: 1", "ColorFilter: [1, 1, 1]", "Saturation: 1",
			"Contrast: 1", "Gamma: 2.2", "Tonemap: 0", "WhiteTemperature: 0", "WhiteTint: 0", "Lift: [0, 0, 0]",
			"GradeGamma: [1, 1, 1]", "Gain: [1, 1, 1]")]
		out.append("Entities:")
		for e in self.entities:
			out.append(f"  - Entity: {e.uuid}")
			out.append("    TagComponent:")
			out.append(f"      Tag: {text_value(e.tag)}")
			if e.folder:
				out.append("    Folder: true")
			out.append(f"    Parent: {e.parent.uuid if e.parent else 0}")
			out.append("    Children:")
			if e.children:
				out += [f"      - Handle: {child.uuid}" for child in e.children]
			else:
				out.append("      []")
			out.append("    TransformComponent:")
			out.append(f"      Position: {vec(e.position)}")
			out.append(f"      Rotation: {vec(e.rotation)}")
			out.append(f"      Scale: {vec(e.scale)}")
			for name, lines in e.components:
				out.append(f"    {name}:")
				out += [f"      {line}" for line in lines]
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")


# --------------------------------------------------------------------------------------------------
# Component helpers. Keys and order mirror SceneSerializer::SerializeEntity.


def static_mesh(entity: Entity, mesh: int, material: int | None = None, scene: Scene | None = None) -> Entity:
	table = [f"  0: {material}"] if material else ["  {}"]
	if scene:
		scene.count("static meshes")
	return entity.add("StaticMeshComponent", [f"AssetID: {mesh}", "MaterialTable:", *table, "Visible: true"])


def rigid_body(entity: Entity, body_type: int, mass: float = 1.0, trigger: bool = False, continuous: bool = False,
			   velocity=(0.0, 0.0, 0.0), angular=(0.0, 0.0, 0.0), locked_axes: int = 0) -> Entity:
	return entity.add("RigidBodyComponent", [
		f"BodyType: {body_type}", "LayerID: 0", "EnableDynamicTypeChange: false", f"Mass: {num(mass)}",
		"LinearDrag: 0.01", "AngularDrag: 0.05", "DisableGravity: false", f"IsTrigger: {boolean(trigger)}",
		f"CollisionDetection: {1 if continuous else 0}", f"InitialLinearVelocity: {vec(velocity)}",
		f"InitialAngularVelocity: {vec(angular)}", "MaxLinearVelocity: 500", "MaxAngularVelocity: 50",
		f"LockedAxes: {locked_axes}"])


def physics_material(friction: float = 0.5, restitution: float = 0.0) -> list[str]:
	return ["Density: 1", f"Friction: {num(friction)}", f"Restitution: {num(restitution)}"]


def box_collider(entity: Entity, half=(0.5, 0.5, 0.5), offset=(0.0, 0.0, 0.0), friction: float = 0.5, restitution: float = 0.0) -> Entity:
	return entity.add("BoxColliderComponent", [f"HalfSize: {vec(half)}", f"Offset: {vec(offset)}", *physics_material(friction, restitution)])


def sphere_collider(entity: Entity, radius: float = 0.5, restitution: float = 0.0) -> Entity:
	return entity.add("SphereColliderComponent", [f"Radius: {num(radius)}", "Offset: [0, 0, 0]", *physics_material(0.5, restitution)])


def capsule_collider(entity: Entity, radius: float = 0.5, half_height: float = 0.5) -> Entity:
	return entity.add("CapsuleColliderComponent", [f"Radius: {num(radius)}", f"HalfHeight: {num(half_height)}", "Offset: [0, 0, 0]", *physics_material()])


def mesh_collider(entity: Entity, mesh: int, acoustic: str) -> Entity:
	# CollisionComplexity is a uint8_t, which yaml-cpp round-trips as a character: "\x02" is
	# UseComplexAsSimple (a triangle mesh), exactly what the editor writes.
	return entity.add("MeshColliderComponent", [
		f"ColliderAsset: {mesh}", "SubmeshIndex: 0", "UseSharedShape: false", *physics_material(),
		f"AcousticMaterial: {acoustic}", "AcousticFromMaterial: true", "AcousticMotion: 0",
		'CollisionComplexity: "\\x02"'])


def event_ref(guid: str) -> list[str]:
	return [f"  Guid: {text_value(guid)}", '  Path: ""', '  BankName: ""']


def audio_source(entity: Entity, event: str, volume: float, play_on_awake: bool, priority: int = 128) -> Entity:
	return entity.add("AudioSourceComponent", [
		f"VolumeMultiplier: {num(volume)}", "PitchMultiplier: 1", f"PlayOnAwake: {boolean(play_on_awake)}",
		f"Priority: {priority}", "DistanceCulling: true", f"EventGuid: {text_value(event)}", 'EventPath: ""',
		'EventBank: ""', "ParameterOverrides:", "  []"])


def audio_zone(entity: Entity, half_extents, priority: float, ambience: str = "", volume: float = 1.0) -> Entity:
	return entity.add("AudioZoneComponent", [
		"Shape: 0", "Enabled: true", "Offset: [0, 0, 0]", f"HalfExtents: {vec(half_extents)}", "Radius: 5",
		f"Priority: {num(priority)}", "BlendDistance: 1.5", "FadeTime: 0.5", f"Volume: {num(volume)}",
		"AmbienceEvent:", *event_ref(ambience), "Snapshot:", *event_ref("")])


def audio_surface(entity: Entity, acoustic: str, footsteps: bool = False) -> Entity:
	return entity.add("AudioSurfaceComponent", [
		f"Material: {acoustic}", "PhysicsSounds: true", f"AutoFootsteps: {boolean(footsteps)}", "StrideLength: 0.7",
		"GroundProbeDistance: 1.2", "FootstepWeight: 75", "FootstepOverride:", *event_ref(""),
		"ImpactOverride:", *event_ref("")])


def script(entity: Entity, class_name: str, fields: list[tuple[str, str, str]] | None = None) -> Entity:
	lines = [f"ClassName: {class_name}", f"ScriptID: {fnv1a(class_name)}"]
	if fields:
		lines.append("StoredFields:")
		for name, type_name, data in fields:
			lines += [f"  - ID: {fnv1a(f'{class_name}.{name}')}", f"    Name: {name}", f"    Type: {type_name}", f"    Data: {data}"]
	return entity.add("ScriptComponent", lines)


def point_light(entity: Entity, color, intensity: float, radius: float, shadows: bool = False) -> Entity:
	return entity.add("PointLightComponent", [
		f"Radiance: {vec(color)}", f"Intensity: {num(intensity)}", "Unit: 0", "ColorTemperature: 6500",
		"UseColorTemperature: false", f"CastShadows: {boolean(shadows)}", f"SoftShadows: {boolean(shadows)}",
		"MinRadius: 0.5", f"Radius: {num(radius)}", "LightSize: 0.3", "Falloff: 1"])


def spot_light(entity: Entity, color, intensity: float, angle: float, range_: float, shadows: bool = False) -> Entity:
	return entity.add("SpotLightComponent", [
		f"Radiance: {vec(color)}", f"Angle: {num(angle)}", "AngleAttenuation: 5", f"CastsShadows: {boolean(shadows)}",
		f"SoftShadows: {boolean(shadows)}", "Falloff: 1", f"Intensity: {num(intensity)}", "Unit: 0",
		"ColorTemperature: 6500", "UseColorTemperature: false", f"Range: {num(range_)}", "ShadowDistance: 0",
		"ShadowResolutionTier: 1"])


def text(entity: Entity, string: str, color=(1.0, 1.0, 1.0, 1.0), screen_space: bool = False, max_width: float = 30.0) -> Entity:
	return entity.add("TextComponent", [
		f"TextString: {json.dumps(string)}", "FontHandle: 0", f"Color: {vec(color)}", "LineSpacing: 0.25",
		"Kerning: 0", f"MaxWidth: {num(max_width)}", f"ScreenSpace: {boolean(screen_space)}", "DropShadow: true",
		"ShadowDistance: 0.04", "ShadowColor: [0, 0, 0, 1]"])


PALETTE = ((1.0, 0.55, 0.32), (0.28, 0.78, 1.0), (0.5, 1.0, 0.48), (1.0, 0.34, 0.7), (0.94, 0.88, 0.42), (0.7, 0.5, 1.0))


# --------------------------------------------------------------------------------------------------
# Districts


def add_environment(scene: Scene, specials: dict[str, int]) -> None:
	env = scene.folder("Environment")
	sun = scene.entity("Sun", rotation=(-0.75, -0.55, 0.0), parent=env)
	sun.add("DirectionalLightComponent", [
		"Intensity: 3", "Radiance: [1, 0.94, 0.85]", "Unit: 0", "ColorTemperature: 6500", "UseColorTemperature: false",
		"CastShadows: true", "SoftShadows: true", "LightSize: 0.5", "ShadowAmount: 1", "ShadowDistance: 0",
		"ShadowResolutionTier: 2"])
	sky = scene.entity("Sky", parent=env)
	sky.add("SkyLightComponent", ["EnvironmentMap: 0", "Intensity: 0.6", "Lod: 0", "DynamicSky: true", "TurbidityAzimuthInclination: [2, 0, 0.6]"])

	ground = scene.entity("Ground", (0.0, -0.5, 0.0), scale=(260.0, 1.0, 260.0), parent=env)
	static_mesh(ground, CUBE, specials["Ground"], scene)
	mesh_collider(ground, CUBE, "Concrete")
	audio_surface(ground, "Concrete", footsteps=True)

	outdoor = scene.entity("Outdoor Audio Zone", (0.0, 20.0, 0.0), parent=env)
	audio_zone(outdoor, (130.0, 20.0, 130.0), priority=0.0)
	scene.outdoor_zone = outdoor

	hud = scene.entity("HUD", (0.02, 0.96, 0.0), scale=(0.035, 0.035, 0.035), parent=env)
	text(hud, "LUX BENCHMARK - Play runs the camera flight (C toggles)", screen_space=True)


def add_camera(scene: Scene) -> None:
	camera = scene.entity(CAMERA_NAME, (0.0, 10.0, 70.0), (-0.12, 0.0, 0.0))
	camera.add("CameraComponent", [
		"Camera:", "  ProjectionType: 0", "  PerspectiveFOV: 50", "  PerspectiveNear: 0.1", "  PerspectiveFar: 800",
		"  OrthographicSize: 10", "  OrthographicNear: -1", "  OrthographicFar: 1", "Primary: true", "FixedAspectRatio: false"])
	script(camera, "LuxSample.FlyCamera", [("Speed", "Float", "12"), ("SprintMultiplier", "Float", "3")])
	camera.add("AudioListenerComponent", ["Active: true", "ListenerIndex: 0", "Weight: 1", "UseAttenuationTarget: false", "AttenuationTarget: 0"])

	# CameraPath looks at its LookAt entity when one exists; none is named that here, so the camera
	# looks ahead along the flight. Waypoints are children, flown in order and looped.
	path = scene.entity("Benchmark Camera Path (C)")
	script(path, "LuxSample.CameraPath", [("SecondsPerPoint", "Float", "5"), ("Loop", "Bool", "true"), ("PlayOnStart", "Bool", "true")])
	waypoints = (
		(0, 10, 70), (0, 9, 20), (-20, 6, -35), (0, 4, -75), (30, 7, -110), (55, 9, -40), (66, 4, 10), (92, 4, 10),
		(105, 10, 45), (40, 7, 55), (0, 5, 88), (-45, 6, 72), (-87.5, 3, 42), (-87.5, 3, -18), (-55, 12, -30), (-25, 14, 40),
	)
	for i, point in enumerate(waypoints):
		scene.entity(f"Waypoint {i + 1:02d}", point, parent=path)


def add_plaza(scene: Scene, specials: dict[str, int], field_materials: list[int]) -> None:
	plaza = scene.folder("Plaza")
	pedestal = scene.entity("Pedestal", (0.0, 0.6, 0.0), scale=(3.0, 0.6, 3.0), parent=plaza)
	static_mesh(pedestal, CYLINDER, specials["Metal"], scene)
	mesh_collider(pedestal, CYLINDER, "Metal")

	emitter = scene.entity("Plaza Emitter - K replays", (0.0, 2.2, 0.0), scale=(1.2, 1.2, 1.2), parent=plaza)
	static_mesh(emitter, SPHERE, specials["Chrome"], scene)
	script(emitter, "LuxSample.FmodAudioDemo", [("PlayOnStart", "Bool", "false"), ("Volume", "Float", "0.6")])
	audio_source(emitter, EVENT_FART, 0.6, play_on_awake=False)
	scene.count("audio sources")

	music = scene.entity("Music Director", (0.0, 4.0, 0.0), parent=plaza)
	music.add("MusicDirectorComponent", ["Event:", *event_ref(EVENT_MUSIC), "PlayOnAwake: true", 'InitialState: ""', "Intensity: 0.5"])

	for i in range(12):
		angle = i / 12.0 * math.tau
		ball = scene.entity(f"PBR Sphere {i:02d}", (math.cos(angle) * 6.0, 0.9, math.sin(angle) * 6.0), scale=(1.6, 1.6, 1.6), parent=plaza)
		static_mesh(ball, SPHERE, field_materials[(i * 11) % len(field_materials)], scene)

	for i in range(10):
		angle = (i + 0.5) / 10.0 * math.tau
		glass = "Glass" if i % 2 == 0 else "GlassAmber"
		panel = scene.entity(f"Glass Panel {i:02d}", (math.cos(angle) * 11.0, 2.0, math.sin(angle) * 11.0),
							 (0.0, -angle + math.pi / 2.0, 0.0), (5.5, 4.0, 0.12), parent=plaza)
		static_mesh(panel, CUBE, specials[glass], scene)
		scene.count("transparent meshes")

	for i in range(8):
		angle = i / 8.0 * math.tau
		x, z = math.cos(angle) * 15.0, math.sin(angle) * 15.0
		post = scene.entity(f"Lamp Post {i}", (x, 2.0, z), scale=(0.25, 2.0, 0.25), parent=plaza)
		static_mesh(post, CYLINDER, specials["Metal"], scene)
		bulb = scene.entity(f"Lamp {i}", (x, 4.3, z), scale=(0.6, 0.6, 0.6), parent=plaza)
		static_mesh(bulb, SPHERE, specials["Lamp" if i % 2 == 0 else "Neon"], scene)
		point_light(bulb, (1.0, 0.8, 0.5) if i % 2 == 0 else (0.2, 0.6, 1.0), 3.0, 10.0)
		scene.count("point lights")
		spot = scene.entity(f"Spot {i}", (x * 0.8, 7.0, z * 0.8), (-1.2, math.atan2(x, z), 0.0), parent=plaza)
		spot_light(spot, PALETTE[i % len(PALETTE)], 6.0, 35.0, 18.0, shadows=(i % 4 == 0))
		scene.count("spot lights")

	label = scene.entity("Plaza Label", (-4.0, 6.5, 12.0), scale=(0.6, 0.6, 0.6), parent=plaza)
	text(label, "PLAZA\nglass, emissive, spot lights")


def add_material_field(scene: Scene, count: int, field_materials: list[int]) -> None:
	folder = scene.folder("Material Field")
	meshes = (CUBE, SPHERE, CYLINDER, CAPSULE)
	width, depth = 90.0, 95.0
	spacing = math.sqrt(width * depth / max(count, 1))
	columns = max(1, int(width / spacing))
	for i in range(count):
		x = -width * 0.5 + (i % columns + 0.5) * spacing
		z = -25.0 - (i // columns + 0.5) * spacing
		size = spacing * 0.7
		mesh = meshes[(i * 3 + i // columns) % len(meshes)]
		height = size * (1.0 + ((i * 7) % 5) * 0.35) if mesh == CUBE else size
		scale = (size, height, size) if mesh != CAPSULE else (size * 0.6, size * 0.5, size * 0.6)
		rotation = (0.0, ((i * 37) % 360) * math.pi / 180.0, 0.0)
		entity = scene.entity(f"Field {i:05d}", (x, (height if mesh == CUBE else size) * 0.5, z), rotation, scale, parent=folder)
		static_mesh(entity, mesh, field_materials[(i * 7 + i // columns) % len(field_materials)], scene)

	label = scene.entity("Field Label", (-10.0, 8.0, -22.0), scale=(0.8, 0.8, 0.8), parent=folder)
	text(label, f"MATERIAL FIELD\n{count} meshes / {len(field_materials)} materials")


def add_lights(scene: Scene, count: int) -> None:
	folder = scene.folder("Light Grid")
	columns = max(1, int(math.sqrt(count * 90.0 / 95.0)))
	rows = max(1, math.ceil(count / columns))
	for i in range(count):
		x = -45.0 + (i % columns + 0.5) * (90.0 / columns)
		z = -25.0 - (i // columns + 0.5) * (95.0 / rows)
		light = scene.entity(f"Grid Light {i:03d}", (x, 2.5 + (i % 3) * 0.75, z), parent=folder)
		point_light(light, PALETTE[i % len(PALETTE)], 4.0, 7.0, shadows=(i % 64 == 0))
		scene.count("point lights")


def add_sponza(scene: Scene) -> None:
	folder = scene.folder("Sponza")
	sponza = scene.entity("Sponza", (80.0, 1.0, 10.0), parent=folder)
	static_mesh(sponza, SPONZA, None, scene)
	mesh_collider(sponza, SPONZA, "Rock")
	zone = scene.entity("Sponza Audio Zone", (80.0, 7.0, 10.0), parent=folder)
	audio_zone(zone, (15.0, 7.0, 9.0), priority=5.0, ambience=EVENT_MUSIC, volume=0.35)
	for i in range(24):
		x = 68.0 + (i % 8) * 3.4
		z = 6.0 if i < 8 else (14.0 if i < 16 else 10.0)
		y = 2.5 if i < 16 else 7.5
		light = scene.entity(f"Sponza Light {i:02d}", (x, y, z), parent=folder)
		point_light(light, (1.0, 0.75, 0.45), 3.0, 8.0, shadows=(i == 20))
		scene.count("point lights")
	label = scene.entity("Sponza Label", (70.0, 16.0, 22.0), scale=(0.8, 0.8, 0.8), parent=folder)
	text(label, "SPONZA\n25 textured materials, mesh collider, ambience zone")


def add_house(scene: Scene, parent: Entity, index: int, cx: float, cz: float, specials: dict[str, int]) -> None:
	house = scene.folder(f"House {index}", parent)

	def part(tag, position, scale, material, acoustic, collider=True):
		entity = scene.entity(tag, (cx + position[0], position[1], cz + position[2]), scale=scale, parent=house)
		static_mesh(entity, CUBE, specials[material], scene)
		if collider:
			mesh_collider(entity, CUBE, acoustic)
		return entity

	part("Back Wall", (0.0, 1.5, -6.85), (14.0, 3.0, 0.3), "Brick", "Brick")
	part("Left Wall", (-6.85, 1.5, -3.4), (0.3, 3.0, 7.0), "Brick", "Brick")
	part("Right Wall", (6.85, 1.5, -3.4), (0.3, 3.0, 7.0), "Brick", "Brick")
	part("Roof", (0.0, 3.15, -3.4), (14.3, 0.3, 7.3), "Roof", "Ceramic")
	part("Front Wall Left", (-3.8, 1.5, 0.0), (6.4, 3.0, 0.35), "Plaster", "Plaster")
	part("Front Wall Right", (3.8, 1.5, 0.0), (6.4, 3.0, 0.35), "Plaster", "Plaster")
	part("Door Lintel", (0.0, 2.55, 0.0), (1.2, 0.9, 0.35), "Plaster", "Plaster")
	floor_material = "Carpet" if index % 2 == 0 else "Wood"
	floor = part("Floor", (0.0, 0.05, -3.4), (13.4, 0.1, 6.6), floor_material, floor_material)
	audio_surface(floor, floor_material, footsteps=True)

	zone = scene.entity("Interior Audio Zone", (cx, 1.5, cz - 3.4), parent=house)
	audio_zone(zone, (6.7, 1.5, 3.4), priority=10.0)

	start_open = index % 2 == 1
	door = scene.entity("Door - Audio Portal (F)", (cx, 1.05, cz), parent=house)
	script(door, "LuxSample.SlidingDoor", [("StartOpen", "Bool", boolean(start_open))])
	door.add("AudioPortalComponent", [
		"Enabled: true", f"ZoneA: {zone.uuid}", f"ZoneB: {scene.outdoor_zone.uuid}", "HalfExtents: [0.6, 1.05, 0.05]",
		f"Open: {1 if start_open else 0}", "BlendDistance: 5", "Material: Wood"])
	panel = scene.entity("Door Panel", scale=(1.2, 2.1, 0.1), parent=door)
	static_mesh(panel, CUBE, specials["Wood"], scene)

	emitter = scene.entity("Emitter", (cx - 2.5, 1.2, cz - 4.5), scale=(0.4, 0.4, 0.4), parent=house)
	static_mesh(emitter, SPHERE, specials["Chrome"], scene)
	audio_source(emitter, EVENT_MUSIC, 0.25, play_on_awake=True, priority=64 + index)
	scene.count("audio sources")
	light = scene.entity("Ceiling Light", (cx, 2.6, cz - 3.4), parent=house)
	point_light(light, PALETTE[index % len(PALETTE)], 3.0, 9.0, shadows=True)
	scene.count("point lights")
	scene.count("houses")


def add_village(scene: Scene, specials: dict[str, int]) -> None:
	village = scene.folder("Village")
	index = 0
	for cz in (-10.0, 15.0, 40.0):
		for cx in (-100.0, -75.0):
			add_house(scene, village, index, cx, cz, specials)
			index += 1
	label = scene.entity("Village Label", (-100.0, 7.0, 48.0), scale=(0.8, 0.8, 0.8), parent=village)
	text(label, "VILLAGE\nzones, portals (F), acoustic geometry")


def add_physics_yard(scene: Scene, bodies: int, specials: dict[str, int], field_materials: list[int]) -> None:
	yard = scene.folder("Physics Yard")
	boxes = int(bodies * 0.6)
	spheres = int(bodies * 0.25)
	capsules = bodies - boxes - spheres

	per_tower = 3 * 3 * 10
	towers = max(1, math.ceil(boxes / per_tower))
	for i in range(boxes):
		tower, slot = divmod(i, per_tower)
		layer, cell = divmod(slot, 9)
		tx = -30.0 + (tower % 6) * 9.0
		tz = 35.0 + (tower // 6) * 9.0
		box = scene.entity(f"Box {i:04d}", (tx + (cell % 3) * 1.02, 0.5 + layer * 1.01, tz + (cell // 3) * 1.02), parent=yard)
		static_mesh(box, CUBE, field_materials[i % len(field_materials)], scene)
		rigid_body(box, 1, mass=1.0)
		box_collider(box)

	for i in range(spheres):
		x = 5.0 + (i % 12) * 1.3
		z = 55.0 + ((i // 12) % 12) * 1.3
		y = 6.0 + (i // 144) * 1.4
		ball = scene.entity(f"Ball {i:04d}", (x, y, z), scale=(0.9, 0.9, 0.9), parent=yard)
		static_mesh(ball, SPHERE, field_materials[(i * 5) % len(field_materials)], scene)
		rigid_body(ball, 1, mass=0.6, continuous=True)
		sphere_collider(ball, 0.5, restitution=0.4)

	for i in range(capsules):
		capsule = scene.entity(f"Capsule {i:04d}", (22.0 + (i % 6) * 1.5, 12.0 + (i // 6) * 1.2, 40.0), (0.0, 0.0, 1.5708), parent=yard)
		static_mesh(capsule, CAPSULE, field_materials[(i * 3) % len(field_materials)], scene)
		rigid_body(capsule, 1, mass=1.2)
		capsule_collider(capsule)

	ramp = scene.entity("Ramp", (30.0, 3.0, 45.0), (-0.35, 0.0, 0.0), (8.0, 0.5, 20.0), parent=yard)
	static_mesh(ramp, CUBE, specials["Metal"], scene)
	rigid_body(ramp, 0)
	box_collider(ramp, friction=0.2)
	audio_surface(ramp, "Metal")

	table = scene.entity("Compound Table", (-5.0, 2.5, 70.0), parent=yard)
	rigid_body(table, 1, mass=8.0)
	parts = [("Table Top", (0.0, 0.9, 0.0), (3.0, 0.2, 2.0))]
	parts += [(f"Table Leg {i}", (sx * 1.3, 0.0, sz * 0.8), (0.2, 1.6, 0.2)) for i, (sx, sz) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1)))]
	compound_children = []
	for tag, position, scale in parts:
		child = scene.entity(tag, position, scale=scale, parent=table)
		static_mesh(child, CUBE, specials["Wood"], scene)
		box_collider(child)
		compound_children.append(child)
	table.add("CompoundColliderComponent", ["IncludeStaticChildColliders: true", "IsImmutable: true",
		"CompoundedColliderEntities:", *[f"  - {child.uuid}" for child in compound_children]])

	platform = scene.entity("Kinematic Platform", (-20.0, 1.0, 75.0), scale=(4.0, 0.3, 4.0), parent=yard)
	static_mesh(platform, CUBE, specials["Chrome"], scene)
	rigid_body(platform, 2)
	box_collider(platform)

	locked = scene.entity("Axis-Locked Box", (-15.0, 4.0, 75.0), parent=yard)
	static_mesh(locked, CUBE, specials["Metal"], scene)
	rigid_body(locked, 1, locked_axes=56)  # rotation locked on all three axes
	box_collider(locked)

	trigger = scene.entity("Trigger Volume", (0.0, 1.5, 45.0), scale=(6.0, 3.0, 6.0), parent=yard)
	rigid_body(trigger, 0, trigger=True)
	box_collider(trigger)

	character = scene.entity("Character", (-25.0, 1.2, 60.0), parent=yard)
	static_mesh(character, CAPSULE, specials["Chrome"], scene)
	character.add("CharacterControllerComponent", ["SlopeLimitDeg: 45", "StepOffset: 0.5", "LayerID: 0", "DisableGravity: false",
		"ControlMovementInAir: false", "ControlRotationInAir: false"])
	capsule_collider(character)

	scene.count("dynamic bodies", boxes + spheres + capsules + 2)
	label = scene.entity("Physics Label", (-30.0, 12.0, 30.0), scale=(0.8, 0.8, 0.8), parent=yard)
	text(label, f"PHYSICS YARD\n{boxes + spheres + capsules} dynamic bodies, compound, trigger, kinematic")


def add_2d_corner(scene: Scene) -> None:
	corner = scene.folder("2D Corner")
	base = (0.0, 0.0, 100.0)

	def sprite(tag, position, scale, color, texture=0):
		entity = scene.entity(tag, (base[0] + position[0], base[1] + position[1], base[2]), scale=scale, parent=corner)
		entity.add("SpriteRendererComponent", [f"Color: {vec(color)}", f"Texture: {texture}", "TilingFactor: 1",
			"UVStart: [0, 0]", "UVEnd: [1, 1]", "ScreenSpace: false"])
		scene.count("sprites")
		return entity

	def body_2d(entity, body_type):
		entity.add("RigidBody2DComponent", [f"BodyType: {body_type}", "FixedRotation: false", "Mass: 1", "LinearDrag: 0.01",
			"AngularDrag: 0.05", "GravityScale: 1", "IsBullet: false"])

	ground = sprite("2D Ground", (0.0, 0.25), (24.0, 0.5, 1.0), (0.3, 0.3, 0.35, 1.0))
	body_2d(ground, 0)
	ground.add("BoxCollider2DComponent", ["Offset: [0, 0]", "Size: [0.5, 0.5]", "Density: 1", "Friction: 0.6"])

	for i in range(48):
		crate = sprite(f"2D Crate {i:02d}", (-9.0 + (i % 12) * 1.5 + (i // 12) * 0.3, 2.0 + (i // 12) * 1.6), (1.0, 1.0, 1.0),
					   (1.0, 1.0, 1.0, 1.0), CHECKERBOARD_TEXTURE if i % 2 == 0 else LOGO_TEXTURE)
		body_2d(crate, 1)
		crate.add("BoxCollider2DComponent", ["Offset: [0, 0]", "Size: [0.5, 0.5]", "Density: 1", "Friction: 0.6"])

	for i in range(16):
		ball = scene.entity(f"2D Ball {i:02d}", (base[0] - 8.0 + i, base[1] + 9.0 + (i % 3), base[2]), scale=(0.8, 0.8, 0.8), parent=corner)
		ball.add("CircleRendererComponent", [f"Color: {vec((*PALETTE[i % len(PALETTE)], 1.0))}", "Thickness: 1", "Fade: 0.005"])
		body_2d(ball, 1)
		ball.add("CircleCollider2DComponent", ["Offset: [0, 0]", "Radius: 0.5", "Density: 1", "Friction: 0.4"])
		scene.count("circles")

	label = scene.entity("2D Label", (base[0] - 6.0, 13.0, base[2]), scale=(0.7, 0.7, 0.7), parent=corner)
	text(label, "2D CORNER\nBox2D sprites + circles")


def main() -> None:
	parser = argparse.ArgumentParser(description="Generate the LuxEngine all-systems benchmark scene.")
	parser.add_argument("--objects", type=int, default=6000, help="static meshes in the material field (default 6000)")
	parser.add_argument("--materials", type=int, default=128, help="distinct generated field materials (default 128)")
	parser.add_argument("--lights", type=int, default=192, help="point lights over the material field (default 192)")
	parser.add_argument("--bodies", type=int, default=800, help="dynamic rigid bodies in the physics yard (default 800)")
	parser.add_argument("--output", type=Path, default=SCENE_PATH, help="scene file to write")
	args = parser.parse_args()
	if min(args.objects, args.lights, args.bodies) < 0 or not 1 <= args.materials <= 900:
		parser.error("counts must be >= 0 and --materials must be in 1..900")

	materials, specials = build_materials(args.materials)
	field_materials = [m.handle for m in materials[: args.materials]]
	write_materials(materials)
	update_registry(materials)

	scene = Scene("Benchmark")
	add_environment(scene, specials)
	add_camera(scene)
	add_plaza(scene, specials, field_materials)
	add_material_field(scene, args.objects, field_materials)
	add_lights(scene, args.lights)
	add_sponza(scene)
	add_village(scene, specials)
	add_physics_yard(scene, args.bodies, specials, field_materials)
	add_2d_corner(scene)
	scene.write(args.output)

	print(f"Wrote {args.output.relative_to(ROOT) if args.output.is_relative_to(ROOT) else args.output}")
	print(f"  {len(scene.entities)} entities, {len(materials)} materials ({args.materials} field + {len(specials)} named)")
	for key in sorted(scene.stats):
		print(f"  {scene.stats[key]:>6} {key}")


if __name__ == "__main__":
	main()
