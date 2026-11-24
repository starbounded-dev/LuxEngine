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


def check_denoise(config):
    """#238: spatiotemporal RTGI denoiser. After a camera sway (which makes the old filter restart its running mean every frame) the
    reprojected history must leave the image closer to the settled one, and a static camera must settle to the same image."""
    scene = "GI_BounceSpot"
    base = "RTGI=1,Lightmaps=0"
    sway = "CameraSway=0.12:150:190"  # back at the authored pose after frame 190
    ref = capture("dn-ref", scene, base, config, 1000)
    static = capture("dn-static", scene, f"{base},RTGIDenoise=1", config, 400)
    check(rmse(static, ref) < 0.01, f"a static camera settles to the plain RTGI image (RMSE {rmse(static, ref):.4f})")
    old = capture("dn-old", scene, f"{base},{sway}", config, 196)
    new = capture("dn-new", scene, f"{base},RTGIDenoise=1,{sway}", config, 196)
    eo, en = rmse(old, ref), rmse(new, ref)
    check(en < eo * 0.9, f"6 frames after the camera stops the denoiser is closer to the settled image (old filter {eo:.4f}, denoiser {en:.4f})")


def rt_lines(tag, scene):
    return [line for line in read_log(tag, scene).splitlines() if "[RT]" in line]


def check_compact(config):
    """#240: BLAS compaction. The static BLASes of Sponza shrink by a good margin and the image does not change."""
    import re
    scene = "AudioSponza"
    base = "PathTracer=1,PathTracerSamples=8"
    on = capture("cp-on", scene, f"{base},RTCompact=1", config, 600)
    off = capture("cp-off", scene, f"{base},RTCompact=0", config, 600)
    lines = [line for line in rt_lines("cp-on", scene) if "compacted" in line]
    check(bool(lines) and not [line for line in rt_lines("cp-off", scene) if "compacted" in line], "compaction runs with the option on and not with it off")
    match = re.search(r"([\d.]+) MiB -> ([\d.]+) MiB", lines[0])
    before, after = float(match.group(1)), float(match.group(2))
    check(after < before * 0.7, f"BLAS memory shrinks ({before:.2f} MiB -> {after:.2f} MiB)")
    check(rmse(on, off) < 0.005, f"the compacted scene renders the same (RMSE {rmse(on, off):.4f})")


def check_tlas(config):
    """#240: TLAS refit on transform-only changes (entities moving). Same image as rebuilding the TLAS every frame."""
    scene = "GI_CornellBox"
    base = "PathTracer=1,PathTracerSamples=1,MoveEntities=2"
    on = capture("tl-on", scene, f"{base},RTTLASUpdate=1", config, 300)
    off = capture("tl-off", scene, f"{base},RTTLASUpdate=0", config, 300)
    check(any("TLAS is refit" in line for line in rt_lines("tl-on", scene)), "the TLAS is refit in place for moving instances")
    check(not any("TLAS is refit" in line for line in rt_lines("tl-off", scene)), "with the option off the TLAS is rebuilt")
    check(rmse(on, off) < 0.005, f"the refit TLAS renders the same as a rebuilt one (RMSE {rmse(on, off):.4f})")


def check_refit(config):
    """#239: skinned BLAS refit. A pulsing bone changes the pose every frame; refit and rebuild must render the same."""
    scene = "RigPosed"
    base = "PathTracer=1,PathTracerSamples=1,PosePulse=40"
    on = capture("rf-on", scene, f"{base},RTRefit=1", config, 300)
    off = capture("rf-off", scene, f"{base},RTRefit=0", config, 300)
    check(any("refit in place" in line and "skinned" in line for line in rt_lines("rf-on", scene)), "a new pose refits the skinned BLAS")
    check(not any("skinned BLASes are refit" in line for line in rt_lines("rf-off", scene)), "with the option off every BLAS is rebuilt")
    check(rmse(on, off) < 0.005, f"the refit BLAS renders the same as a rebuilt one (RMSE {rmse(on, off):.4f})")


def check_sort(config):
    """#235: software ray sorting. Equivalence is the pass condition; the GPU time is a measurement, printed and recorded in the
    issue. On the RTX 4070 Ti it is ~30% SLOWER than the unsorted tracer (Sponza 2.9 -> 3.8 ms), so PathTracerRaySort stays off."""
    import json
    scene = "AudioSponza"
    base = "PathTracer=1,PathTracerSamples=100000,PathTracerBounces=8"
    off = capture("so-off", scene, base, config, 600)
    on = capture("so-on", scene, f"{base},PTSort=1", config, 600)
    check(rmse(on, off) < 0.001, f"the sorted tracer renders the same image (RMSE {rmse(on, off):.5f})")
    times = []
    for tag in ("so-off", "so-on"):
        perf = json.loads((ROOT / "bin/golden" / f"rt-check-{tag}" / f"{scene}.perf.json").read_text())
        times.append(perf["gpuStats"]["median"])
    print(f"MEASURED: GPU median {times[0]:.2f} ms unsorted, {times[1]:.2f} ms sorted ({times[1] / times[0]:.2f}x)")


def diff_mae(tag, scene, options, config, frame):
    import re
    capture(tag, scene, options, config, frame)
    found = re.findall(r"diff vs raster: MAE ([0-9.]+)", read_log(tag, scene))
    check(bool(found), f"{tag}: the diff pass logged its MAE")
    return float(found[-1])


def check_lobes(config):
    """#241: clearcoat, sheen and anisotropy in the path tracer. On a no-sky scene with point lights the raster frame already has the
    lobes; the path tracer must agree with it better when it has them too, and a coated furnace sphere must not gain energy."""
    scene = "GI_Lobes"
    base = "PathTracer=1,PathTracerSamples=128,PathTracerBounces=2"
    raster = capture("lb-raster", scene, "", config, 330)
    off = capture("lb-off", scene, f"{base},PTLobes=0", config, 330)
    on = capture("lb-on", scene, f"{base},PTLobes=1", config, 330)
    h, w = raster.shape[:2]
    def band(x0, x1):
        return (slice(int(h * 0.33), int(h * 0.65)), slice(int(w * x0), int(w * x1)))
    for name, (x0, x1) in {"cloth (sheen)": (0.45, 0.55), "brushed (anisotropy)": (0.6, 0.75)}.items():
        region = band(x0, x1)
        eo, en = rmse(off[region], raster[region]), rmse(on[region], raster[region])
        check(en < eo * 0.85, f"{name}: the path tracer agrees better with the raster frame with the lobe (RMSE {eo:.4f} -> {en:.4f})")
    region = band(0.2, 0.8)
    check(rmse(on[region], raster[region]) <= rmse(off[region], raster[region]), "no lobe makes the sphere band worse overall")
    plain_off = capture("lb-plain-off", "GI_CornellBox", "PathTracer=1,PathTracerSamples=16,PTLobes=0", config, spp(16))
    plain_on = capture("lb-plain-on", "GI_CornellBox", "PathTracer=1,PathTracerSamples=16,PTLobes=1", config, spp(16))
    check(rmse(plain_on, plain_off) < 1e-6, f"a scene without lobed materials renders identically (RMSE {rmse(plain_on, plain_off):.7f})")
    furnace = capture("lb-furnace", "GI_FurnaceCoat", "PathTracer=1,PathTracerFurnace=1,PathTracerSamples=256,PathTracerBounces=8", config, 330)
    h, w = furnace.shape[:2]
    centre = furnace[h // 2 - 20:h // 2 + 20, w // 2 - 20:w // 2 + 20].reshape(-1, 3).mean(axis=0)
    corner = furnace[10:40, 10:40].reshape(-1, 3).mean(axis=0)
    ratio = float(centre.mean() / corner.mean())
    check(0.7 < ratio < 1.02, f"a clearcoated furnace sphere does not exceed the environment (ratio {ratio:.3f})")


def furnace_ratio(image):
    h, w = image.shape[:2]
    centre = image[h // 2 - 20:h // 2 + 20, w // 2 - 20:w // 2 + 20].reshape(-1, 3).mean(axis=0)
    corner = image[10:40, 10:40].reshape(-1, 3).mean(axis=0)
    return float(centre.mean() / corner.mean())


_TONE = []


def tone_curve(config):
    """The captures are tone mapped. Measure the display value of a uniform furnace of known radiance, once, so a measured pixel can be
    turned back into linear radiance: returns (display values ascending, radiances)."""
    if not _TONE:
        pairs = []
        for level in (1.0, 0.75, 0.5, 0.35, 0.25, 0.15, 0.08):
            image = capture(f"tone-{level}", "GI_FogEmpty", f"PathTracer=1,PathTracerFurnace=1,PathTracerSamples=8,PTFurnaceL={level}", config, spp(8))
            h, w = image.shape[:2]
            pairs.append((float(image[h // 2 - 20:h // 2 + 20, w // 2 - 20:w // 2 + 20].mean()), level))
        pairs.sort()
        _TONE.extend(pairs)
    return _TONE


def linear_radiance(config, display):
    table = tone_curve(config)
    return float(np.interp(display, [d for d, _ in table], [r for _, r in table]))


def linear_centre(config, image):
    """Linear radiance of the image centre (the furnace environment is radiance 1), through the measured tone curve."""
    h, w = image.shape[:2]
    return linear_radiance(config, float(image[h // 2 - 20:h // 2 + 20, w // 2 - 20:w // 2 + 20].mean()))


def check_glass(config):
    """#242: glass. A no-absorption dielectric sphere in a white furnace vanishes (more completely than the 0.87 diffuse sphere), an
    absorbing slab with an invisible interface transmits exp(-sigma d), and glass off makes the sphere an opaque grey ball."""
    base = "PathTracer=1,PathTracerFurnace=1,PathTracerSamples=256,PathTracerBounces=24"
    ratio = furnace_ratio(capture("gl-furnace", "GI_GlassFurnace", base, config, 330))
    check(0.97 < ratio < 1.03, f"a glass sphere in the white furnace vanishes (ratio {ratio:.3f})")
    slab = furnace_ratio(capture("gl-slab", "GI_GlassSlab", base, config, 330))
    slab_image = capture("gl-slab-lin", "GI_GlassSlab", base, config, 330)
    slab = linear_centre(config, slab_image)
    linear_colour = ((0.5 + 0.055) / 1.055) ** 2.4  # the material's attenuation colour 0.5 is sRGB; the row holds it linear
    expected = linear_colour ** 0.5  # attenuation distance 1 m, slab thickness 0.5 m
    check(abs(slab - expected) < 0.03, f"an absorbing slab transmits exp(-sigma d) in linear radiance ({slab:.3f}, expected {expected:.3f})")
    solid = furnace_ratio(capture("gl-slab-off", "GI_GlassSlab", f"{base},PTGlass=0", config, 330))
    check(solid > 0.9, f"with glass off the slab is an opaque white block that absorbs nothing (ratio {solid:.3f})")
    plain_off = capture("gl-plain-off", "GI_CornellBox", "PathTracer=1,PathTracerSamples=16,PTGlass=0", config, spp(16))
    plain_on = capture("gl-plain-on", "GI_CornellBox", "PathTracer=1,PathTracerSamples=16,PTGlass=1", config, spp(16))
    check(rmse(plain_on, plain_off) < 1e-6, f"a scene without glass renders identically (RMSE {rmse(plain_on, plain_off):.7f})")


def check_fog(config):
    """#243: volumetrics, analytically. A purely absorbing fog sphere transmits exp(-sigma * chord); an albedo-1 scattering fog in a
    uniform environment changes nothing (white furnace); no fog renders identically."""
    import math
    scene = "GI_FogEmpty"
    base = "PathTracer=1,PathTracerFurnace=1,PathTracerSamples=256,PathTracerBounces=32"
    sigma, radius = 0.25, 2.0
    absorbing = capture("fg-abs", scene, f"{base},PTFog={sigma}:{radius}:0:0", config, 330)
    ratio = linear_centre(config, absorbing)
    expected = math.exp(-sigma * 2.0 * radius)
    check(abs(ratio - expected) < 0.03, f"a purely absorbing fog sphere transmits exp(-sigma d) (ratio {ratio:.3f}, expected {expected:.3f})")
    scattering = linear_centre(config, capture("fg-scat", scene, f"{base},PTFog=0.5:{radius}:0.4:1", config, 330))
    check(abs(scattering - 1.0) < 0.03, f"an albedo-1 fog in a uniform environment vanishes (ratio {scattering:.3f})")
    glow = "PathTracer=1,PathTracerSamples=128,PathTracerBounces=4"
    lit = capture("fg-light", "GI_FogLight", f"{glow},PTFog=0.4:2:0:1", config, 330)
    dark = capture("fg-dark", "GI_FogLight", glow, config, 330)
    h, w = lit.shape[:2]
    centre = float(lit[h // 2 - 20:h // 2 + 20, w // 2 - 20:w // 2 + 20].mean())
    rim = float(lit[h // 2 - 20:h // 2 + 20, int(w * 0.5 + h * 0.18):int(w * 0.5 + h * 0.18) + 40].mean())
    check(centre > 0.1 and float(dark.mean()) < 0.02, f"a lamp inside fog glows (centre {centre:.3f}) where the same scene without fog is black ({float(dark.mean()):.3f})")
    check(centre > rim * 1.5, f"the glow falls off away from the lamp (centre {centre:.3f}, off to the side {rim:.3f})")
    plain_off = capture("fg-plain", "GI_CornellBox", "PathTracer=1,PathTracerSamples=16", config, spp(16))
    plain_zero = capture("fg-zero", "GI_CornellBox", "PathTracer=1,PathTracerSamples=16,PTFog=0:0:0:1", config, spp(16))
    check(rmse(plain_zero, plain_off) < 1e-6, f"fog density 0 renders identically (RMSE {rmse(plain_zero, plain_off):.7f})")


CHECKS = {"power": check_power, "mis": check_mis, "adaptive": check_adaptive, "restir_di": check_restir_di, "restir_gi": check_restir_gi, "denoise": check_denoise, "compact": check_compact, "tlas": check_tlas, "refit": check_refit, "sort": check_sort, "lobes": check_lobes, "glass": check_glass, "fog": check_fog}


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
