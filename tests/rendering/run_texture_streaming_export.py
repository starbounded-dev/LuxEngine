#!/usr/bin/env python3
"""Verify that the exported runtime streams pages and restores resident image detail."""

import argparse
from pathlib import Path
import re
from tempfile import TemporaryDirectory

from golden_export import export_and_capture
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
    _, image, log = export_and_capture(project, scene, f"texture-streaming-export-{mode}", config)
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
