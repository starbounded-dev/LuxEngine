#!/usr/bin/env python3
"""Single- vs multi-threaded A/B of the editor (see .claude/docs/Rendering.md § Golden image capture).

    threading_ab.py [--config release] [--runs 3] [--scenes FMODDemo,Benchmark] [--no-build]

Builds the editor first (Windows: premake with the saved options, then MSBuild; Linux: make Core and
Editor), then captures the scene set under each policy with golden_run.py --threading, alternating
the policies so drift over the session hits both. Captures land in bin/golden/ab-<policy>-<run>/.
Prints per-scene medians and checks that both policies rendered identical images. Exits 1 if a
capture or the build failed, or the images differ. The user's threading setting is never changed.
"""

import argparse
import glob
import json
import os
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GOLDEN_ROOT = ROOT / "bin" / "golden"
GOLDEN_RUN = Path(__file__).resolve().parent / "golden_run.py"
POLICIES = ["single", "multi"]
METRICS = ["frameMs", "mainThreadMs", "renderThreadMs", "gpuMs"]


def find_msbuild():
    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if vswhere.exists():
        output = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.Component.MSBuild",
                                 "-find", r"MSBuild\**\Bin\MSBuild.exe"], capture_output=True, text=True, check=False)
        candidates = [line.strip() for line in output.stdout.splitlines() if line.strip()]
        if candidates:
            return candidates[0]
    return shutil.which("MSBuild") or shutil.which("msbuild")


def build(config):
    if os.name == "nt":
        # --last regenerates with the options saved by the previous Win-GenProjects run, no prompts.
        if subprocess.run([sys.executable, "Win-GenProjects.py", "--last"], cwd=ROOT / "scripts", check=False).returncode != 0:
            return False
        solutions = sorted(glob.glob(str(ROOT / "*.sln")) + glob.glob(str(ROOT / "*.slnx")))
        msbuild = find_msbuild()
        if not solutions or not msbuild:
            print("No generated solution or no MSBuild found; build Release in Visual Studio and pass --no-build.")
            return False
        command = [msbuild, solutions[0], "/restore", "/m", "/v:minimal", "/nologo",
                   f"/p:Configuration={config.capitalize()}", "/p:Platform=Mixed Platforms"]
        return subprocess.run(command, cwd=ROOT, check=False).returncode == 0
    for project in ("Core", "Editor"):
        if subprocess.run(["make", f"config={config}", project, f"-j{os.cpu_count() or 8}"], cwd=ROOT, check=False).returncode != 0:
            return False
    return True


def label(policy, run):
    return f"ab-{policy}-{run}"


def load_perf(policy, runs, scene):
    results = []
    for run in range(1, runs + 1):
        path = GOLDEN_ROOT / label(policy, run) / f"{scene}.perf.json"
        if path.exists():
            results.append(json.loads(path.read_text(encoding="utf-8")))
    return results


def median(perf, key):
    values = [entry[key] for entry in perf if key in entry]
    return statistics.median(values) if values else None


def report(args):
    print(f"\nMedians over {args.runs} run(s) per policy ({args.config}, 1280x720):")
    print(f"{'scene':<14} {'metric':<15} {'single':>9} {'multi':>9} {'delta':>8}")
    for scene in args.scenes:
        single, multi = load_perf("single", args.runs, scene), load_perf("multi", args.runs, scene)
        for key in METRICS:
            a, b = median(single, key), median(multi, key)
            if a is None or b is None:
                continue
            delta = f"{(b - a) / a * 100:+.1f}%" if a else ""
            print(f"{scene:<14} {key:<15} {a:9.2f} {b:9.2f} {delta:>8}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="release", choices=["debug", "release", "dist"])
    parser.add_argument("--runs", type=int, default=3, help="captures per policy; the report uses the median")
    parser.add_argument("--scenes", default="FMODDemo,Benchmark,Physics,LaptopStart,SkyDiver")
    parser.add_argument("--no-build", action="store_true", help="use the editor that is already built")
    args = parser.parse_args()
    args.scenes = [scene for scene in args.scenes.split(",") if scene]

    if not args.no_build and not build(args.config):
        print("Build failed.")
        return 1

    failed = False
    for run in range(1, args.runs + 1):
        for policy in POLICIES:
            command = [sys.executable, str(GOLDEN_RUN), "--label", label(policy, run), "--config", args.config,
                       "--threading", policy, "--scenes", ",".join(args.scenes)]
            failed |= subprocess.run(command, check=False).returncode != 0

    print("\nImages, single vs multi:", flush=True)
    images_differ = subprocess.run([sys.executable, str(GOLDEN_RUN), "--compare", label("single", 1), label("multi", 1)],
                                   check=False).returncode != 0

    report(args)
    if failed:
        print("\nAt least one capture did not finish; see bin/golden/ab-*/<scene>.log.")
    return 1 if failed or images_differ else 0


if __name__ == "__main__":
    sys.exit(main())
