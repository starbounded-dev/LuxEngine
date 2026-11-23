#!/usr/bin/env python3
"""GI1 automated checks for the path tracer, through the golden-capture harness (one editor launch each,
no manual interaction): the Cornell box shows colour bleeding, the white-furnace sphere disappears into its
uniform environment, and noise falls as samples grow.

    python tests/rendering/run_path_trace_check.py [--config release] [--keep]

Exits non-zero on the first failed assertion. Captures land in bin/golden/gi-check-*/ and are deleted
afterwards unless --keep (the M: drive is small).
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from golden_compare import Capture  # noqa: E402


def capture(label, scene, options, config, frame):
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


def check(condition, message):
    print(("PASS: " if condition else "FAIL: ") + message)
    if not condition:
        raise SystemExit(1)


def noise(patch):
    """Std of the pixel minus its 5x5 local mean: high-frequency noise, ignoring smooth gradients."""
    from numpy.lib.stride_tricks import sliding_window_view
    gray = patch.mean(axis=2)
    local = sliding_window_view(gray, (5, 5)).mean(axis=(2, 3))
    centre = gray[2:-2, 2:-2]
    return float((centre - local).std())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="release")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    labels = []
    try:
        # 1. Cornell box: red wall on the left, green on the right; their bounce tints the floor and boxes.
        labels.append("gi-check-cornell")
        cornell = capture(labels[-1], "GI_CornellBox", "PathTracer=1,PathTracerSamples=256", args.config, 330)
        h, w = cornell.shape[:2]
        floor_left = cornell[int(h * 0.80):int(h * 0.82), int(w * 0.33):int(w * 0.38)].reshape(-1, 3).mean(axis=0)
        floor_right = cornell[int(h * 0.80):int(h * 0.82), int(w * 0.62):int(w * 0.67)].reshape(-1, 3).mean(axis=0)
        # Tone mapping compresses the tint, so compare the two sides with each other.
        left_tint = floor_left[0] - floor_left[1]
        right_tint = floor_right[0] - floor_right[1]
        check(left_tint > 0.0 and right_tint < 0.0 and left_tint - right_tint > 0.03,
              f"floor is red-tinted near the red wall and green-tinted near the green wall (R-G: left {left_tint:.3f}, right {right_tint:.3f})")

        # 2. White furnace: an albedo-1 sphere in a uniform environment of radiance 1 must vanish into it.
        labels.append("gi-check-furnace")
        furnace = capture(labels[-1], "GI_Furnace", "PathTracer=1,PathTracerFurnace=1,PathTracerSamples=256,PathTracerBounces=8", args.config, 330)
        h, w = furnace.shape[:2]
        centre = furnace[h // 2 - 20:h // 2 + 20, w // 2 - 20:w // 2 + 20].reshape(-1, 3).mean(axis=0)
        corner = furnace[10:40, 10:40].reshape(-1, 3).mean(axis=0)
        ratio = float(centre.mean() / max(corner.mean(), 1e-6))
        # The BRDF is the engine's (Frostbite diffuse + GGX), which is not energy conserving at high roughness, so the
        # sphere is a little darker than a perfect furnace; far darker or brighter would be a bug.
        check(0.80 <= ratio <= 1.05, f"furnace sphere vs environment ratio {ratio:.3f} (centre {centre.round(3)}, env {corner.round(3)})")

        # 3. Convergence: 16x the samples cuts the noise to about a quarter (8-bit output adds a floor, so the bound is loose).
        labels.append("gi-check-n16")
        n16 = capture(labels[-1], "GI_CornellBox", "PathTracer=1,PathTracerSamples=16", args.config, 150)
        labels.append("gi-check-n256")
        n256 = capture(labels[-1], "GI_CornellBox", "PathTracer=1,PathTracerSamples=256", args.config, 330)
        h, w = n16.shape[:2]
        region = (slice(int(h * 0.34), int(h * 0.58)), slice(int(w * 0.42), int(w * 0.58)))
        ratio = noise(n256[region]) / max(noise(n16[region]), 1e-9)
        check(0.1 <= ratio <= 0.6, f"noise at 256 spp / noise at 16 spp = {ratio:.3f} (expected about 0.25)")
        # 4. Diff view: the heat map is not flat and the logged MAE / PSNR are sane.
        labels.append("gi-check-diff")
        diff = capture(labels[-1], "GI_CornellBox", "PathTracer=1,PathTracerDiff=1,PathTracerSamples=64", args.config, 300)
        check(float(diff.std()) > 0.05, f"diff heat map has structure (std {float(diff.std()):.3f})")
        log = (ROOT / "bin/golden" / labels[-1] / "GI_CornellBox.log").read_text(encoding="utf-8", errors="replace")
        match = re.findall(r"diff vs raster: MAE ([0-9.]+), PSNR ([0-9.]+) dB", log)
        check(bool(match), "the diff pass logged MAE / PSNR")
        mae, psnr = float(match[-1][0]), float(match[-1][1])
        check(0.005 < mae < 0.5 and 3.0 < psnr < 60.0, f"diff MAE {mae:.4f}, PSNR {psnr:.2f} dB are in range")
        # 5. Lightmaps: bake the Cornell box, then the raster frame is closer to the path-traced reference with the
        # lightmap than without it ("Diff Lightmap"), and colour bleeds onto the floor.
        sidecar = ROOT / "Editor/LuxSampleProject/Assets/Lightmaps/GI_CornellBox.lxlm"
        sidecar.unlink(missing_ok=True)
        labels.append("gi-check-bake")
        capture(labels[-1], "GI_CornellBox", "BakeLightmaps=128,Lightmaps=1", args.config, 330)
        check(sidecar.exists() and sidecar.stat().st_size > 1_000_000, f"the bake wrote its sidecar ({sidecar.stat().st_size if sidecar.exists() else 0} bytes)")
        maes = {}
        for use in ("0", "1"):
            labels.append(f"gi-check-diff-lm{use}")
            capture(labels[-1], "GI_CornellBox", f"PathTracer=1,PathTracerDiff=1,PathTracerSamples=128,Lightmaps={use}", args.config, 330)
            text = (ROOT / "bin/golden" / labels[-1] / "GI_CornellBox.log").read_text(encoding="utf-8", errors="replace")
            found = re.findall(r"diff vs raster: MAE ([0-9.]+)", text)
            check(bool(found), f"diff log present for Lightmaps={use}")
            maes[use] = float(found[-1])
        check(maes["1"] < maes["0"] * 0.9, f"lightmap lowers the error against the path tracer (MAE {maes['0']:.4f} -> {maes['1']:.4f})")
        sidecar.unlink(missing_ok=True)
        # 6. Realtime GI (probe volume): also closer to the path tracer than the raster frame alone ("Diff Realtime GI").
        gi = {}
        for use in ("0", "1"):
            labels.append(f"gi-check-diff-gi{use}")
            capture(labels[-1], "GI_CornellBox", f"PathTracer=1,PathTracerDiff=1,PathTracerSamples=128,RealtimeGI={use},Lightmaps=0", args.config, 330)
            text = (ROOT / "bin/golden" / labels[-1] / "GI_CornellBox.log").read_text(encoding="utf-8", errors="replace")
            found = re.findall(r"diff vs raster: MAE ([0-9.]+)", text)
            check(bool(found), f"diff log present for RealtimeGI={use}")
            gi[use] = float(found[-1])
        check(gi["1"] < gi["0"] * 0.92, f"realtime GI lowers the error against the path tracer (MAE {gi['0']:.4f} -> {gi['1']:.4f})")
        print("All path tracer checks passed")
    finally:
        if not args.keep:
            for label in labels:
                shutil.rmtree(ROOT / "bin/golden" / label, ignore_errors=True)


if __name__ == "__main__":
    main()
