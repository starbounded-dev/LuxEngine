#!/usr/bin/env python3
"""Run isolated Sponza Play/oracle cases with the built native editor (requires PyYAML)."""

import argparse
import base64
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

import yaml

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ID = 73049999999999901
LISTENER_ID = 73049999999999902
SPONZA_ID = 73049999999999910
ZONE_ID = 73049999999999930
CASES = {
    "gpu-corner": {"listener": [1, 0, -0.5], "source": [-1, 0, -0.5], "simple": True, "audible": True},
    "gpu-reflection": {"listener": [-1, 1, 0], "source": [1, 1, 0], "simple": True, "reflective": True, "clear": True},
    "gpu-caustic": {"listener": [-0.5, 1, 0], "source": [0.5, 1, 0], "simple": True, "reflective": True,
                    "caustic": True, "clear": True},
    "corner": {"listener": [-8.8, 1.6, -1.5], "source": [-7.5, 1.6, -2.8], "audible": True},
    "doorway-open": {"listener": [-5, 1.6, -3.2], "source": [-4, 1.6, -1], "bay": True, "audible": True},
    "doorway-closed": {"listener": [-5, 1.6, -3.2], "source": [-4, 1.6, -1], "bay": True, "closed": True},
    "direct-blocked": {"listener": [-8.8, 1.6, -1.5], "source": [-7.5, 1.6, -2.8], "direct": True},
    "direct-clear": {"listener": [0, 1.6, 0], "source": [2, 1.6, 0], "direct": True, "clear": True},
    "zone-stone": {"listener": [0, 1.6, 0], "source": [0, 1.6, 1], "zone": True, "clear": True},
    "zone-corridor": {"listener": [0, 1.6, 0], "source": [0, 1.6, 1], "zone": True, "clear": True},
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


def make_curved_reflector(path):
    # One glTF primitive and one acoustic instance. The two tilted panels share
    # positions with the central pane so Soundwave can infer signed curvature.
    positions, normals, indices = [], [], []
    panels = ((-2, -1, 0.3, 0), (-1, 1, 0, 0), (1, 2, 0, 0.3))
    for left, right, y_left, y_right in panels:
        index = len(positions)
        positions += [(left, y_left, -2), (right, y_right, 2),
                      (right, y_right, -2), (left, y_left, 2)]
        slope = (y_right - y_left) / (right - left)
        inverse = 1.0 / math.sqrt(1.0 + slope * slope)
        normals += [(-slope * inverse, inverse, 0)] * 4
        indices += [index, index + 1, index + 2, index, index + 3, index + 1]
    position_bytes = b"".join(struct.pack("<fff", *point) for point in positions)
    normal_bytes = b"".join(struct.pack("<fff", *normal) for normal in normals)
    index_bytes = struct.pack("<" + "H" * len(indices), *indices)
    data = position_bytes + normal_bytes + index_bytes
    encoded = base64.b64encode(data).decode("ascii")
    path.write_text(json.dumps({
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2}]}],
        "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64," + encoded}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(position_bytes), "target": 34962},
                        {"buffer": 0, "byteOffset": len(position_bytes), "byteLength": len(normal_bytes), "target": 34962},
                        {"buffer": 0, "byteOffset": len(position_bytes) + len(normal_bytes), "byteLength": len(index_bytes),
                         "target": 34963}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3",
                       "min": [-2, 0, -2], "max": [2, 0.3, 2]},
                      {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
                      {"bufferView": 2, "componentType": 5123, "count": len(indices), "type": "SCALAR"}]
    }), encoding="utf-8")


def make_fixture(directory, case_name, case, base_project, demo, source_assets, handles, target_config="debug", gpu_source=False):
    assets = directory/"Assets"
    assets.mkdir(parents=True)
    for original in source_assets.iterdir():
        if original.name in ("Scenes", "Cache", "Scripts", "Meshes", "Audio"):
            continue
        destination = assets/original.name
        if original.is_dir():
            directory_link(original, destination)
        else:
            shutil.copy2(original, destination)
    # An owned copy of the Sounds, their clips and the demo's Sound Bank, never a linked input.
    shutil.copytree(source_assets/"Audio", assets/"Audio")
    # Only the copied scene belongs to this fixture. Export validates every registered
    # scene/prefab, so remove unrelated sample entries rather than weakening validation.
    registry_path = assets/"AssetRegistry.lzr"
    registry = yaml.safe_load(registry_path.read_text(encoding="utf-8"))
    registry["Assets"] = [row for row in registry["Assets"] if row["Type"] not in ("Scene", "Prefab")
                          or row["FilePath"].replace("\\", "/") == "Scenes/AudioDemo.luxscene"]
    registry_path.write_text(yaml.safe_dump(registry, sort_keys=False), encoding="utf-8")
    (assets/"Scenes").mkdir()
    # Assimp rewrites generated .lmat files alongside an imported model. Copy these
    # model folders rather than linking them, including their referenced textures.
    for model in ("Sponza", "Default"):
        shutil.copytree(source_assets/"Meshes/Source"/model, assets/"Meshes/Source"/model)
    if case.get("caustic"):
        make_curved_reflector(assets/"Meshes/Source/Default/Cube.gltf")
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
    entities = [listener, emitter, light] if case.get("zone") or case.get("simple") else [model, listener, emitter, light]
    if case.get("simple"):
        if case.get("caustic"):
            entities.insert(0, box(SPONZA_ID, "Concave reflector", [-0.5, -0.5, -0.5], [0.5, 0.5, 0.5],
                                   handles["Meshes/Source/Default/Cube.gltf"]))
        elif case.get("reflective"):
            entities.insert(0, box(SPONZA_ID, "Reflective floor", [-5, -0.1, -5], [5, 0, 5],
                                   handles["Meshes/Source/Default/Cube.gltf"]))
        else:
            entities.insert(0, box(SPONZA_ID, "Thick corner wall", [-0.1, -2, -2], [0.1, 2, 0],
                                   handles["Meshes/Source/Default/Cube.gltf"]))
    if case.get("zone"):
        corridor = case_name == "zone-corridor"
        low, high = ([-1, 0, -10], [1, 3, 10]) if corridor else ([-5, 0, -5], [5, 4, 5])
        thickness = 0.1
        bounds = [([low[0], -thickness, low[2]], [high[0], 0, high[2]]),
                  ([low[0], high[1], low[2]], [high[0], high[1] + thickness, high[2]]),
                  ([low[0] - thickness, low[1], low[2]], [low[0], high[1], high[2]]),
                  ([high[0], low[1], low[2]], [high[0] + thickness, high[1], high[2]]),
                  ([low[0], low[1], low[2] - thickness], [high[0], high[1], low[2]]),
                  ([low[0], low[1], high[2]], [high[0], high[1], high[2] + thickness])]
        for index, (wall_low, wall_high) in enumerate(bounds):
            entities.append(box(SPONZA_ID + index + 1, f"Zone wall {index}", wall_low, wall_high,
                                handles["Meshes/Source/Default/Cube.gltf"]))
        zone = entity(ZONE_ID, "Measured room", [0, 1.5, 0])
        zone["AudioZoneComponent"] = {"Shape": 0, "Enabled": True, "HalfExtents": [1, 1, 10] if corridor else [5, 3, 5],
                                      "BlendDistance": 0, "FadeTime": 0, "Volume": 1}
        entities.append(zone)
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
    (assets/"Scenes/AudioDemo.luxscene").write_text(yaml.safe_dump(scene, sort_keys=False), encoding="utf-8")
    project = copy.deepcopy(base_project)
    config = project["Project"]
    config.update({"Name": "Soundwave Sponza " + case_name, "AssetDirectory": "Assets", "AssetRegistry": "Assets/AssetRegistry.lzr",
                   "AutoSave": False, "StartScene": "Scenes/AudioDemo.luxscene"})
    config["RuntimeExport"].update({"GameName": "Soundwave-" + case_name, "TargetConfig": target_config.capitalize(),
                                    "WindowWidth": 1280, "WindowHeight": 720, "Fullscreen": False, "VSync": False})
    # No script components are present in these fixtures; do not request an unrelated
    # sample script build when exercising the normal export preflight.
    config["ScriptModulePath"] = ""
    audio = config["Audio"]
    audio["StudioProjectPath"] = "Audio/SampleProject/SampleProject.fspro"
    audio["StudioPlatform"] = "Desktop"
    audio["StudioBankOutputPath"] = "Build/Desktop"
    for platform in ("Windows", "Linux"):
        audio.setdefault(platform, {})["BankOutputPath"] = "Build/Desktop"
        audio[platform]["StudioPlatform"] = "Desktop"
    audio["RebuildBanksOnPlay"] = False
    audio["EnableLiveUpdate"] = False
    audio["AcousticMaterials"] = {"Default": {"AbsorptionLF": 0.2, "AbsorptionHF": 0.7, "Scattering": 0.1,
        "TransmissionLF": 0.001, "TransmissionHF": 0.001, "FlatTransmissionLF": 1, "FlatTransmissionHF": 1}}
    if case.get("zone"):
        audio["AcousticMaterials"]["Default"].update({"AbsorptionLF": 0.05, "AbsorptionHF": 0.1})
    audio["Occlusion"] = {"Source": "Engine", "UpdateRateHz": 1, "CastBudget": 32, "Strength": 1,
         "Kernel": {"Version": 1, "ReflectionDepth": 1 if case.get("reflective") else 0,
                    "OpeningGridSize": 1 if case.get("direct") or case.get("reflective") else 8,
                    "CausticFocus": 1.0 if case.get("caustic") else 0.0,
                   "MaxBounces": 1 if case.get("direct") else 2, "CandidateBudget": 65536,
                   "OpeningNodeBudget": 128 if gpu_source else 2048, "PathBudget": 64, "Absorption": 1, "CompareRaycast": True}}
    if case.get("zone"):
        audio["Occlusion"]["Kernel"].update({"EchogramWindowSeconds": 3.0, "EchogramSmoothingSeconds": 0.04})
    path = directory/"Acoustics.luxproj"
    path.write_text(yaml.safe_dump(project, sort_keys=False), encoding="utf-8")
    return path


def verify(data, case, triangle_counts, gpu_source=False):
    require(data["EngineRayComparison"], "Engine-ray comparison did not run")
    require(data.get("ReverbOn") == "GPU" and data.get("ReverbMeasurements", 0) > 0,
            f"Listener reverb did not run on the GPU: {data.get('ReverbOn')} x{data.get('ReverbMeasurements')}")
    if gpu_source:
        require(data.get("SourceVisibilityBatches", 0) > 0 and not data.get("SourceVisibilityGpuFailed"),
                "GPU source visibility did not complete a batch")
    require(0 <= data["ReverbSend"] <= 1 and data["ReverbDecaySeconds"] >= 0, "Listener reverb values out of range")
    expected = (6 if case.get("caustic") else triangle_counts["Default"] if case.get("simple") else
                6 * triangle_counts["Default"] if case.get("zone") else
                triangle_counts["Sponza"] + (5 * triangle_counts["Default"] if case.get("bay") else 0))
    require(data["StaticTriangles"] == expected, f"Sponza/partition geometry missing: {data['StaticTriangles']} != {expected}")
    require(data["MovingTriangles"] == (12 if case.get("closed") else 0), "Portal shutter capture disagrees with the authored state")
    require(data["StaticMeshInstances"] == (6 if case.get("zone") or case.get("bay") else 1), "A rendered model/partition instance was omitted")
    require(data["MovingMeshInstances"] == (1 if case.get("closed") else 0), "Portal instance state disagrees with its authoring")
    require(data["CheckedSources"] == 1 and len(data["Sources"]) == 1, "Probe did not verify its audio source")
    source = data["Sources"][0]
    require(source["UUID"] == SOURCE_ID, "Unexpected playback source")
    for name in ("listener", "source"):
        actual = source[name.title()]
        require(len(actual) == 3 and all(abs(a-b) < 0.00001 for a, b in zip(actual, case[name])), f"{name} moved during the pinned probe")
    require(source["DirectGainLFBits"] == source["EngineRayGainLFBits"], "Direct gain differs from the actual engine-ray oracle")
    if case.get("zone"):
        require(len(data["Zones"]) == 1 and data["Zones"][0]["UUID"] == ZONE_ID, "Active zone response missing")
        zone = data["Zones"][0]
        require(zone["Weight"] > 0.99 and zone["Arrivals"] > 0 and 0 <= zone["Send"] <= 1,
                "Zone response did not publish a timed, weighted measurement")
        require(abs(zone["BinSeconds"] - 3.0 / 256.0) < 0.00001,
                "Authored echogram window did not reach the zone worker")
        require(abs(source["AppliedReverbSend"] - zone["Send"]) < 0.001,
                "Zone response did not drive the source's playback reverb send")
        require(source["RawGainLF"] == 1, "Clear zone source is occluded")
        return {"ZoneSend": zone["Send"], "ZoneDecaySeconds": zone["DecaySeconds"],
                "DirectGainLFBits": source["DirectGainLFBits"], "EngineRayGainLFBits": source["EngineRayGainLFBits"],
                "OpeningPaths": source["OpeningPaths"], "RawGainLF": source["RawGainLF"]}
    if case.get("clear"):
        require(source["RawGainLF"] == 1 and source["EngineRayGainLF"] == 1, "Clear control is obstructed")
        if case.get("reflective"):
            require(source["ReflectionPaths"] > 0, "The verified source has no reflection path")
    else:
        require(source["EngineRayWalls"] > 0 and source["DirectGainLF"] <= 0.001001, "Control ray did not encounter the opaque geometry")
        if case.get("audible"):
            require(source["OpeningPaths"] > 0 and "AudibleOpening" in source and source["RawGainLF"] > 0.01,
                    "No audible route around Sponza's obstruction")
            require(source["OcclusionGainLF"] > source["EngineRayGainLF"] * 5, "Audible route did not reach playback")
        else:
            require(source["RawGainLF"] == source["DirectGainLF"] and source["OpeningPaths"] == 0,
                    "Closed/direct-only control acquired an unintended opening route")
    if case.get("caustic"):
        require(source["AddedFocusEnergy"] > 0.01 and source["FocusedReflectionSend"] > 0.01,
                "Concave reflector did not contribute focused reflection energy")
        require(source["AppliedReverbSend"] > 0.01, "Caustic focus did not reach the source reverb send")
    return {key: source[key] for key in ("DirectGainLF", "RawGainLF", "OcclusionGainLF", "EngineRayGainLF", "OpeningPaths",
                                        "ReflectionPaths", "WorkLimited", "AddedFocusEnergy", "FocusedReflectionSend")}


def prepare_linux_validation(environment, directory):
    # A layer manifest can name only a DSO basename, which normally needs SDK/lib in
    # LD_LIBRARY_PATH. Pin just this tool to an absolute path instead: game-library
    # resolution must remain isolated to the package. Never change validation settings.
    sdk = Path(environment.get("VULKAN_SDK") or ROOT/"Core/vendor/VulkanSDK/x86_64")
    source = sdk/"share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json"
    description = json.loads(source.read_text(encoding="utf-8"))
    layer = description["layer"]
    require(layer["name"] == "VK_LAYER_KHRONOS_validation", f"Unexpected validation manifest: {source}")
    library = Path(layer["library_path"])
    if not library.is_absolute():
        library = (sdk/"lib"/library) if library.parent == Path(".") else (source.parent/library)
    require(library.is_file(), f"Vulkan validation library is missing: {library}")
    layer["library_path"] = str(library.resolve())
    directory.mkdir(parents=True, exist_ok=True)
    manifest = directory/source.name
    manifest.write_text(json.dumps(description, indent=2)+"\n", encoding="utf-8")
    environment["VK_LAYER_PATH"] = str(directory.resolve())
    return manifest


def verify_export(capture, destination, case, triangle_counts, timeout, threading, runtime_config, gpu_source=False):
    metadata = yaml.safe_load((capture/"AudioDemo.export.yaml").read_text(encoding="utf-8"))
    package, executable = Path(metadata["Root"]).resolve(), Path(metadata["Executable"]).resolve()
    require(package.is_relative_to(destination.resolve()) and executable.parent == package,
            "Export metadata points outside the isolated destination")
    for path in (executable, package/"Assets/Project.luxruntime", package/"Assets/AssetPack.lap", package/"Assets/ShaderPack.lsp"):
        require(path.is_file() and path.stat().st_size > 0, f"Missing exported artifact: {path}")
    for path in package.rglob("*"):
        name = path.name.lower().removeprefix("lib")
        require(not name.startswith(("fmod", "vaudionative", "asterwave")) or
                (".so" not in name and not name.endswith(".dll")),
                f"Export bundled a retired audio SDK library: {path}")
    if os.name != "nt":
        require(not (package/"lib/libvulkan.so.1").exists(), "Export bundled the Vulkan SDK loader instead of using the host driver stack")
    report_path = capture/"AudioDemo.runtime-audio.yaml"
    env = {key: value for key, value in os.environ.items() if not key.startswith("LUX_GOLDEN_")}
    env.update({"LUX_RUNTIME_AUDIO_REPORT": str(report_path.resolve()), "LUX_NO_WATCHDOG": "1",
                "LUX_THREADING_POLICY": threading.capitalize()})
    if gpu_source:
        env["LUX_VERIFY_GPU_SOURCE"] = "1"
    if os.name != "nt":
        # Build-tree paths must not hide a missing packaged shared library.
        env["LD_LIBRARY_PATH"] = str(package/"lib")
        if runtime_config == "debug":
            prepare_linux_validation(env, capture/"vulkan-validation")
    started = time.time_ns()
    log_path = capture/"AudioDemo.runtime.log"
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
    verified = verify(data, case, triangle_counts, gpu_source)
    play = yaml.safe_load((capture/"AudioDemo.audio.yaml").read_text(encoding="utf-8"))
    # Pinned scenes settle to the same reference result; compare raw solver output
    # rather than frame-rate-dependent smoothing during startup.
    for key in ("DirectGainLFBits", "EngineRayGainLFBits", "OpeningPaths", "ReflectionPaths"):
        require(data["Sources"][0][key] == play["Sources"][0][key], f"Play/runtime mismatch: {key}")
    require(abs(data["Sources"][0]["RawGainLF"] - play["Sources"][0]["RawGainLF"]) < 0.00001,
            "Play/runtime raw acoustic gains differ")
    # Same listener, geometry and kernel: the GPU reverb must agree between Play and the package.
    require(abs(data["ReverbDecaySeconds"] - play["ReverbDecaySeconds"]) <= 0.01 * max(play["ReverbDecaySeconds"], 0.01),
            "Play/runtime listener reverb differs")
    if case.get("caustic"):
        require(abs(data["Sources"][0]["FocusedReflectionSend"] - play["Sources"][0]["FocusedReflectionSend"]) < 0.001,
                "Play/runtime focused reflection send differs")
    if case.get("zone"):
        require(abs(data["Zones"][0]["Send"] - play["Zones"][0]["Send"]) < 0.02,
                "Play/runtime zone reverb differs")
    return {"package": str(package), "report": str(report_path), "values": verified}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=("debug", "release"), default="debug")
    parser.add_argument("--export-runtime", action="store_true", help="export each isolated project normally, run the packaged game and verify its real acoustic values")
    parser.add_argument("--runtime-config", choices=("debug", "release", "dist"), help="exported runtime configuration (defaults to --config)")
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    parser.add_argument("--threading", choices=("single", "multi"), default="multi")
    parser.add_argument("--gpu-source", action="store_true", help="require a completed GPU source-visibility batch")
    parser.add_argument("--output", type=Path, default=ROOT/"bin/soundwave-scene-tests",
                        help="Parent directory for fixtures, exports and results")
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()
    sample = ROOT/"Editor/LuxSampleProject"
    project_path, demo_path, registry_path = sample/"LuxSample.luxproj", sample/"Assets/Scenes/AudioDemo.luxscene", sample/"Assets/AssetRegistry.lzr"
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
    run = args.output.resolve()/run_id
    run.mkdir(parents=True)
    results = {}
    try:
        for name in args.cases:
            case = CASES[name]
            fixture = make_fixture(run/name, name, case, project, demo, sample/"Assets", handles, args.runtime_config or args.config,
                                   args.gpu_source)
            label = run_id + "-" + name
            destination = run/name/"export"
            command = [sys.executable, str(ROOT/"tests/rendering/golden_run.py"), "--config", args.config, "--project", str(fixture),
                       "--label", label, "--scenes", "AudioDemo", "--play", "--threading", args.threading,
                       "--frame", "2", "--perf-frames", "1", "--perf-warmup", "0", "--timeout", str(args.timeout)]
            if args.export_runtime:
                command += ["--export-dir", str(destination)]
            environment = dict(os.environ)
            if args.gpu_source:
                environment["LUX_VERIFY_GPU_SOURCE"] = "1"
            subprocess.run(command, cwd=ROOT, env=environment, check=True)
            capture = ROOT/"bin/golden"/label
            artifact = capture/"AudioDemo.audio.yaml"
            # golden_run already checked this run's final artifact timestamp once.
            summary = json.loads((capture/"summary.json").read_text(encoding="utf-8"))
            require(summary["runs"][0]["validationMessages"] == 0, "Native run reported Vulkan validation messages")
            results[name] = verify(yaml.safe_load(artifact.read_text(encoding="utf-8")), case, triangle_counts, args.gpu_source)
            results[name]["capture"] = str(capture)
            if "zone-stone" in results and "zone-corridor" in results:
                require(results["zone-stone"]["ZoneSend"] > results["zone-corridor"]["ZoneSend"] + 0.05,
                        "Stone room and corridor zone sends sound alike")
            if args.export_runtime:
                results[name]["runtime"] = verify_export(capture, destination, case, triangle_counts, args.timeout, args.threading,
                                                          args.runtime_config or args.config, args.gpu_source)
            print(name + ": PASS " + json.dumps(results[name]), flush=True)
            (run/"results.json").write_text(json.dumps(results, indent=2)+"\n", encoding="utf-8")
    finally:
        for path, digest in original.items():
            require(hashlib.sha256(path.read_bytes()).hexdigest() == digest, f"Original project input changed during the isolated run: {path}")
    print("PASS: isolated native Sponza Play/oracle cases; original project, scene, registry and model bytes preserved. Evidence: " + str(run), flush=True)


if __name__ == "__main__":
    main()
