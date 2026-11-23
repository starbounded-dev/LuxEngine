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


def check_mis(config):
    """#233: MIS between light and BSDF sampling. Same converged image (no double counting), lower error at equal low spp."""
    scene = "GI_GlossyArea"
    base = "PathTracer=1,PathTracerBounces=2"
    ref = capture("mis-ref", scene, f"{base},PathTracerSamples=1024,PTMIS=0", config, spp(1024))
    mis_conv = capture("mis-conv", scene, f"{base},PathTracerSamples=1024,PTMIS=1", config, spp(1024))
    check(rmse(ref, mis_conv) < 0.02, f"converged MIS image matches the NEE-only one (RMSE {rmse(ref, mis_conv):.4f}; a double count would show here)")
    off = capture("mis-off8", scene, f"{base},PathTracerSamples=8,PTMIS=0", config, spp(8))
    on = capture("mis-on8", scene, f"{base},PathTracerSamples=8,PTMIS=1", config, spp(8))
    eo, em = rmse(off, ref), rmse(on, ref)
    check(em < eo * 0.9, f"at 8 spp MIS has lower error vs the reference (NEE only {eo:.4f}, MIS {em:.4f})")


def check_adaptive(config):
    """#234: adaptive sampling. The sample-count heat map gives the effective samples per pixel; at that equal budget the
    adaptive image must beat plain progressive accumulation, and the saving must be real."""
    scene = "GI_CornellBox"
    base = "PathTracer=1,PathTracerBounces=3"
    ref = capture("ad-ref", scene, f"{base},PathTracerSamples=1024", config, spp(1024))
    adaptive = capture("ad-on", scene, f"{base},PathTracerSamples=256,PTAdaptive=0.04", config, spp(256))
    heat = capture("ad-heat", scene, f"{base},PathTracerSamples=256,PTAdaptive=0.04,PTHeat=1", config, spp(256)).mean()
    calibration = [(n, capture(f"ad-cal{n}", scene, f"{base},PathTracerSamples={n},PTHeat=1", config, spp(n)).mean()) for n in (64, 128, 256)]
    effective = float(np.interp(heat, [0.0] + [level for _, level in calibration], [0.0] + [n for n, _ in calibration]))
    check(effective <= 0.85 * 256, f"adaptive sampling traces fewer samples (about {effective:.0f} of 256 per pixel on average)")
    equal = capture("ad-equal", scene, f"{base},PathTracerSamples={max(int(effective), 4)}", config, spp(max(int(effective), 4)))
    ea, ee = rmse(adaptive, ref), rmse(equal, ref)
    check(ea < ee, f"at the same average sample count adaptive has lower error vs the reference (adaptive {ea:.4f}, uniform {ee:.4f})")


def check_restir_di(config):
    """#236: ReSTIR DI on the 24-light scene. Biased and opt-in, so the converged image only has to stay close to the unbiased
    reference; at a handful of samples the reservoir reuse must beat plain power-sampled next-event estimation."""
    scene = "GI_PowerLights"
    base = "PathTracer=1,PathTracerBounces=2"
    ref = capture("rd-ref", scene, f"{base},PathTracerSamples=1024", config, spp(1024))
    conv = capture("rd-conv", scene, f"{base},PathTracerSamples=256,PTReSTIR=1", config, spp(256))
    check(rmse(conv, ref) < 0.03, f"converged ReSTIR DI stays close to the unbiased reference (RMSE {rmse(conv, ref):.4f})")
    plain = capture("rd-nee4", scene, f"{base},PathTracerSamples=4", config, spp(4))
    restir = capture("rd-restir4", scene, f"{base},PathTracerSamples=4,PTReSTIR=1", config, spp(4))
    en, er = rmse(plain, ref), rmse(restir, ref)
    check(er < en * 0.85, f"at 4 spp ReSTIR DI has lower error vs the reference (plain NEE {en:.4f}, ReSTIR {er:.4f})")


def check_restir_gi(config):
    """#237: ReSTIR GI reuse for RTGI, on a scene whose floor is lit only by a bright wall patch (high-variance indirect light).
    With temporal accumulation off (RTGIHistory=1: what a moving camera sees) reuse must beat plain RTGI against the settled
    image, and the settled reuse image must stay close to the settled plain one."""
    scene = "GI_BounceSpot"
    base = "RTGI=1,Lightmaps=0"
    settled = capture("rg-settled", scene, base, config, 400)
    settled_reuse = capture("rg-settled-r", scene, f"{base},RTGIReuse=1", config, 400)
    check(rmse(settled_reuse, settled) < 0.01, f"settled reuse stays close to plain RTGI (RMSE {rmse(settled_reuse, settled):.4f})")
    plain1 = capture("rg-plain1", scene, f"{base},RTGIHistory=1", config, 400)
    reuse1 = capture("rg-reuse1", scene, f"{base},RTGIHistory=1,RTGIReuse=1", config, 400)
    ep, er = rmse(plain1, settled), rmse(reuse1, settled)
    check(er < ep * 0.8, f"without temporal accumulation reuse has lower error vs the settled image (plain {ep:.4f}, reuse {er:.4f})")


CHECKS = {"power": check_power, "mis": check_mis, "adaptive": check_adaptive, "restir_di": check_restir_di, "restir_gi": check_restir_gi}


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
