#!/usr/bin/env python3
"""Per-feature checks for the ray-tracing sampling work (issues #232-#240), through the golden-capture harness.
Each check flips one option, so a feature is verified on its own:

    python tests/rendering/make_rt_scenes.py                       # once: writes GI_PowerLights / GI_GlossyArea
    python tests/rendering/run_rt_sampling_check.py power [mis adaptive restir_di restir_gi denoise ...] [--config release] [--keep]

A feature passes when (1) with the option on, a converged image matches the converged baseline (unbiased features) or stays
within a stated tolerance (biased, opt-in ones), and (2) at equal low sample counts the error against a high-sample
reference is lower. Captures land in bin/golden/rt-check-*/ and are deleted afterwards unless --keep.
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from golden_compare import Capture  # noqa: E402

LABELS = []


def capture(tag, scene, options, config, frame):
    label = f"rt-check-{tag}"
    LABELS.append(label)
    command = [sys.executable, str(ROOT / "tests/rendering/golden_run.py"), "--label", label, "--scenes", scene, "--config", config,
               "--options", options, "--frame", str(frame), "--perf-frames", "10", "--perf-warmup", "5", "--timeout", "600"]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0 or " OK " not in result.stdout:
        print(result.stdout[-2000:], result.stderr[-1000:])
        raise SystemExit(f"capture {label} failed")
    cap = Capture(ROOT / "bin/golden" / label / f"{scene}.lximg")
    values = np.array(cap.values(), dtype=np.float64)
    image = values.reshape(cap.height, cap.width, cap.channels)[..., :3]
    return image / 255.0 if not cap.is_float() else image


def read_log(tag, scene):
    return (ROOT / "bin/golden" / f"rt-check-{tag}" / f"{scene}.log").read_text(encoding="utf-8", errors="replace")


def rmse(a, b):
    return float(np.sqrt(np.mean((a - b) ** 2)))


def check(condition, message):
    print(("PASS: " if condition else "FAIL: ") + message)
    if not condition:
        raise SystemExit(1)


def spp(n):
    """Capture frame by which n samples have accumulated (one sample per frame plus start-up)."""
    return 120 + 2 * n


def check_power(config):
    """#232: power-weighted light choice. Same converged image, lower error at equal low spp (24 lights, 3 decades of power)."""
    scene = "GI_PowerLights"
    base = "PathTracer=1,PathTracerBounces=2"
    ref = capture("power-ref", scene, f"{base},PathTracerSamples=1024,PTPowerLights=1", config, spp(1024))
    uniform_conv = capture("power-uconv", scene, f"{base},PathTracerSamples=1024,PTPowerLights=0", config, spp(1024))
    check(rmse(ref, uniform_conv) < 0.02, f"converged power-sampled image matches the uniform one (RMSE {rmse(ref, uniform_conv):.4f})")
    uniform = capture("power-u8", scene, f"{base},PathTracerSamples=8,PTPowerLights=0", config, spp(8))
    power = capture("power-p8", scene, f"{base},PathTracerSamples=8,PTPowerLights=1", config, spp(8))
    eu, ep = rmse(uniform, ref), rmse(power, ref)
    check(ep < eu * 0.85, f"at 8 spp power sampling has lower error vs the reference (uniform {eu:.4f}, power {ep:.4f})")


CHECKS = {"power": check_power}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("features", nargs="*", default=list(CHECKS))
    parser.add_argument("--config", default="release")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    try:
        for name in args.features:
            print(f"== {name}")
            CHECKS[name](args.config)
    finally:
        if not args.keep:
            for label in LABELS:
                shutil.rmtree(ROOT / "bin/golden" / label, ignore_errors=True)
    print("rt sampling checks passed")


if __name__ == "__main__":
    main()
