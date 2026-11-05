#!/usr/bin/env python3
"""Export saved material/Sound/Mixer panel fixtures and run the packaged player (requires PyYAML)."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import yaml

from native_checks import require_clean_graphics_log

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/audio"))
import run_scene_acoustics as scenes


def passed_fixture(folder, asset):
    candidates = sorted((ROOT / "bin" / folder).glob("*/Multi/project"),
                        key=lambda path: path.parent.parent.stat().st_mtime_ns, reverse=True)
    for project in candidates:
        status = project.with_suffix(".status")
        if status.is_file() and status.read_text() == "PASS\n" and (project / asset).is_file():
            return project
    raise RuntimeError(f"No passed {folder} fixture; run the corresponding panel runner first")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--threading", choices=("single", "multi"), default="multi")
    parser.add_argument("--runtime-config", choices=("debug", "release", "dist"), default="dist")
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    material_project = passed_fixture("material-panel-smoke", "Assets/Surface.lmat")
    audio_project = passed_fixture("audio-panels-smoke", "Assets/Audio/é##Editor.lsound")
    audio_config_path = next(audio_project.glob("*.luxproj"))
    sample = ROOT / "Editor/LuxSampleProject"
    inputs = [sample / "LuxSample.luxproj", sample / "Assets/Scenes/AudioDemo.luxscene",
              sample / "Assets/AssetRegistry.lzr", material_project / "Assets/Surface.lmat",
              audio_project / "Assets/Audio/é##Editor.lsound", audio_project / "Assets/Tone.wav",
              audio_config_path]
    for model in ("Sponza", "Default"):
        inputs.extend(path for path in (sample / "Assets/Meshes/Source" / model).rglob("*")
                      if path.is_file())
    original = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    run_id = "remaster-export-" + str(time.time_ns())
    output = ROOT / "bin/remaster-export" / run_id
    user = output / "user"
    user.mkdir(parents=True)
    (user / "UserPreferences.yaml").write_text(
        "UserPrefs:\n  ShowSplashScreen: false\n  RecentProjects: []\n", encoding="utf-8")
    project = yaml.safe_load(inputs[0].read_text(encoding="utf-8"))
    demo = yaml.safe_load(inputs[1].read_text(encoding="utf-8"))
    registry = yaml.safe_load(inputs[2].read_text(encoding="utf-8"))
    handles = {row["FilePath"].replace("\\", "/"): row["Handle"] for row in registry["Assets"]}
    triangle_counts = {}
    for model, filename in (("Sponza", "Sponza"), ("Default", "Cube")):
        gltf = json.loads((sample / f"Assets/Meshes/Source/{model}/{filename}.gltf").read_text())
        triangle_counts[model] = sum(gltf["accessors"][primitive["indices"]]["count"] // 3
                                     for mesh in gltf["meshes"] for primitive in mesh["primitives"])
    case = scenes.CASES["corner"]
    fixture = scenes.make_fixture(output / "project", "corner", case, project, demo,
                                  sample / "Assets", handles, args.runtime_config)
    assets = fixture.parent / "Assets"
    owned = assets / "Remaster"
    owned.mkdir()
    material_id, sound_id, clip_id, bank_id = range(93049999999999901, 93049999999999905)
    registry_path = assets / "AssetRegistry.lzr"
    registry = yaml.safe_load(registry_path.read_text(encoding="utf-8"))
    scenes.require(not {material_id, sound_id, clip_id, bank_id}.intersection(
        row["Handle"] for row in registry["Assets"]), "Fixture handles collide with sample inputs")
    shutil.copy2(material_project / "Assets/Surface.lmat", owned / "Surface.lmat")
    shutil.copy2(audio_project / "Assets/Tone.wav", owned / "Tone.wav")
    material = yaml.safe_load((owned / "Surface.lmat").read_text(encoding="utf-8"))["Material"]
    scenes.require(material["Graph"]["Links"] and material["Roughness"] == 0.19 and
                   material["Metalness"] == 0.66, "Passed material fixture lost its authored results")
    sound = yaml.safe_load((audio_project / "Assets/Audio/é##Editor.lsound").read_text(encoding="utf-8"))
    scenes.require(sound["Sound"]["Volume"] == 0, "Use the panel runner's silent real-PCM Sound")
    old_clips = {node.get("Clip") for node in sound["Sound"]["Nodes"] if node["Type"] == "Clip"}
    registry_bytes = (audio_project / "Assets/AssetRegistry.lzr").read_bytes()
    try:
        registry_text = registry_bytes.decode("utf-8")
    except UnicodeDecodeError:
        if os.name != "nt":
            raise RuntimeError("Recreate panel fixtures on Linux; this registry uses the legacy Windows encoding")
        # EditorAssetManager currently writes generic_string(), the Windows ANSI encoding.
        # Reading that existing format here must not silently rewrite or migrate source assets.
        print("Reading the panel fixture's legacy Windows-encoded registry", flush=True)
        registry_text = registry_bytes.decode("mbcs")
    audio_registry = yaml.safe_load(registry_text)
    tone_id = next(row["Handle"] for row in audio_registry["Assets"] if row["FilePath"] == "Tone.wav")
    scenes.require(old_clips == {tone_id}, "Sound fixture has unexpected clip dependencies")
    for node in sound["Sound"]["Nodes"]:
        if node["Type"] == "Clip":
            node["Clip"] = clip_id  # Rebind the copied clip; retain every authored Sound property.
    (owned / "Sound.lsound").write_text(yaml.safe_dump(sound, sort_keys=False), encoding="utf-8")
    bank = {"SoundBank": {"Version": 1, "Resident": False, "Sounds": [sound_id]}}
    (owned / "Bank.lbank").write_text(yaml.safe_dump(bank, sort_keys=False), encoding="utf-8")
    for handle, name, kind in ((material_id, "Surface.lmat", "Material"),
                               (sound_id, "Sound.lsound", "Sound"),
                               (clip_id, "Tone.wav", "Audio"), (bank_id, "Bank.lbank", "SoundBank")):
        registry["Assets"].append({"Handle": handle, "FilePath": "Remaster/" + name, "Type": kind})
    registry_path.write_text(yaml.safe_dump(registry, sort_keys=False), encoding="utf-8")
    scene_path = assets / "Scenes/AudioDemo.luxscene"
    scene = yaml.safe_load(scene_path.read_text(encoding="utf-8"))
    scene["SoundBanks"] = [bank_id]
    material_count = len(json.loads((sample / "Assets/Meshes/Source/Sponza/Sponza.gltf").read_text())["materials"])
    for entity in scene["Entities"]:
        if entity["Entity"] == scenes.SPONZA_ID:
            entity["StaticMeshComponent"]["MaterialTable"] = {slot: material_id for slot in range(material_count + 1)}
        if entity["Entity"] == scenes.SOURCE_ID:
            entity["AudioSourceComponent"]["EventSound"] = sound_id
    scene_path.write_text(yaml.safe_dump(scene, sort_keys=False), encoding="utf-8")
    config = yaml.safe_load(fixture.read_text(encoding="utf-8"))
    saved_audio = yaml.safe_load(audio_config_path.read_text(encoding="utf-8"))["Project"]["Audio"]
    config["Project"]["Audio"]["Mixer"] = copy.deepcopy(saved_audio["Mixer"])
    fixture.write_text(yaml.safe_dump(config, sort_keys=False), encoding="utf-8")
    destination = output / "export"
    command = [sys.executable, str(ROOT / "tests/rendering/golden_run.py"), "--config", "debug",
               "--project", str(fixture), "--label", run_id, "--scenes", "AudioDemo", "--play",
               "--threading", args.threading, "--frame", "2", "--perf-frames", "1", "--perf-warmup", "0",
               "--timeout", str(args.timeout), "--export-dir", str(destination)]
    previous_user = os.environ.get("LUX_USER_DATA_DIR")
    try:
        os.environ["LUX_USER_DATA_DIR"] = str(user)
        subprocess.run(command, cwd=ROOT, check=True)
        capture = ROOT / "bin/golden" / run_id
        summary = json.loads((capture / "summary.json").read_text())
        scenes.require(summary["runs"][0]["validationMessages"] == 0, "Play reported validation messages")
        require_clean_graphics_log(capture / "AudioDemo.log")
        result = scenes.verify_export(capture, destination, case, triangle_counts,
                                      args.timeout, args.threading, args.runtime_config)
        require_clean_graphics_log(capture / "AudioDemo.runtime.log")
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    finally:
        if previous_user is None:
            os.environ.pop("LUX_USER_DATA_DIR", None)
        else:
            os.environ["LUX_USER_DATA_DIR"] = previous_user
        for path, digest in original.items():
            scenes.require(hashlib.sha256(path.read_bytes()).hexdigest() == digest,
                           f"Verification changed an original input: {path}")
    print("PASS: saved graph material, real-PCM Sound and Mixer exported and packaged player ran", flush=True)
    print("Evidence:", output)


if __name__ == "__main__":
    main()
