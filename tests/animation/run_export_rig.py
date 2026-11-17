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
ASSETS = ROOT / "Editor/LuxSampleProject/Assets"

MESH, SKELETON, SCENE = 7400000000000000001, 7400000000000000002, 7300000000000000001
MATERIAL_A, MATERIAL_B = 2110506557084641280, 8419695537258757221
CLIP = 7400000000000000004
GRAPH, IDLE = 7400000000000000012, 7400000000000000003

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

RIGS = {
    # folder, mesh file, skeleton file, clip file, [(handle, material file)]
    "simple": ("Rig", "RiggedSimple.gltf", "RiggedSimple.lskel", None,
               [(MATERIAL_A, "RiggedSimple_0_Material_001-effect.lmat"), (MATERIAL_B, "RiggedSimple_1_Material_1.lmat")]),
    "luxbiped": ("LuxBiped", "LuxBiped.fbx", "LuxBiped.lskel", "LuxBiped_Walk.lanim",
                 [(1482404925294778034, "LuxBiped_1_Material_1.lmat"), (10881254862323545889, "LuxBiped_0_LuxBipedMaterial.lmat")]),
}


def registry_text(rig, scene_handle, graph=False):
    folder, mesh, skeleton, clip, materials = RIGS[rig]
    entries = [(scene_handle, "Scenes/RigExport.luxscene", "Scene"),
               (MESH, f"Meshes/Source/{folder}/{mesh}", "MeshSource"),
               (SKELETON, f"Meshes/Source/{folder}/{skeleton}", "Skeleton")]
    if clip:
        entries.append((CLIP, f"Meshes/Source/{folder}/{clip}", "Animation"))
    if graph:
        # the graph names both clips; the pack collector has to find them through it
        entries.append((IDLE, f"Meshes/Source/{folder}/LuxBiped_Idle.lanim", "Animation"))
        entries.append((GRAPH, f"Meshes/Source/{folder}/LuxBiped.lanimgraph", "AnimationGraph"))
    entries += [(h, f"Meshes/Source/{folder}/Materials/{f}", "Material") for h, f in materials]
    return "Assets:\n" + "".join(f"  - Handle: {h}\n    FilePath: {p}\n    Type: {t}\n" for h, p, t in entries)


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
{skeleton_extra}  - Entity: 7200000000000000005
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


def write_project(project, config, rig, graph=False):
    assets = project / "Assets"
    (assets / "Scenes").mkdir(parents=True)
    shutil.copytree(ASSETS / "Meshes/Source" / RIGS[rig][0], assets / "Meshes/Source" / RIGS[rig][0])
    extra = f"      Clip: {CLIP}\n" if RIGS[rig][3] else "      DebugBone: Bone.001\n      DebugBendDegrees: 55\n"
    if graph:
        extra = f"      Graph: {GRAPH}\n"
        # Speed defaults to 1: the graph starts in Idle and moves to Walk on the first update
        text = (ASSETS / "Meshes/Source/LuxBiped/LuxBiped.lanimgraph").read_text(encoding="utf-8")
        (assets / "Meshes/Source/LuxBiped/LuxBiped.lanimgraph").write_text(text.replace("      Type: Float\n", "      Type: Float\n      Default: 1\n", 1), encoding="utf-8", newline="\n")
    (project / "RigExport.luxproj").write_text(PROJECT.format(scene=SCENE, config=config.capitalize()), encoding="utf-8", newline="\n")
    (assets / "AssetRegistry.lzr").write_text(registry_text(rig, SCENE, graph), encoding="utf-8", newline="\n")
    (assets / "Scenes/RigExport.luxscene").write_text(SCENE_YAML.format(mesh=MESH, skeleton=SKELETON, skeleton_extra=extra), encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="release", choices=["debug", "release"], help="editor configuration")
    parser.add_argument("--runtime-config", choices=["debug", "release", "dist"], help="the exported player's configuration (default: --config); dist has no shader compiler, so it needs Skinning.glsl in the shader pack")
    parser.add_argument("--rig", default="luxbiped", choices=sorted(RIGS), help="luxbiped: binary FBX with a playing clip; simple: glTF with the debug bend")
    parser.add_argument("--graph", action="store_true", help="luxbiped only: play through an animation graph instead of one clip")
    parser.add_argument("--seconds", type=float, default=15.0, help="how long the player runs before it is stopped")
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="lux-rig-export-") as temporary:
        root = Path(temporary)
        project = root / "project"
        write_project(project, args.runtime_config or args.config, args.rig, args.graph)
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
        if args.graph and "graph state 'Walk'" not in text:
            problems.append("the animation graph never moved to its Walk state (no \"graph state 'Walk'\" line): the graph or its clips did not load from the asset pack")
        if RIGS[args.rig][3] and "playing clip '" not in text:
            problems.append("the player never played the clip (no \"playing clip '...'\" line): the animation did not load from the asset pack")
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
        clip_line = next((line for line in text.splitlines() if "playing clip '" in line), "")
        print("PASS:", skin_line.strip(), clip_line.strip())
        return 0


if __name__ == "__main__":
    sys.exit(main())
