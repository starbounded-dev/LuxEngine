#!/usr/bin/env python3
"""Verify that exported scenes run their authored NRD or DLSS Ray Reconstruction mode."""

import argparse
from pathlib import Path
import re
import shutil
from tempfile import TemporaryDirectory

from golden_export import ROOT, export_and_capture
from run_rt_sampling_check import check, load_image, rmse


def capture_variant(root, tag, scene, metadata, config, materials):
    source = ROOT / "Editor/LuxSampleProject"
    assets = root / "Assets"
    geometry = ("Meshes/Source/Default/Cube.gltf", "Meshes/Source/Default/Cube.bin")
    for relative in (*geometry, *(f"Materials/GI/{material}.lmat" for material in materials)):
        destination = assets / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / "Assets" / relative, destination)
    (assets / "Scenes").mkdir()
    fixture = (source / f"Assets/Scenes/{scene}.luxscene").read_text(encoding="utf-8")
    (assets / f"Scenes/{scene}.luxscene").write_text(fixture.replace("Entities:", metadata + "Entities:", 1), encoding="utf-8")
    text = (source / "LuxSample.luxproj").read_text(encoding="utf-8")
    for old, new in (
        ("Name: LuxSample", f"Name: {scene}"),
        ("StartScene: Scenes/NewSceneSystem.luxscene", f"StartScene: Scenes/{scene}.luxscene"),
        ("TargetConfig: Dist", f"TargetConfig: {config.capitalize()}"),
        ("WindowWidth: 1920", "WindowWidth: 961"),
        ("WindowHeight: 1080", "WindowHeight: 541"),
        ("IconPath: Textures/luxLogo.png", 'IconPath: ""'),
        ("AutoSave: true", "AutoSave: false"),
    ):
        text = text.replace(old, new)
    project = root / f"{scene}.luxproj"
    project.write_text(text, encoding="utf-8")
    package, capture, log = export_and_capture(project, scene, tag, config)
    check(not re.search(r"VUID-|Validation Error:|SYNC-HAZARD", log), f"{tag} passed Vulkan validation")
    return package, load_image(capture), log


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="release", choices=("debug", "release"))
    parser.add_argument("--denoiser", default="all", choices=("all", "nrd", "dlss"))
    args = parser.parse_args()
    modes = (
        ("nrd", "GI_CornellDark", ("GI_White", "GI_Red", "GI_Green"),
         "RayTracedGI:\n  Enabled: true\n  UseNRD: true\n", "",
         "NRD RELAX diffuse active", "NRD.dll", "NRD-LICENSE.txt"),
        ("dlss", "GI_CornellDark", ("GI_White", "GI_Red", "GI_Green"),
         "PathTracing:\n  Enabled: true\n  MaxBounces: 3\n  TargetSamples: 1\n  RayReconstruction: true\n",
         "PathTracing:\n  Enabled: true\n  MaxBounces: 3\n  TargetSamples: 1\n",
         "DLSS Ray Reconstruction active", "nvngx_dlssd.dll", "DLSS-LICENSE.txt"),
    )
    # Windows may briefly retain a package directory handle after a successful player exit.
    with TemporaryDirectory(prefix="vendor-denoiser-export-", dir=ROOT / "bin", ignore_cleanup_errors=True) as directory:
        root = Path(directory)
        for name, scene, materials, enabled, disabled, active, library, notice in modes:
            if args.denoiser not in ("all", name):
                continue
            tag = f"denoiser-export-{name}"
            package, image, log = capture_variant(root / name, tag, scene, enabled, args.config, materials)
            check((package / library).is_file() and (package / notice).is_file(), f"{name} export bundles the SDK and license")
            check(active in log and "DLSS Ray Reconstruction failed" not in log and "NRD settings were rejected" not in log,
                  f"{name} dispatches from the exported scene's settings")
            _, reference, off_log = capture_variant(root / f"{name}-off", tag + "-off", scene, disabled, args.config, materials)
            check(active not in off_log, f"{name} control export uses the built-in path")
            error = rmse(image, reference)
            check(error > 0.001, f"{name} measurably changes the exported player's image ({error:.4f} RMSE)")


if __name__ == "__main__":
    main()
