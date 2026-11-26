#!/usr/bin/env python3
"""Verify that the exported runtime streams pages and restores resident image detail."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
from tempfile import TemporaryDirectory

from run_rt_sampling_check import ROOT, check, load_image, rmse, texture_streaming_project


def capture_package(root, mode, config):
    scene = "TextureStreaming"
    project = texture_streaming_project(root, mode)
    settings = project.read_text(encoding="utf-8")
    for old, new in (
        ("TargetConfig: Dist", f"TargetConfig: {config.capitalize()}"),
        ("WindowWidth: 1920", "WindowWidth: 961"),
        ("WindowHeight: 1080", "WindowHeight: 541"),
        ("IconPath: Textures/luxLogo.png", 'IconPath: ""'),
    ):
        settings = settings.replace(old, new)
    project.write_text(settings, encoding="utf-8")
    label = f"texture-streaming-export-{mode}"
    golden = ROOT / "bin/golden" / label
    subprocess.run([
        sys.executable, str(ROOT / "tests/rendering/golden_run.py"),
        "--config", config, "--project", str(project), "--label", label, "--scenes", scene,
        "--size", "961x541", "--frame", "100", "--perf-frames", "10", "--export-dir", str(root / "Export"),
    ], cwd=ROOT, check=True)
    metadata = (golden / f"{scene}.export.yaml").read_text(encoding="utf-8")
    fields = dict(line.split(": ", 1) for line in metadata.splitlines() if ": " in line)
    package, executable = (Path(fields[name].strip().strip("'\"")) for name in ("Root", "Executable"))
    image = golden / "Player.lximg"
    image.unlink(missing_ok=True)
    logfile = golden / "Player.log"
    env = dict(os.environ, LUX_NO_WATCHDOG="1", LUX_RUNTIME_CAPTURE=str(image), LUX_RUNTIME_CAPTURE_FRAME="120")
    with logfile.open("w", encoding="utf-8") as log:
        subprocess.run([str(executable), "--project=" + str(package)], cwd=package, env=env,
                       stdout=log, stderr=subprocess.STDOUT, timeout=120, check=True)
    log = logfile.read_text(encoding="utf-8", errors="replace")
    check(not re.search(r"VUID-|Validation Error:|SYNC-HAZARD", log), f"exported mode {mode} renders cleanly under Vulkan validation")
    stats = re.search(r"Runtime capture residency: (\d+) virtual pages, (\d+) page uploads", log)
    check(stats is not None, f"exported mode {mode} reports runtime residency")
    return load_image(image), tuple(map(int, stats.groups()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="release", choices=("debug", "release"))
    args = parser.parse_args()
    with TemporaryDirectory(prefix="texture-streaming-export-", dir=ROOT / "bin") as directory:
        root = Path(directory)
        reference, resident = capture_package(root / "resident", 0, args.config)
        streamed, pages = capture_package(root / "virtual", 2, args.config)
        check(resident == (0, 0) and 0 < pages[0] <= pages[1], f"exported virtual texture uses visible pages ({pages})")
        error = rmse(reference[170:370, 340:620], streamed[170:370, 340:620])
        check(error < 0.015, f"exported virtual texture matches resident surface detail ({error:.4f} RMSE)")


if __name__ == "__main__":
    main()
