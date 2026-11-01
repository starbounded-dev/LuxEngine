#!/usr/bin/env python3
"""Run isolated Sponza Play/oracle cases with the built native editor (requires PyYAML)."""

import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import yaml

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ID = 73049999999999901
LISTENER_ID = 73049999999999902
SPONZA_ID = 73049999999999910
CASES = {
    "corner": {"listener": [-8.8, 1.6, -1.5], "source": [-7.5, 1.6, -2.8], "audible": True},
    "doorway-open": {"listener": [-5, 1.6, -3.2], "source": [-4, 1.6, -1], "bay": True, "audible": True},
    "doorway-closed": {"listener": [-5, 1.6, -3.2], "source": [-4, 1.6, -1], "bay": True, "closed": True},
    "direct-blocked": {"listener": [-8.8, 1.6, -1.5], "source": [-7.5, 1.6, -2.8], "direct": True},
    "direct-clear": {"listener": [0, 1.6, 0], "source": [2, 1.6, 0], "direct": True, "clear": True},
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def directory_link(source, destination):
    if os.name == "nt":
        # Literal environment arguments avoid turning filesystem paths into shell code.
        env = dict(os.environ, LUX_TEST_LINK_SOURCE=str(source), LUX_TEST_LINK_DESTINATION=str(destination))
        subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                        "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path $env:LUX_TEST_LINK_DESTINATION -Target $env:LUX_TEST_LINK_SOURCE | Out-Null"],
                       env=env, check=True)
    else:
        destination.symlink_to(source, target_is_directory=True)


def entity(identity, name, position, scale=None):
    return {"Entity": identity, "TagComponent": {"Tag": name}, "Parent": 0, "Children": [],
            "TransformComponent": {"Position": position, "Rotation": [0, 0, 0], "Scale": scale or [1, 1, 1]}}


def box(identity, name, low, high, cube):
    result = entity(identity, name, [(a+b)*0.5 for a, b in zip(low, high)], [b-a for a, b in zip(low, high)])
    result["StaticMeshComponent"] = {"AssetID": cube, "MaterialTable": {}, "Visible": True}
    result["MeshColliderComponent"] = {"ColliderAsset": cube, "SubmeshIndex": 0, "UseSharedShape": False,
        "Density": 1, "Friction": 0.5, "Restitution": 0, "AcousticMaterial": "Default", "AcousticFromMaterial": False,
        "AcousticMotion": 0, "CollisionComplexity": 2}
    result["AudioSurfaceComponent"] = {"Material": "Default", "PhysicsSounds": False}
    return result


def make_fixture(directory, case_name, case, base_project, demo, source_assets, handles, target_config="debug"):
    assets = directory/"Assets"
    assets.mkdir(parents=True)
    for original in source_assets.iterdir():
        if original.name in ("Scenes", "Cache", "Scripts", "Meshes"):
            continue
        destination = assets/original.name
        if original.is_dir():
            directory_link(original, destination)
        else:
            shutil.copy2(original, destination)
    # Only the copied scene belongs to this fixture. Export validates every registered
    # scene/prefab, so remove unrelated sample entries rather than weakening validation.
    registry_path = assets/"AssetRegistry.lzr"
    registry = yaml.safe_load(registry_path.read_text(encoding="utf-8"))
    registry["Assets"] = [row for row in registry["Assets"] if row["Type"] not in ("Scene", "Prefab")
                          or row["FilePath"].replace("\\", "/") == "Scenes/FMODDemo.luxscene"]
    registry_path.write_text(yaml.safe_dump(registry, sort_keys=False), encoding="utf-8")
    (assets/"Scenes").mkdir()
    # Assimp rewrites generated .lmat files alongside an imported model. Copy these
    # model folders rather than linking them, including their referenced textures.
    for model in ("Sponza", "Default"):
        shutil.copytree(source_assets/"Meshes/Source"/model, assets/"Meshes/Source"/model)
    (assets/"Scripts/Binaries").mkdir(parents=True)
    for original in (source_assets/"Scripts/Binaries").iterdir():
        if original.is_file():
            shutil.copy2(original, assets/"Scripts/Binaries"/original.name)

    scene = copy.deepcopy(demo)
    scene["Scene"] = "Soundwave Sponza " + case_name
    model = entity(SPONZA_ID, "Sponza acoustic geometry", [0, 0, 0])
    model["StaticMeshComponent"] = {"AssetID": handles["Meshes/Source/Sponza/Sponza.gltf"], "MaterialTable": {}, "Visible": True}
    model["MeshColliderComponent"] = {"ColliderAsset": handles["Meshes/Source/Sponza/Sponza.gltf"],
        "SubmeshIndex": 4294967295, "UseSharedShape": False, "Density": 1, "Friction": 0.5, "Restitution": 0,
        "AcousticMaterial": "Default", "AcousticFromMaterial": False, "AcousticMotion": 0, "CollisionComplexity": 2}
    model["AudioSurfaceComponent"] = {"Material": "Default", "PhysicsSounds": False}
    listener = entity(LISTENER_ID, "Pinned acoustic listener", case["listener"])
    listener["TransformComponent"]["Rotation"] = [0, math.pi, 0]
    listener["CameraComponent"] = copy.deepcopy(next(e["CameraComponent"] for e in demo["Entities"] if "CameraComponent" in e))
    listener["AudioListenerComponent"] = {"Active": True, "ListenerIndex": 0, "Weight": 1,
        "UseAttenuationTarget": False, "AttenuationTarget": 0}
    emitter = entity(SOURCE_ID, "Pinned acoustic source", case["source"])
    emitter["AudioSourceComponent"] = copy.deepcopy(next(e["AudioSourceComponent"] for e in demo["Entities"]
        if e.get("TagComponent", {}).get("Tag") == "Music Emitter - MusicAmbiance"))
    emitter["AudioSourceComponent"].update({"VolumeMultiplier": 0.02, "DistanceCulling": False, "Enabled": True})
    light = copy.deepcopy(next(e for e in demo["Entities"] if "DirectionalLightComponent" in e))
    light["Parent"], light["Children"] = 0, []
    entities = [model, listener, emitter, light]
    if case.get("bay"):
        # Sponza's neighboring open arches are alternate physical routes. Authored
        # partitions seal this bay's sides/back/floor/ceiling, leaving its real arch.
        bounds = [([-5.6, -0.5, -5.4], [-5.5, 4.1, -1.9]),
                  ([-1.5, -0.5, -5.4], [-1.4, 4.1, -1.9]),
                  ([-5.6, -0.5, -5.4], [-1.4, 4.1, -5.3]),
                  ([-5.6, -0.5, -5.4], [-1.4, -0.4, -1.9]),
                  ([-5.6, 4.0, -5.4], [-1.4, 4.1, -1.9])]
        for index, (low, high) in enumerate(bounds):
            entities.append(box(SPONZA_ID+index+1, f"Bay partition {index}", low, high, handles["Meshes/Source/Default/Cube.gltf"]))
        door = entity(SPONZA_ID+6, "Bay portal shutter", [-3.5, 1.8, -1.95])
        door["AudioPortalComponent"] = {"Enabled": True, "ZoneA": 0, "ZoneB": 0, "HalfExtents": [2.1, 2.3, 0.05],
            "Open": 0 if case.get("closed") else 1, "BlendDistance": 1, "Material": "Default"}
        entities.append(door)
    scene["Entities"] = entities
    (assets/"Scenes/FMODDemo.luxscene").write_text(yaml.safe_dump(scene, sort_keys=False), encoding="utf-8")
    project = copy.deepcopy(base_project)
    config = project["Project"]
    config.update({"Name": "Soundwave Sponza " + case_name, "AssetDirectory": "Assets", "AssetRegistry": "Assets/AssetRegistry.lzr",
                   "AutoSave": False, "StartScene": "Scenes/FMODDemo.luxscene"})
    config["RuntimeExport"].update({"GameName": "Soundwave-" + case_name, "TargetConfig": target_config.capitalize(),
                                    "WindowWidth": 1280, "WindowHeight": 720, "Fullscreen": False, "VSync": False})
    # No script components are present in these fixtures; do not request an unrelated
    # sample script build when exercising the normal export preflight.
    config["ScriptModulePath"] = ""
    audio = config["Audio"]
    audio["RebuildBanksOnPlay"] = False
    audio["EnableLiveUpdate"] = False
    audio["AcousticMaterials"] = {"Default": {"AbsorptionLF": 0.2, "AbsorptionHF": 0.7, "Scattering": 0.1,
        "TransmissionLF": 0.001, "TransmissionHF": 0.001, "FlatTransmissionLF": 1, "FlatTransmissionHF": 1}}
    audio["Occlusion"] = {"Source": "Engine", "UpdateRateHz": 1, "CastBudget": 32, "Strength": 1,
        "Kernel": {"Version": 1, "ReflectionDepth": 0, "OpeningGridSize": 1 if case.get("direct") else 8,
                   "MaxBounces": 1 if case.get("direct") else 2, "CandidateBudget": 65536,
                   "OpeningNodeBudget": 2048, "PathBudget": 64, "Absorption": 1, "CompareRaycast": True}}
    path = directory/"Acoustics.luxproj"
    path.write_text(yaml.safe_dump(project, sort_keys=False), encoding="utf-8")
    return path


def verify(data, case, triangle_counts):
    require(data["EngineRayComparison"], "Engine-ray comparison did not run")
    expected = triangle_counts["Sponza"] + (5*triangle_counts["Default"] if case.get("bay") else 0)
    require(data["StaticTriangles"] == expected, f"Sponza/partition geometry missing: {data['StaticTriangles']} != {expected}")
    require(data["MovingTriangles"] == (12 if case.get("closed") else 0), "Portal shutter capture disagrees with the authored state")
    require(data["StaticMeshInstances"] == (6 if case.get("bay") else 1), "A rendered model/partition instance was omitted")
    require(data["MovingMeshInstances"] == (1 if case.get("closed") else 0), "Portal instance state disagrees with its authoring")
    require(data["CheckedFMODParameters"] == 1 and len(data["Sources"]) == 1, "Probe did not verify its actual FMOD parameter")
    source = data["Sources"][0]
    require(source["UUID"] == SOURCE_ID, "Unexpected playback source")
    for name in ("listener", "source"):
        actual = source[name.title()]
        require(len(actual) == 3 and all(abs(a-b) < 0.00001 for a, b in zip(actual, case[name])), f"{name} moved during the pinned probe")
    require(source["DirectGainLFBits"] == source["EngineRayGainLFBits"], "Direct gain differs from the actual engine-ray oracle")
    if case.get("clear"):
        require(source["RawGainLF"] == 1 and source["EngineRayGainLF"] == 1, "Clear control is obstructed")
    else:
        require(source["EngineRayWalls"] > 0 and source["DirectGainLF"] <= 0.001001, "Control ray did not encounter the opaque geometry")
        if case.get("audible"):
            require(source["OpeningPaths"] > 0 and "AudibleOpening" in source and source["RawGainLF"] > 0.01,
                    "No audible route around Sponza's obstruction")
            require(source["OcclusionGainLF"] > source["EngineRayGainLF"] * 5, "Audible route did not reach playback")
        else:
            require(source["RawGainLF"] == source["DirectGainLF"] and source["OpeningPaths"] == 0,
                    "Closed/direct-only control acquired an unintended opening route")
    return {key: source[key] for key in ("DirectGainLF", "RawGainLF", "OcclusionGainLF", "EngineRayGainLF", "OpeningPaths", "WorkLimited")}


def verify_export(capture, destination, case, triangle_counts, timeout, threading):
    metadata = yaml.safe_load((capture/"FMODDemo.export.yaml").read_text(encoding="utf-8"))
    package, executable = Path(metadata["Root"]).resolve(), Path(metadata["Executable"]).resolve()
    require(package.is_relative_to(destination.resolve()) and executable.parent == package,
            "Export metadata points outside the isolated destination")
    for path in (executable, package/"Assets/Project.luxruntime", package/"Assets/AssetPack.lap", package/"Assets/ShaderPack.lsp"):
        require(path.is_file() and path.stat().st_size > 0, f"Missing exported artifact: {path}")
    report_path = capture/"FMODDemo.runtime-audio.yaml"
    env = {key: value for key, value in os.environ.items() if not key.startswith("LUX_GOLDEN_")}
    env.update({"LUX_RUNTIME_AUDIO_REPORT": str(report_path.resolve()), "LUX_NO_WATCHDOG": "1",
                "LUX_THREADING_POLICY": threading.capitalize()})
    if os.name != "nt":
        env["LD_LIBRARY_PATH"] = str(package/"lib") + ":" + env.get("LD_LIBRARY_PATH", "")
    started = time.time_ns()
    log_path = capture/"FMODDemo.runtime.log"
    with log_path.open("w", encoding="utf-8") as log:
        result = subprocess.run([str(executable), "--project=" + str(package)], cwd=package, env=env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=timeout, check=False)
    require(result.returncode == 0, f"Exported runtime failed with {result.returncode}; see {log_path}")
    # One freshness check for this process execution and its two native outputs.
    for path in (report_path, Path(str(report_path) + ".status")):
        artifact = path.stat()
        require(artifact.st_size > 0 and artifact.st_mtime_ns >= started - 2_000_000_000,
                f"Empty or stale runtime verification artifact: {path}")
    require(Path(str(report_path) + ".status").read_text(encoding="utf-8").splitlines()[0] == "OK",
            "Runtime acoustic verification failed")
    log = log_path.read_text(encoding="utf-8", errors="replace")
    require("Vulkan validation error" not in log and "Vulkan validation warning" not in log and
            "Vulkan performance validation" not in log, "Exported runtime reported Vulkan validation messages")
    data = yaml.safe_load(report_path.read_text(encoding="utf-8"))
    verified = verify(data, case, triangle_counts)
    play = yaml.safe_load((capture/"FMODDemo.audio.yaml").read_text(encoding="utf-8"))
    # Pinned scenes settle to the same reference result; compare raw solver output
    # rather than frame-rate-dependent smoothing during startup.
    for key in ("DirectGainLFBits", "EngineRayGainLFBits", "OpeningPaths"):
        require(data["Sources"][0][key] == play["Sources"][0][key], f"Play/runtime mismatch: {key}")
    require(abs(data["Sources"][0]["RawGainLF"] - play["Sources"][0]["RawGainLF"]) < 0.00001,
            "Play/runtime raw acoustic gains differ")
    return {"package": str(package), "report": str(report_path), "values": verified}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=("debug", "release"), default="debug")
    parser.add_argument("--export-runtime", action="store_true", help="export each isolated project normally, run the packaged game and verify its real acoustic/FMOD values")
    parser.add_argument("--runtime-config", choices=("debug", "release", "dist"), help="exported runtime configuration (defaults to --config)")
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    parser.add_argument("--threading", choices=("single", "multi"), default="multi")
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()
    sample = ROOT/"Editor/LuxSampleProject"
    project_path, demo_path, registry_path = sample/"LuxSample.luxproj", sample/"Assets/Scenes/FMODDemo.luxscene", sample/"Assets/AssetRegistry.lzr"
    inputs = [project_path, demo_path, registry_path]
    for model in ("Sponza", "Default"):
        inputs.extend(path for path in (sample/"Assets/Meshes/Source"/model).rglob("*") if path.is_file())
    original = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    project = yaml.safe_load(project_path.read_text(encoding="utf-8"))
    demo = yaml.safe_load(demo_path.read_text(encoding="utf-8"))
    registry = yaml.safe_load(registry_path.read_text(encoding="utf-8"))
    handles = {row["FilePath"].replace("\\", "/"): row["Handle"] for row in registry["Assets"]}
    triangle_counts = {}
    for model, filename in (("Sponza", "Sponza"), ("Default", "Cube")):
        gltf = json.loads((sample/f"Assets/Meshes/Source/{model}/{filename}.gltf").read_text(encoding="utf-8"))
        triangle_counts[model] = sum(gltf["accessors"][primitive["indices"]]["count"]//3
                                     for mesh in gltf["meshes"] for primitive in mesh["primitives"])
    run_id = "sponza-acoustics-" + str(time.time_ns())
    run = ROOT/"bin/soundwave-scene-tests"/run_id
    run.mkdir(parents=True)
    results = {}
    try:
        for name in args.cases:
            case = CASES[name]
            fixture = make_fixture(run/name, name, case, project, demo, sample/"Assets", handles, args.runtime_config or args.config)
            label = run_id + "-" + name
            destination = run/name/"export"
            command = [sys.executable, str(ROOT/"tests/rendering/golden_run.py"), "--config", args.config, "--project", str(fixture),
                       "--label", label, "--scenes", "FMODDemo", "--play", "--threading", args.threading,
                       "--frame", "2", "--perf-frames", "1", "--perf-warmup", "0", "--timeout", str(args.timeout)]
            if args.export_runtime:
                command += ["--export-dir", str(destination)]
            subprocess.run(command, cwd=ROOT, check=True)
            capture = ROOT/"bin/golden"/label
            artifact = capture/"FMODDemo.audio.yaml"
            # golden_run already checked this run's final artifact timestamp once.
            summary = json.loads((capture/"summary.json").read_text(encoding="utf-8"))
            require(summary["runs"][0]["validationMessages"] == 0, "Native run reported Vulkan validation messages")
            results[name] = verify(yaml.safe_load(artifact.read_text(encoding="utf-8")), case, triangle_counts)
            results[name]["capture"] = str(capture)
            if args.export_runtime:
                results[name]["runtime"] = verify_export(capture, destination, case, triangle_counts, args.timeout, args.threading)
            print(name + ": PASS " + json.dumps(results[name]), flush=True)
            (run/"results.json").write_text(json.dumps(results, indent=2)+"\n", encoding="utf-8")
    finally:
        for path, digest in original.items():
            require(hashlib.sha256(path.read_bytes()).hexdigest() == digest, f"Original project input changed during the isolated run: {path}")
    print("PASS: isolated native Sponza Play/oracle cases; original project, scene, registry and model bytes preserved. Evidence: " + str(run), flush=True)


if __name__ == "__main__":
    main()
