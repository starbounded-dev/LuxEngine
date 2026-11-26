#!/usr/bin/env python3
"""Export a scene with analytic fog and measure fog in the standalone player's frame."""

import argparse
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from golden_compare import Capture
from make_rt_scenes import ROOT, height_fog_scene


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="release", choices=("debug", "release"))
    parser.add_argument("--runtime-config", choices=("debug", "release", "dist"))
    args = parser.parse_args()

    scene = "HeightFogExport"
    source = ROOT / "Editor/LuxSampleProject"
    with tempfile.TemporaryDirectory(prefix="fog-export-", dir=ROOT / "bin") as directory:
        root = Path(directory)
        assets = root / "Assets"
        project = root / f"{scene}.luxproj"
        for relative in ("Meshes/Source/Default/Cube.gltf", "Meshes/Source/Default/Cube.bin", "Materials/GI/GI_White.lmat"):
            destination = assets / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source / "Assets" / relative, destination)
        (assets / "Scenes").mkdir()
        (assets / f"Scenes/{scene}.luxscene").write_text(height_fog_scene(scene), encoding="utf-8")
        text = (source / "LuxSample.luxproj").read_text(encoding="utf-8")
        runtime_config = (args.runtime_config or args.config).capitalize()
        for old, new in (
            ("Name: LuxSample", f"Name: {scene}"),
            ("StartScene: Scenes/NewSceneSystem.luxscene", f"StartScene: Scenes/{scene}.luxscene"),
            ("TargetConfig: Dist", f"TargetConfig: {runtime_config}"),
            ("WindowWidth: 1920", "WindowWidth: 961"),
            ("WindowHeight: 1080", "WindowHeight: 541"),
            ("IconPath: Textures/luxLogo.png", 'IconPath: ""'),
            ("AutoSave: true", "AutoSave: false"),
        ):
            text = text.replace(old, new)
        project.write_text(text, encoding="utf-8")
        label = "height-fog-export-windows" if os.name == "nt" else "height-fog-export-linux"
        golden = ROOT / "bin/golden" / label
        command = [
            sys.executable, str(ROOT / "tests/rendering/golden_run.py"),
            "--config", args.config, "--project", str(project), "--label", label, "--scenes", scene,
            "--size", "961x541", "--frame", "100", "--options", "Bloom=0,DOF=0,Lightmaps=0",
            "--perf-frames", "10", "--export-dir", str(root / "Export"),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        metadata = (golden / f"{scene}.export.yaml").read_text(encoding="utf-8")
        fields = dict(line.split(": ", 1) for line in metadata.splitlines() if ": " in line)
        package, executable = (Path(fields[name].strip().strip("'\"")) for name in ("Root", "Executable"))
        image_path = golden / "Player.lximg"
        image_path.unlink(missing_ok=True)
        env = dict(os.environ, LUX_NO_WATCHDOG="1", LUX_RUNTIME_CAPTURE=str(image_path), LUX_RUNTIME_CAPTURE_FRAME="120")
        with (golden / "Player.log").open("w", encoding="utf-8") as log:
            subprocess.run([str(executable), "--project=" + str(package)], cwd=package, env=env,
                           stdout=log, stderr=subprocess.STDOUT, timeout=120, check=True)
        capture = Capture(image_path)
        pixels = capture.values()
        scale = 1.0 if capture.is_float() else 1.0 / 255.0
        # The source geometry is unlit and the fog has unit red radiance. The near and far
        # surfaces are 3.95 m and 9.95 m away along the camera's forward axis.
        length_scale = math.sqrt(1 + (0.32 * math.tan(math.radians(25)) * capture.width / capture.height) ** 2)
        y = round(capture.height * 0.5)
        for u, distance in ((0.34, 3.95), (0.66, 9.95)):
            x = round(capture.width * u)
            red = sum(pixels[(row * capture.width + col) * capture.channels]
                      for row in range(y - 3, y + 4) for col in range(x - 3, x + 4)) * scale / 49
            expected = 1 - math.exp(-0.12 * length_scale * distance)
            if abs(red - expected) >= 0.03:
                raise SystemExit(f"Exported fog at {distance:.2f} m: {red:.3f}, expected {expected:.3f}")
            print(f"PASS: exported fog at {distance:.2f} m matches analytic extinction ({red:.3f}, expected {expected:.3f})")


if __name__ == "__main__":
    main()
