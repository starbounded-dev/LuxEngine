from __future__ import annotations

import argparse
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_DIR = ROOT / "Editor" / "SandboxProject" / "Assets" / "Scenes" / "Benchmarks"

CUBE_MESH_SOURCE = 6041298930630833044


class SceneWriter:
	def __init__(self, path: Path, name: str, base_uuid: int):
		self.path = path
		self.name = name
		self.base_uuid = base_uuid
		self.next_uuid = base_uuid
		self.file = None

	def __enter__(self) -> "SceneWriter":
		self.path.parent.mkdir(parents=True, exist_ok=True)
		self.file = self.path.open("w", encoding="utf-8", newline="\n")
		self.file.write(f"Scene: {self.name}\n")
		self.file.write("Entities:\n")
		return self

	def __exit__(self, exc_type, exc, tb) -> None:
		if self.file:
			self.file.close()

	def uuid(self) -> int:
		value = self.next_uuid
		self.next_uuid += 1
		return value

	def vec3(self, values: tuple[float, float, float]) -> str:
		return f"[{values[0]:.6g}, {values[1]:.6g}, {values[2]:.6g}]"

	def begin_entity(
		self,
		tag: str,
		position: tuple[float, float, float],
		rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
		scale: tuple[float, float, float] = (1.0, 1.0, 1.0),
	) -> None:
		self.file.write(f"  - Entity: {self.uuid()}\n")
		self.file.write("    TagComponent:\n")
		self.file.write(f"      Tag: {tag}\n")
		self.file.write("    Parent: 0\n")
		self.file.write("    Children:\n")
		self.file.write("      []\n")
		self.file.write("    TransformComponent:\n")
		self.file.write(f"      Position: {self.vec3(position)}\n")
		self.file.write(f"      Rotation: {self.vec3(rotation)}\n")
		self.file.write(f"      Scale: {self.vec3(scale)}\n")

	def static_cube(
		self,
		tag: str,
		position: tuple[float, float, float],
		scale: tuple[float, float, float] = (1.0, 1.0, 1.0),
		rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
	) -> None:
		self.begin_entity(tag, position, rotation, scale)
		self.file.write("    StaticMeshComponent:\n")
		self.file.write(f"      AssetID: {CUBE_MESH_SOURCE}\n")
		self.file.write("      MaterialTable:\n")
		self.file.write("        {}\n")
		self.file.write("      Visible: true\n")

	def camera(
		self,
		position: tuple[float, float, float],
		rotation: tuple[float, float, float],
		far_clip: float = 2000.0,
	) -> None:
		self.begin_entity("Camera", position, rotation)
		self.file.write("    CameraComponent:\n")
		self.file.write("      Camera:\n")
		self.file.write("        ProjectionType: 0\n")
		self.file.write("        PerspectiveFOV: 45\n")
		self.file.write("        PerspectiveNear: 0.1\n")
		self.file.write(f"        PerspectiveFar: {far_clip:.6g}\n")
		self.file.write("        OrthographicSize: 10\n")
		self.file.write("        OrthographicNear: -1\n")
		self.file.write("        OrthographicFar: 1\n")
		self.file.write("      Primary: true\n")
		self.file.write("      FixedAspectRatio: false\n")

	def directional_light(self, intensity: float = 2.0) -> None:
		self.begin_entity("Directional Light", (0.0, 8.0, 0.0), (-0.75, -0.55, 0.0))
		self.file.write("    DirectionalLightComponent:\n")
		self.file.write(f"      Intensity: {intensity:.6g}\n")
		self.file.write("      Radiance: [1, 0.96, 0.9]\n")
		self.file.write("      CastShadows: true\n")
		self.file.write("      SoftShadows: true\n")
		self.file.write("      LightSize: 0.5\n")
		self.file.write("      ShadowAmount: 1\n")

	def sky_light(self, intensity: float = 0.55) -> None:
		self.begin_entity("Sky Light", (0.0, 0.0, 0.0))
		self.file.write("    SkyLightComponent:\n")
		self.file.write("      EnvironmentMap: 0\n")
		self.file.write(f"      Intensity: {intensity:.6g}\n")
		self.file.write("      Lod: 0\n")
		self.file.write("      DynamicSky: true\n")
		self.file.write("      TurbidityAzimuthInclination: [2, 0, 0]\n")

	def point_light(
		self,
		tag: str,
		position: tuple[float, float, float],
		radiance: tuple[float, float, float],
		intensity: float = 2.0,
		radius: float = 14.0,
		cast_shadows: bool = False,
	) -> None:
		self.begin_entity(tag, position)
		self.file.write("    PointLightComponent:\n")
		self.file.write(f"      Radiance: {self.vec3(radiance)}\n")
		self.file.write(f"      Intensity: {intensity:.6g}\n")
		self.file.write(f"      CastShadows: {'true' if cast_shadows else 'false'}\n")
		self.file.write("      SoftShadows: false\n")
		self.file.write("      MinRadius: 1\n")
		self.file.write(f"      Radius: {radius:.6g}\n")
		self.file.write("      LightSize: 0.35\n")
		self.file.write("      Falloff: 1\n")


def color_for(index: int) -> tuple[float, float, float]:
	palette = (
		(1.0, 0.55, 0.32),
		(0.28, 0.78, 1.0),
		(0.5, 1.0, 0.48),
		(1.0, 0.34, 0.7),
		(0.94, 0.88, 0.42),
	)
	return palette[index % len(palette)]


def add_cube_grid(scene: SceneWriter, count: int, spacing: float, scale: float, tag_prefix: str) -> None:
	grid_width = math.ceil(math.sqrt(count))
	half = (grid_width - 1) * spacing * 0.5
	for i in range(count):
		x = (i % grid_width) * spacing - half
		z = (i // grid_width) * spacing - half
		scene.static_cube(f"{tag_prefix}_{i:06d}", (x, scale * 0.5, z), (scale, scale, scale))


def write_cube_scene(output_dir: Path, count: int, base_uuid: int) -> None:
	name = f"Benchmark {count // 1000}k Cubes"
	path = output_dir / f"Benchmark_{count // 1000}k_Cubes.luxscene"
	spacing = 2.15 if count <= 10000 else 1.8
	scale = 0.85 if count <= 10000 else 0.7
	grid_width = math.ceil(math.sqrt(count))
	size = grid_width * spacing
	with SceneWriter(path, name, base_uuid) as scene:
		scene.camera((0.0, size * 0.28, size * 0.62), (-0.45, 0.0, 0.0), far_clip=max(2000.0, size * 3.0))
		scene.directional_light(2.2)
		scene.sky_light(0.65)
		add_cube_grid(scene, count, spacing, scale, "Cube")


def write_city_blocks(output_dir: Path) -> None:
	path = output_dir / "Benchmark_CityBlocks.luxscene"
	with SceneWriter(path, "Benchmark City Blocks", 15100000000000000000) as scene:
		scene.camera((-85.0, 48.0, 95.0), (-0.48, -0.72, 0.0), far_clip=2500.0)
		scene.directional_light(2.5)
		scene.sky_light(0.45)
		scene.static_cube("Ground", (0.0, -0.05, 0.0), (170.0, 0.1, 170.0))

		block_count = 34
		spacing = 4.4
		half = (block_count - 1) * spacing * 0.5
		for z in range(block_count):
			for x in range(block_count):
				if x % 5 == 0 or z % 5 == 0:
					continue
				height = 2.0 + ((x * 17 + z * 31) % 14) * 0.9
				width = 1.2 + ((x * 7 + z * 3) % 4) * 0.28
				depth = 1.2 + ((x * 5 + z * 11) % 4) * 0.28
				scene.static_cube(
					f"Building_{x:02d}_{z:02d}",
					(x * spacing - half, height * 0.5, z * spacing - half),
					(width, height, depth),
				)

		for i in range(20):
			row = i % 5
			col = i // 5
			scene.point_light(
				f"Street Light {i:02d}",
				(-55.0 + col * 35.0, 5.5, -55.0 + row * 28.0),
				color_for(i),
				intensity=1.5,
				radius=20.0,
			)


def write_indoor_occluders(output_dir: Path) -> None:
	path = output_dir / "Benchmark_IndoorOccluders.luxscene"
	with SceneWriter(path, "Benchmark Indoor Occluders", 15200000000000000000) as scene:
		scene.camera((-32.0, 4.2, 0.0), (-0.08, -1.5708, 0.0), far_clip=1200.0)
		scene.directional_light(0.4)
		scene.sky_light(0.2)
		scene.static_cube("Floor", (0.0, -0.05, 0.0), (95.0, 0.1, 42.0))
		scene.static_cube("Ceiling", (0.0, 9.0, 0.0), (95.0, 0.2, 42.0))
		scene.static_cube("Back Wall", (47.0, 4.5, 0.0), (0.4, 9.0, 42.0))
		scene.static_cube("Left Wall", (0.0, 4.5, -21.0), (95.0, 9.0, 0.4))
		scene.static_cube("Right Wall", (0.0, 4.5, 21.0), (95.0, 9.0, 0.4))

		for i in range(18):
			x = -38.0 + i * 4.8
			gap = -11.0 if i % 2 == 0 else 11.0
			scene.static_cube(f"Occluder Wall A {i:02d}", (x, 4.0, gap - 9.5), (0.35, 8.0, 19.0))
			scene.static_cube(f"Occluder Wall B {i:02d}", (x, 4.0, gap + 9.5), (0.35, 8.0, 19.0))

		for i in range(1400):
			x = -36.0 + (i % 70) * 1.15
			z = -16.0 + ((i // 70) % 25) * 1.3
			y = 0.55 + ((i // (70 * 25)) % 4) * 1.1
			scene.static_cube(f"Indoor Prop {i:04d}", (x, y, z), (0.55, 0.55, 0.55))

		for i in range(32):
			scene.point_light(
				f"Interior Light {i:02d}",
				(-38.0 + (i % 16) * 5.4, 7.6, -10.0 if i < 16 else 10.0),
				color_for(i),
				intensity=1.6,
				radius=16.0,
			)


def write_many_lights(output_dir: Path) -> None:
	path = output_dir / "Benchmark_ManyLights.luxscene"
	with SceneWriter(path, "Benchmark Many Lights", 15300000000000000000) as scene:
		scene.camera((0.0, 42.0, 72.0), (-0.52, 0.0, 0.0), far_clip=1600.0)
		scene.directional_light(1.0)
		scene.sky_light(0.35)
		add_cube_grid(scene, 2500, 2.2, 0.8, "LightTestCube")

		light_count = 256
		grid_width = 16
		half = (grid_width - 1) * 5.0 * 0.5
		for i in range(light_count):
			x = (i % grid_width) * 5.0 - half
			z = (i // grid_width) * 5.0 - half
			y = 3.0 + (i % 4) * 1.0
			scene.point_light(
				f"Point Light {i:03d}",
				(x, y, z),
				color_for(i),
				intensity=2.4,
				radius=13.0,
				cast_shadows=(i % 32 == 0),
			)


def main() -> None:
	parser = argparse.ArgumentParser(description="Generate Lux renderer benchmark scenes.")
	parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_DIR, help="Output directory for .luxscene files.")
	args = parser.parse_args()

	output_dir = args.output
	write_cube_scene(output_dir, 10_000, 15000000000000000000)
	write_cube_scene(output_dir, 100_000, 15010000000000000000)
	write_city_blocks(output_dir)
	write_indoor_occluders(output_dir)
	write_many_lights(output_dir)
	print(f"Generated benchmark scenes in {output_dir}")


if __name__ == "__main__":
	main()
