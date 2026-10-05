#!/usr/bin/env python3
"""Capture and compare golden renderer images (see .claude/docs/Rendering.md § Golden image capture).

Capture a labelled set (one editor launch per scene / feature variant):

    golden_run.py --label nvrhi-baseline [--config release] [--features] [--scenes FMODDemo,Physics]
                  [--sync-validation] [--timeout 900]

Compare two labelled sets (every capture present in both):

    golden_run.py --compare nvrhi-baseline p1 [--max-bad-percent 0.05]

Captures land in bin/golden/<label>/ (not committed). Each run writes <name>.lximg, <name>.perf.json,
<name>.status and <name>.log; the label directory gets summary.json (status, timings and the
validation messages found in each log) and validation.txt (unique validation identifiers).
Validation output only exists in Debug builds, where the editor enables the Khronos layer.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GOLDEN_ROOT = ROOT / "bin" / "golden"
COMPARE = Path(__file__).resolve().parent / "golden_compare.py"
DEFAULT_PROJECT = ROOT / "Editor" / "LuxSampleProject" / "LuxSample.luxproj"
DEFAULT_SCENES = ["FMODDemo", "Benchmark", "Physics", "LaptopStart", "SkyDiver"]

# One extra capture of the first scene per entry, each changing one renderer option from its default.
FEATURE_VARIANTS = {
    "noSSR": "SSR=0",
    "noGTAO": "GTAO=0",
    "noBloom": "Bloom=0",
    "DOF": "DOF=1",
    "SMAA": "SMAA=1",
    "colliders": "Colliders=1",
    "debugCategories": "DebugCategories=1",
    "noVRS": "VRS=0",
    "noOcclusionCulling": "OcclusionCulling=0",
    "noSoftShadows": "SoftShadows=0",
    "asyncCompute": "AsyncCompute=1",
    "meshShaders": "MeshShaders=1",
}

VALIDATION_LINE = re.compile(r"Vulkan (performance )?validation (error|warning)")
VALIDATION_ID = re.compile(r"\b(VUID-[A-Za-z0-9_-]+|UNASSIGNED-[A-Za-z0-9_.-]+|SYNC-[A-Za-z0-9_-]+|BestPractices-[A-Za-z0-9_-]+)")


def editor_command(config):
    if os.name == "nt":
        exe = ROOT / "bin" / f"{config.capitalize()}-windows-x86_64" / "Editor" / "Editor.exe"
        return [str(exe)], ROOT / "Editor"
    return [str(ROOT / "scripts" / "Linux-Run.sh"), config], ROOT


def run_capture(label_dir, name, scene, options, args, selftest):
    env = dict(os.environ)
    env.update({
        "LUX_GOLDEN_DIR": str(label_dir),
        "LUX_GOLDEN_SCENE": f"Scenes/{scene}.luxscene",
        "LUX_GOLDEN_NAME": name,
        "LUX_GOLDEN_PROJECT": str(args.project.resolve()),
        "LUX_GOLDEN_SIZE": args.size,
        "LUX_GOLDEN_FRAME": str(args.frame),
        "LUX_GOLDEN_PERF_FRAMES": str(args.perf_frames),
        "LUX_GOLDEN_OPTIONS": options,
        "LUX_GOLDEN_SELFTEST": "1" if selftest else "0",
        "LUX_SKIP_BUILD": "1",
    })
    if args.sync_validation:
        env["VK_LAYER_ENABLES"] = "VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT"
        env["VK_KHRONOS_VALIDATION_VALIDATE_SYNC"] = "true"

    status_path = label_dir / f"{name}.status"
    if status_path.exists():
        status_path.unlink()

    command, cwd = editor_command(args.config)
    log_path = label_dir / f"{name}.log"
    started = time.monotonic()
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        try:
            subprocess.run(command, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout, check=False)
            timed_out = False
        except subprocess.TimeoutExpired:
            timed_out = True
    elapsed = time.monotonic() - started

    status = "TIMEOUT" if timed_out else "CRASHED"
    reason = ""
    if status_path.exists():
        lines = status_path.read_text(encoding="utf-8", errors="replace").splitlines()
        status = lines[0] if lines else "CRASHED"
        reason = lines[1] if len(lines) > 1 else ""

    log_text = log_path.read_text(encoding="utf-8", errors="replace")
    validation_lines = len(VALIDATION_LINE.findall(log_text))
    validation_ids = sorted(set(VALIDATION_ID.findall(log_text)))

    result = {
        "name": name,
        "scene": scene,
        "options": options,
        "status": status,
        "reason": reason,
        "seconds": round(elapsed, 1),
        "validationMessages": validation_lines,
        "validationIds": validation_ids,
    }
    perf_path = label_dir / f"{name}.perf.json"
    if perf_path.exists():
        try:
            result["perf"] = json.loads(perf_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            result["perf"] = None

    print(f"  {name:<32} {status:<8} {elapsed:6.1f}s  validation={validation_lines}  {reason}")
    return result


def capture(args):
    label_dir = GOLDEN_ROOT / args.label
    label_dir.mkdir(parents=True, exist_ok=True)

    runs = [(scene, scene, "") for scene in args.scenes]
    if args.features:
        first = args.scenes[0]
        runs += [(f"{first}__{variant}", first, options) for variant, options in FEATURE_VARIANTS.items()]

    print(f"Capturing {len(runs)} run(s) into {label_dir} ({args.config})")
    results = [run_capture(label_dir, name, scene, options, args, selftest=(index == 0)) for index, (name, scene, options) in enumerate(runs)]

    ids = sorted({vid for result in results for vid in result["validationIds"]})
    (label_dir / "validation.txt").write_text("\n".join(ids) + ("\n" if ids else ""), encoding="utf-8")
    summary = {"label": args.label, "config": args.config, "size": args.size, "runs": results}
    (label_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    failed = [result["name"] for result in results if result["status"] != "OK"]
    if failed:
        print(f"{len(failed)} capture(s) did not finish: {', '.join(failed)}")
        return 1
    print(f"All {len(results)} capture(s) finished; {len(ids)} unique validation id(s)")
    return 0


def compare(args):
    dir_a, dir_b = GOLDEN_ROOT / args.compare[0], GOLDEN_ROOT / args.compare[1]
    names = sorted(path.stem for path in dir_a.glob("*.lximg"))
    if not names:
        print(f"No captures in {dir_a}")
        return 1

    diff_dir = GOLDEN_ROOT / f"diff-{args.compare[0]}-vs-{args.compare[1]}"
    diff_dir.mkdir(parents=True, exist_ok=True)

    failures = 0
    for name in names:
        other = dir_b / f"{name}.lximg"
        if not other.exists():
            print(f"  {name:<32} MISSING in {dir_b.name}")
            failures += 1
            continue
        command = [sys.executable, str(COMPARE), str(dir_a / f"{name}.lximg"), str(other),
                   "--max-bad-percent", str(args.max_bad_percent), "--diff", str(diff_dir / f"{name}.png")]
        output = subprocess.run(command, capture_output=True, text=True, check=False)
        print(f"  {name:<32} {output.stdout.strip() or output.stderr.strip()}")
        if output.returncode != 0:
            failures += 1

    ids_a = set((dir_a / "validation.txt").read_text().split()) if (dir_a / "validation.txt").exists() else set()
    ids_b = set((dir_b / "validation.txt").read_text().split()) if (dir_b / "validation.txt").exists() else set()
    new_ids = sorted(ids_b - ids_a)
    if new_ids:
        print(f"New validation ids in {dir_b.name}: {', '.join(new_ids)}")

    passed = failures == 0 and not new_ids
    print(f"{'PASS' if passed else 'FAIL'}: {len(names) - failures}/{len(names)} captures match; diffs in {diff_dir}")
    return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--label", help="capture into bin/golden/<label>")
    parser.add_argument("--compare", nargs=2, metavar=("A", "B"), help="compare two labelled sets")
    parser.add_argument("--config", default="release", choices=["debug", "release", "dist"])
    parser.add_argument("--project", type=Path, default=DEFAULT_PROJECT)
    parser.add_argument("--scenes", type=lambda s: [x for x in s.split(",") if x], default=DEFAULT_SCENES)
    parser.add_argument("--features", action="store_true", help="also capture the first scene once per feature variant")
    parser.add_argument("--size", default="1280x720")
    parser.add_argument("--frame", type=int, default=300)
    parser.add_argument("--perf-frames", type=int, default=300)
    parser.add_argument("--timeout", type=int, default=900, help="seconds per editor launch")
    parser.add_argument("--sync-validation", action="store_true", help="enable synchronization validation (Debug builds)")
    parser.add_argument("--max-bad-percent", type=float, default=0.05)
    args = parser.parse_args()

    if args.compare:
        return compare(args)
    if args.label:
        return capture(args)
    parser.error("pass --label or --compare")


if __name__ == "__main__":
    sys.exit(main())
