#!/usr/bin/env python3
"""Exports a one-scene rig project through the editor's normal pipeline and runs the packaged player.

It proves the half of R12 that an editor run cannot: the skinned mesh survives the asset pack (the mesh
entry's skin section, the skeleton asset the scene's SkeletonComponent names) and the shipped player
builds its skinning pass for it. The player has no frame capture, so the checks are on its log: the
"Skinning '...'" line (the first skinned mesh reached the renderer), no error lines, and the player
still running when it is stopped.

    python tests/animation/run_export_rig.py [--config release] [--runtime-config dist] [--seconds 15]

The project is written to a temporary folder (assets copied from the sample project's Rig folder), the
editor runs with LUX_USER_DATA_DIR pointing at another one, so the user's preferences are not touched.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
RIG = ROOT / "Editor/LuxSampleProject/Assets/Meshes/Source/Rig"

MESH, SKELETON, SCENE = 7100000000000000001, 7100000000000000002, 7300000000000000001
MATERIAL_A, MATERIAL_B = 2110506557084641280, 8419695537258757221

PROJECT = """Project:
  Name: RigExport
  AssetDirectory: Assets
  AssetRegistry: Assets/AssetRegistry.lzr
  StartScene: {scene}
  AutoSave: false
  RuntimeExport:
    GameName: RigExport
    WindowWidth: 1280
    WindowHeight: 720
    Fullscreen: false
    VSync: true
    TargetConfig: {config}
"""

REGISTRY = """Assets:
  - Handle: {scene}
    FilePath: Scenes/RigExport.luxscene
    Type: Scene
  - Handle: {mesh}
    FilePath: Meshes/Source/Rig/RiggedSimple.gltf
    Type: MeshSource
  - Handle: {skeleton}
    FilePath: Meshes/Source/Rig/RiggedSimple.lskel
    Type: Skeleton
  - Handle: {material_a}
    FilePath: Meshes/Source/Rig/Materials/RiggedSimple_0_Material_001-effect.lmat
    Type: Material
  - Handle: {material_b}
    FilePath: Meshes/Source/Rig/Materials/RiggedSimple_1_Material_1.lmat
    Type: Material
"""

SCENE_YAML = """Scene: RigExport
Entities:
  - Entity: 7200000000000000001
    TagComponent:
      Tag: Sun
    Parent: 0
    Children:
      []
    TransformComponent:
      Position: [0, 0, 0]
      Rotation: [-0.8, 0.5, 0]
      Scale: [1, 1, 1]
    DirectionalLightComponent:
      Intensity: 20
      Radiance: [1, 0.95, 0.85]
      Unit: 0
      ColorTemperature: 6500
      UseColorTemperature: false
      CastShadows: true
      SoftShadows: true
      LightSize: 0.5
      ShadowAmount: 1
      ShadowDistance: 0
      ShadowResolutionTier: 2
  - Entity: 7200000000000000002
    TagComponent:
      Tag: Sky
    Parent: 0
    Children:
      []
    TransformComponent:
      Position: [0, 0, 0]
      Rotation: [0, 0, 0]
      Scale: [1, 1, 1]
    SkyLightComponent:
      EnvironmentMap: 0
      Intensity: 0.8
      Lod: 0
      DynamicSky: true
      TurbidityAzimuthInclination: [2, 0, 0.4]
  - Entity: 7200000000000000004
    TagComponent:
      Tag: Rig
    Parent: 0
    Children:
      []
    TransformComponent:
      Position: [0, 4.6, -14]
      Rotation: [0, 0, 0]
      Scale: [1, 1, 1]
    StaticMeshComponent:
      AssetID: {mesh}
      Visible: true
    SkeletonComponent:
      AssetID: {skeleton}
      DebugBone: Bone.001
      DebugBendDegrees: 55
  - Entity: 7200000000000000005
    TagComponent:
      Tag: Camera
    Parent: 0
    Children:
      []
    TransformComponent:
      Position: [0, 4, 6]
      Rotation: [-0.04, 0, 0]
      Scale: [1, 1, 1]
    CameraComponent:
      Camera:
        ProjectionType: 0
        PerspectiveFOV: 50
        PerspectiveNear: 0.1
        PerspectiveFar: 600
        OrthographicSize: 10
        OrthographicNear: -1
        OrthographicFar: 1
      Primary: true
      FixedAspectRatio: false
"""


def write_project(project, config):
    assets = project / "Assets"
    (assets / "Scenes").mkdir(parents=True)
    shutil.copytree(RIG, assets / "Meshes/Source/Rig")
    ids = dict(scene=SCENE, mesh=MESH, skeleton=SKELETON, material_a=MATERIAL_A, material_b=MATERIAL_B)
    (project / "RigExport.luxproj").write_text(PROJECT.format(scene=SCENE, config=config.capitalize()), encoding="utf-8", newline="\n")
    (assets / "AssetRegistry.lzr").write_text(REGISTRY.format(**ids), encoding="utf-8", newline="\n")
    (assets / "Scenes/RigExport.luxscene").write_text(SCENE_YAML.format(mesh=MESH, skeleton=SKELETON), encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="release", choices=["debug", "release"], help="editor configuration")
    parser.add_argument("--runtime-config", choices=["debug", "release", "dist"], help="the exported player's configuration (default: --config); dist has no shader compiler, so it needs Skinning.glsl in the shader pack")
    parser.add_argument("--seconds", type=float, default=15.0, help="how long the player runs before it is stopped")
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="lux-rig-export-") as temporary:
        root = Path(temporary)
        project = root / "project"
        write_project(project, args.runtime_config or args.config)
        user = root / "user"
        user.mkdir()
        (user / "UserPreferences.yaml").write_text("UserPrefs:\n  ShowSplashScreen: false\n  RecentProjects: []\n", encoding="utf-8")

        label = f"rig-export-{time.time_ns()}"
        export_dir = root / "export"
        command = [sys.executable, str(ROOT / "tests/rendering/golden_run.py"), "--config", args.config,
                   "--project", str(project / "RigExport.luxproj"), "--label", label, "--scenes", "RigExport",
                   "--frame", "2", "--perf-frames", "1", "--perf-warmup", "0", "--timeout", str(args.timeout),
                   "--export-dir", str(export_dir)]
        subprocess.run(command, cwd=ROOT, env=dict(os.environ, LUX_USER_DATA_DIR=str(user)), check=True)
        metadata = (ROOT / "bin/golden" / label / "RigExport.export.yaml").read_text(encoding="utf-8")
        fields = dict(line.split(": ", 1) for line in metadata.splitlines() if ": " in line)
        package, executable = Path(fields["Root"].strip().strip("'\"")), Path(fields["Executable"].strip().strip("'\""))
        print("exported:", package)

        env = {k: v for k, v in os.environ.items() if not k.startswith("LUX_GOLDEN_")}
        env.update(LUX_NO_WATCHDOG="1")
        started = time.time()
        stdout_log = root / "player.stdout.log"
        with stdout_log.open("w", encoding="utf-8") as stream:
            player = subprocess.Popen([str(executable), "--project=" + str(package)], cwd=package, env=env, stdout=stream, stderr=subprocess.STDOUT)
            time.sleep(args.seconds)
            still_running = player.poll() is None
            if still_running:
                player.terminate()
            try:
                player.wait(timeout=20)
            except subprocess.TimeoutExpired:
                player.kill()

        text = stdout_log.read_text(encoding="utf-8", errors="replace")
        engine_logs = sorted((package / "logs").glob("LUX_*.log"), key=lambda p: p.stat().st_mtime)
        if engine_logs and engine_logs[-1].stat().st_mtime >= started - 2:
            text += engine_logs[-1].read_text(encoding="utf-8", errors="replace")

        problems = []
        if not still_running:
            problems.append("the player exited by itself before it was stopped")
        if "Skinning '" not in text:
            problems.append("the player never built a skinning pass (no \"Skinning '...'\" line): the skinned mesh or its skeleton did not reach the renderer")
        for line in text.splitlines():
            if "[Scripting]" in line:
                continue # the project has no scripts; the player's scripting host says so
            lowered = line.lower()
            if any(marker in lowered for marker in ("error", "is not loaded", "invalid packed", "not a valid")):
                problems.append(line.strip())
        if problems:
            print("FAIL")
            for problem in dict.fromkeys(problems):
                print("  -", problem)
            print("--- player log tail ---")
            print("\n".join(text.splitlines()[-25:]))
            return 1
        skin_line = next(line for line in text.splitlines() if "Skinning '" in line)
        print("PASS:", skin_line.strip())
        return 0


if __name__ == "__main__":
    sys.exit(main())
