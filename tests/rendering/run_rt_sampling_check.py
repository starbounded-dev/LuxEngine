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
import json
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from golden_compare import Capture  # noqa: E402

LABELS = []


def capture(tag, scene, options, config, frame, *, perf_frames=10):
    label = f"rt-check-{tag}"
    LABELS.append(label)
    command = [sys.executable, str(ROOT / "tests/rendering/golden_run.py"), "--label", label, "--scenes", scene, "--config", config,
               "--options", options, "--frame", str(frame), "--perf-frames", str(perf_frames), "--perf-warmup", "5", "--timeout", "600"]
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


def check_ser(config):
    """#254: the ray-tracing pipeline executes, agrees with compute, and remains opt-in."""
    scene = "GI_Lobes"
    options = "PathTracer=1,PathTracerBounces=4,PathTracerSamples=64"
    baseline = capture("ser-reference", scene, options, config, spp(64))
    reordered = capture("ser-on", scene, f"{options},PTSER=1", config, spp(64))
    log = read_log("ser-on", scene)
    if "shader execution reordering is unavailable on this device" in log:
        check(rmse(baseline, reordered) < 1e-6, "SER falls back to the compute image on unsupported hardware")
        print("SKIP: this device does not support hardware reordering")
        return

    ready = "shader execution reordering pipeline ready"
    check(ready in log and ready not in read_log("ser-reference", scene),
          "only the opt-in capture creates the shader execution reordering pipeline")
    error = rmse(baseline, reordered)
    check(error < 0.003, f"the ray pipeline matches compute at equal sample count (RMSE {error:.6f})")
    disabled = capture("ser-off", scene, f"{options},PTSER=0", config, spp(64))
    check(rmse(baseline, disabled) < 1e-6, "explicitly disabling SER preserves the compute image")


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


def check_glass_shadow(config):
    """#252: shadow rays pass through glass. The floor in the shadow of a red-tinted plate is lit red with glass on, and as dark as an
    opaque plate's shadow with glass off."""
    scene = "GI_GlassShadow"
    base = "PathTracer=1,PathTracerSamples=128,PathTracerBounces=2"
    on = capture("gs-on", scene, base, config, 330)
    off = capture("gs-off", scene, f"{base},PTGlass=0", config, 330)
    red = lambda im: (im[..., 0] > 1.8 * im[..., 1]) & (im[..., 0] > 1.8 * im[..., 2]) & (im[..., 0] > 0.05)
    mask = red(on)
    check(int(mask.sum()) > 500, f"the shadow of the red plate is a red region ({int(mask.sum())} red pixels)")
    check(int(red(off).sum()) < 50, f"with glass off the plate casts no coloured shadow ({int(red(off).sum())} red pixels)")
    check(float(on[mask][:, 0].mean()) > float(off[mask][:, 0].mean()) * 1.25, f"the red shadow gets more red light than under an opaque plate (red {float(on[mask][:, 0].mean()):.3f} vs opaque {float(off[mask][:, 0].mean()):.3f})")


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


def check_fog_sun(config):
    """#250: bounded fog extinguishes the directional light before it reaches a clear line of sight."""
    import math

    scene = "GI_FogSun"
    density, radius = 0.55, 0.65
    expected = math.exp(-density * 2 * radius)
    modes = (
        ("path", "PathTracer=1,PathTracerSamples=128,PathTracerBounces=1", spp(128)),
        ("restir", "PathTracer=1,PathTracerSamples=128,PathTracerBounces=1,PTReSTIR=1", spp(128)),
        ("raster", "PathTracer=0,Lightmaps=0,RealtimeGI=0,RTGI=0", 120),
    )
    ratios = {}
    for label, mode, frame in modes:
        options = f"{mode},Bloom=0"
        plain = capture(f"fsun-{label}-off", scene, options, config, frame)
        through = capture(f"fsun-{label}-on", scene, f"{options},PTFog={density}:{radius}:0:0:0:1.5:0", config, frame)
        clear = linear_centre(config, plain)
        check(0.18 < clear < 0.95, f"{label}: the clear sunlit patch is inside the calibrated range ({clear:.3f})")
        ratio = linear_centre(config, through) / clear
        ratios[label] = ratio
        check(abs(ratio - expected) < 0.055,
              f"{label}: bounded fog attenuates sunlight by its chord length (ratio {ratio:.3f}, expected {expected:.3f})")

        out_of_path = capture(f"fsun-{label}-miss", scene, f"{options},PTFog={density}:{radius}:0:0:40:1.5:0", config, frame)
        check(rmse(plain, out_of_path) < 1e-6, f"{label}: a fog sphere outside the sun and camera rays leaves the image unchanged")

    check(max(ratios.values()) - min(ratios.values()) < 0.02, f"all three paths agree on fog transmittance ({ratios})")


def check_fog_gi(config):
    """#250: fog attenuates indirect light. Measure each GI method where the unoccluded camera sees almost no raster direct light."""
    scene = "GI_CornellDark"
    base = "Lightmaps=0,Bloom=0"
    direct = capture("fgi-direct", scene, f"{base},RealtimeGI=0,RTGI=0", config, 400)
    dark = direct.max(axis=-1) < 0.02
    for label, mode in (("probes", "RealtimeGI=1,RTGI=0"), ("rtgi", "RealtimeGI=0,RTGI=1")):
        options = f"{base},{mode}"
        off = capture(f"fgi-{label}-off", scene, options, config, 400)
        on = capture(f"fgi-{label}-on", scene, f"{options},PTFog=0.6:0:0:0", config, 400)
        zero = capture(f"fgi-{label}-zero", scene, f"{options},PTFog=0:0:0:0", config, 400)
        indirectly_lit = dark & (off.mean(axis=-1) > 0.04)
        check(int(indirectly_lit.sum()) > 500, f"the {label} light reaches {int(indirectly_lit.sum())} pixels in direct shadow")
        clear, fogged = float(off[indirectly_lit].mean()), float(on[indirectly_lit].mean())
        check(fogged < clear * 0.8, f"fog darkens the {label} light in the direct shadow ({clear:.4f} -> {fogged:.4f})")
        check(rmse(zero, off) < 1e-4, f"fog density 0 leaves the {label}-lit frame unchanged")


def check_sss(config):
    """#244: subsurface scattering. An albedo-1 random-walk sphere in the white furnace vanishes; a thin slab lit only from behind glows
    on its front face with the walk and stays black without it; a scene without subsurface materials renders identically."""
    furnace = furnace_ratio(capture("ss-furnace", "GI_SSSFurnace", "PathTracer=1,PathTracerFurnace=1,PathTracerSamples=256,PathTracerBounces=96", config, 330))
    check(0.95 < furnace < 1.04, f"a subsurface sphere in the white furnace vanishes (ratio {furnace:.3f})")
    options = "PathTracer=1,PathTracerSamples=256,PathTracerBounces=96"
    lit = capture("ss-lit", "GI_SSSBacklit", options, config, 330)
    dark = capture("ss-dark", "GI_SSSBacklit", f"{options},PTSSS=0", config, 330)
    h, w = lit.shape[:2]
    centre = (slice(h // 2 - 40, h // 2 + 40), slice(w // 2 - 40, w // 2 + 40))
    check(float(lit[centre].mean()) > 0.05 and float(dark[centre].mean()) < 0.02, f"light reaches the front of a thin slab through the material ({float(lit[centre].mean()):.3f}) and not without it ({float(dark[centre].mean()):.3f})")
    plain_off = capture("ss-plain-off", "GI_CornellBox", "PathTracer=1,PathTracerSamples=16,PTSSS=0", config, spp(16))
    plain_on = capture("ss-plain-on", "GI_CornellBox", "PathTracer=1,PathTracerSamples=16,PTSSS=1", config, spp(16))
    check(rmse(plain_on, plain_off) < 1e-6, f"a scene without subsurface materials renders identically (RMSE {rmse(plain_on, plain_off):.7f})")


def check_reflections(config):
    """#245: ray-traced reflections replace SSR (they trace the same surfaces). On a no-sky scene with a polished floor the frame with them
    must be closer to the path tracer than the raster frame with screen-space reflections."""
    scene = "GI_Mirror"
    reference = capture("rr-pt", scene, "PathTracer=1,PathTracerSamples=256,PathTracerBounces=4", config, spp(256))
    raster = capture("rr-raster", scene, "", config, 330)
    traced = capture("rr-on", scene, "RTReflections=1", config, 330)
    h, w = raster.shape[:2]
    floor_band = (slice(int(h * 0.62), int(h * 0.95)), slice(int(w * 0.15), int(w * 0.85)))
    er, et = rmse(raster[floor_band], reference[floor_band]), rmse(traced[floor_band], reference[floor_band])
    check(et < er * 0.8, f"on the polished floor the frame agrees better with the path tracer with ray-traced reflections (RMSE {er:.4f} -> {et:.4f})")
    sphere_top = (slice(int(h * 0.25), int(h * 0.5)), slice(int(w * 0.4), int(w * 0.6)))
    check(rmse(traced[sphere_top], raster[sphere_top]) < 0.01, "pixels that reflect nothing (the sphere itself) are untouched")
    off = capture("rr-off", scene, "RTReflections=0", config, 330)
    check(rmse(off, raster) < 1e-6, f"with the option off the raster frame is unchanged (RMSE {rmse(off, raster):.7f})")


def check_reflection_sky(config):
    """#251: under a bright sky a reflection ray that hits geometry replaces the sky reflection. With the replacement the polished floor
    agrees better with the path tracer than the old add-on; a single ray on the mirror floor matches the default ray count."""
    scene = "GI_MirrorSky"
    reference = capture("rk-pt", scene, "PathTracer=1,PathTracerSamples=256,PathTracerBounces=4", config, spp(256))
    old = capture("rk-old", scene, "RTReflections=1,RTReflEnv=0", config, 330)
    new = capture("rk-new", scene, "RTReflections=1,RTReflEnv=1", config, 330)
    h, w = old.shape[:2]
    band = (slice(int(h * 0.62), int(h * 0.95)), slice(int(w * 0.15), int(w * 0.85)))
    eo, en = rmse(old[band], reference[band]), rmse(new[band], reference[band])
    check(en < eo * 0.9, f"replacing the sky reflection is closer to the path tracer under a bright sky (RMSE {eo:.4f} -> {en:.4f})")
    one = capture("rk-1", scene, "RTReflections=1,RTReflRays=1", config, 330)
    check(rmse(one[band], new[band]) < 0.02, f"one ray on the mirror floor matches the default count (RMSE {rmse(one[band], new[band]):.4f})")
    unfiltered = capture("rk-unfiltered", scene, "RTReflections=1,RTReflDenoise=0", config, 330)
    check(rmse(unfiltered[band], new[band]) < 1e-6, "the rough-reflection denoiser leaves the mirror floor unchanged")


def check_rough_reflection(config):
    """#251: denoise a rough reflection of a small emitter and verify the adaptive budget on the same rough surface."""
    scene = "GI_RoughReflection"
    base = "RTReflections=1,Bloom=0,DOF=0,RTReflRays=4"
    reference = capture("rrrough-ref", scene, "PathTracer=1,PathTracerSamples=512,PathTracerBounces=3,Bloom=0,DOF=0", config, spp(512))
    raw = capture("rrrough-raw", scene, f"{base},RTReflDenoise=0", config, 330)
    filtered = capture("rrrough-filter", scene, f"{base},RTReflDenoise=1", config, 330)
    h, w = raw.shape[:2]
    floor = (slice(int(h * 0.57), int(h * 0.96)), slice(int(w * 0.22), int(w * 0.78)))
    raw_error = rmse(raw[floor], reference[floor])
    filter_error = rmse(filtered[floor], reference[floor])
    check(filter_error < raw_error * 0.85,
          f"filtering the rough reflection reduces error against the path tracer ({raw_error:.4f} -> {filter_error:.4f})")

    def trace_time(tag, options):
        capture(tag, scene, options, config, 330, perf_frames=60)
        perf = json.loads((ROOT / "bin/golden" / f"rt-check-{tag}" / f"{scene}.perf.json").read_text(encoding="utf-8"))
        check(perf["complete"] and perf["passes"]["RTReflectionsPass"]["frames"] >= 50, "reflection GPU timing contains steady-state samples")
        return perf["passes"]["RTReflectionsPass"]["gpuMs"]

    timing = "RTReflections=1,RTReflRays=16,RTReflDenoise=0,Bloom=0,DOF=0"
    fixed = trace_time("rrrough-fixed-budget", f"{timing},RTReflBudget=0")
    adaptive = trace_time("rrrough-adaptive-budget", f"{timing},RTReflBudget=1")
    check(adaptive < fixed * 0.85, f"the roughness budget reduces trace GPU time ({fixed:.3f} -> {adaptive:.3f} ms)")


def check_shadows(config):
    """#246: ray-traced directional shadows replace the cascades. The sun's shadow distance is 3 m, so the cascades end before the box and
    sphere (10 m away) and the raster frame has no shadows there; the ray-traced mask has no range limit and must match the path tracer far
    better. The option off must leave the raster frame identical."""
    scene = "GI_Shadows"
    reference = capture("rs-pt", scene, "PathTracer=1,PathTracerSamples=256,PathTracerBounces=2", config, spp(256))
    raster = capture("rs-raster", scene, "", config, 330)
    traced = capture("rs-on", scene, "RTShadows=1", config, 330)
    h, w = raster.shape[:2]
    lit = (slice(int(h * 0.82), int(h * 0.95)), slice(int(w * 0.3), int(w * 0.7)))
    shadowed = (slice(int(h * 0.38), int(h * 0.66)), slice(int(w * 0.33), int(w * 0.7)))

    def normalised(image):
        """Each renderer's own lit floor is its unit, so the comparison is about the shadows, not the overall light level."""
        return image / max(float(np.median(image[lit])), 1e-6)

    r, t, p = normalised(raster), normalised(traced), normalised(reference)
    er, et = rmse(r[shadowed], p[shadowed]), rmse(t[shadowed], p[shadowed])
    check(et < er * 0.6, f"around the shadows the frame agrees better with the path tracer with ray-traced shadows (RMSE {er:.4f} -> {et:.4f})")
    off = capture("rs-off", scene, "RTShadows=0", config, 330)
    check(rmse(off, raster) < 1e-6, f"with the option off the raster frame is unchanged (RMSE {rmse(off, raster):.7f})")


def check_debug(config):
    """#247: per-pixel debug views. Inside the room (the empty surround has no paths), more allowed bounces or rays turn the heat ramp from
    blue toward green/red (the blue channel falls); the noise view's blue channel rises as samples accumulate; the normal image is
    shown with the view off."""
    scene = "GI_CornellBox"

    def blue(tag, options, frame):
        image = capture(tag, scene, f"PathTracer=1,{options}", config, frame)
        h, w = image.shape[:2]
        return float(image[int(h * 0.3):int(h * 0.7), int(w * 0.34):int(w * 0.66), 2].mean())

    low = blue("db-b1", "PathTracerSamples=16,PathTracerBounces=1,PTDebug=1", spp(16))
    high = blue("db-b8", "PathTracerSamples=16,PathTracerBounces=8,PTDebug=1", spp(16))
    check(low > high * 1.1, f"more allowed bounces move the bounce view up the ramp (blue {low:.3f} -> {high:.3f})")
    low = blue("db-r1", "PathTracerSamples=16,PathTracerBounces=1,PTDebug=2", spp(16))
    high = blue("db-r8", "PathTracerSamples=16,PathTracerBounces=8,PTDebug=2", spp(16))
    check(low > high * 1.05, f"more allowed bounces move the ray view up the ramp (blue {low:.3f} -> {high:.3f})")
    few = blue("db-n16", "PathTracerSamples=16,PathTracerBounces=4,PTDebug=3", spp(16))
    many = blue("db-n256", "PathTracerSamples=256,PathTracerBounces=4,PTDebug=3", spp(256))
    check(many > few * 1.05, f"the noise view cools as samples accumulate (blue {few:.3f} -> {many:.3f})")
    plain = capture("db-off", scene, "PathTracer=1,PathTracerSamples=16,PTDebug=0", config, spp(16))
    check(float(plain.mean()) > 0.2 and float(plain[..., 0].mean()) > float(plain[..., 2].mean()) * 0.8, "with the view off the normal image is shown")


def check_lmdenoise(config):
    """#248: lightmap denoising. A 16-sample bake with the denoiser is closer (as rendered) to a 512-sample bake than the raw 16-sample
    bake, and the denoiser does not darken the result."""
    scene = "GI_CornellDark"
    sidecar = ROOT / "Editor/LuxSampleProject/Assets/Lightmaps/GI_CornellDark.lxlm"
    try:
        reference = capture("ld-ref", scene, "BakeLightmaps=512,Lightmaps=1,LMDenoise=0", config, 900)
        raw = capture("ld-raw", scene, "BakeLightmaps=16,Lightmaps=1,LMDenoise=0", config, 900)
        clean = capture("ld-clean", scene, "BakeLightmaps=16,Lightmaps=1,LMDenoise=1", config, 900)
    finally:
        if sidecar.exists():
            sidecar.unlink()
    er, ec = rmse(raw, reference), rmse(clean, reference)
    check(ec < er * 0.8, f"the denoised 16-sample bake is closer to the 512-sample bake (RMSE {er:.4f} -> {ec:.4f})")
    check(abs(float(clean.mean()) - float(reference.mean())) < 0.02, f"the denoiser keeps the overall light level (mean {float(clean.mean()):.3f} vs {float(reference.mean()):.3f})")


def check_probes(config):
    """#249: probe auto placement. The grid follows the scene size and the requested spacing (logged), and the probe-lit frame is as close
    to the path tracer as the fixed grid's."""
    import re
    scene = "GI_CornellDark"
    base = "PathTracer=1,PathTracerDiff=1,PathTracerSamples=128,RealtimeGI=1,Lightmaps=0"
    fixed = diff_mae("pb-fixed", scene, base, config, 330)
    auto = diff_mae("pb-auto", scene, f"{base},GIProbeSpacing=0.7", config, 330)
    fixed_dims = re.findall(r"probe grid (\d+)x(\d+)x(\d+)", read_log("pb-fixed", scene))
    auto_dims = re.findall(r"probe grid (\d+)x(\d+)x(\d+)", read_log("pb-auto", scene))
    check(bool(fixed_dims) and bool(auto_dims) and fixed_dims[-1] == ("16", "8", "16") and auto_dims[-1] != fixed_dims[-1], f"the grid follows the spacing (fixed {fixed_dims[-1:]}, auto {auto_dims[-1:]})")
    check(auto <= fixed * 1.15, f"auto placement is as close to the path tracer as the fixed grid (MAE {fixed:.4f} -> {auto:.4f})")


def check_adaptive_probes(config):
    """#255: probe placement uses measured variation to improve indirect light within the same budget."""
    import re

    scene = "GI_OverlapUV"
    base = "PathTracer=1,PathTracerDiff=1,PathTracerSamples=128,PathTracerBounces=4,RealtimeGI=1,Lightmaps=0,GIProbeSpacing=1.4"
    uniform = diff_mae("ap-uniform", scene, base, config, 400)
    adaptive = diff_mae("ap-adaptive", scene, f"{base},GIProbeAdaptive=1", config, 400)
    uniform_log = read_log("ap-uniform", scene)
    adaptive_log = read_log("ap-adaptive", scene)
    budget = re.findall(r"probe grid \d+x\d+x\d+ \((\d+) probes\)", uniform_log)
    placed = re.findall(r"adapted (\d+) probes from measured radiance variation", adaptive_log)
    check(bool(budget) and bool(placed) and int(placed[-1]) == int(budget[-1]) and "adapted " not in uniform_log,
          f"adaptive placement redistributes the same probe budget (uniform {budget[-1:]}, adaptive {placed[-1:]})")
    check(adaptive < uniform * 0.99, f"adaptive probes reduce the error against the path tracer (MAE {uniform:.4f} -> {adaptive:.4f})")

    off = capture("ap-off", scene, "RealtimeGI=0,Lightmaps=0", config, 230)
    off_adaptive = capture("ap-off-adaptive", scene, "RealtimeGI=0,Lightmaps=0,GIProbeAdaptive=1", config, 230)
    check(rmse(off, off_adaptive) < 1e-6 and "adapted " not in read_log("ap-off-adaptive", scene),
          "adaptive placement leaves the frame unchanged when realtime GI is off")


def check_unwrap(config):
    """#227: report the xatlas result for a deliberately overlapping mesh without changing its UVs."""
    import re
    scene = "GI_OverlapUV"
    sidecar = ROOT / "Editor/LuxSampleProject/Assets/Lightmaps/GI_OverlapUV.lxlm"
    previous_sidecar = sidecar.read_bytes() if sidecar.exists() else None
    try:
        sidecar.unlink(missing_ok=True)
        capture("un-report", scene, "BakeLightmaps=4,Lightmaps=1,LMUnwrapReport=1", config, 330)
    finally:
        if previous_sidecar is None:
            sidecar.unlink(missing_ok=True)
        else:
            sidecar.write_bytes(previous_sidecar)
    log = read_log("un-report", scene)
    check("overlapping lightmap UVs" in log and "unwrapped submesh" not in log, "reporting leaves the original overlap in place")
    found = re.findall(r"unwrap of submesh \d+: (\d+) -> (\d+) vertices, (\d+) charts, (\d+)% of a (\d+)x(\d+) atlas used, (\d+) triangles", log)
    check(bool(found), "the bake reports an xatlas unwrap for the overlapping room")
    before, after, charts, used, side, _, triangles = (int(v) for v in found[0])
    check(before == 8 and after >= before and charts == 2 and triangles == 4 and used >= 20,
          f"{before} -> {after} vertices, {charts} charts, {used}% of a {side}x{side} atlas, {triangles} triangles")


def check_unwrap_apply(config):
    """#253: applying the unwrap separates the floor and wall lightmaps while preserving their unbaked appearance."""
    import re

    scene = "GI_OverlapUV"
    sidecar = ROOT / "Editor/LuxSampleProject/Assets/Lightmaps/GI_OverlapUV.lxlm"
    previous_sidecar = sidecar.read_bytes() if sidecar.exists() else None
    bake_options = "BakeLightmaps=64,Lightmaps=1,LMDenoise=1,Bloom=0"
    try:
        sidecar.unlink(missing_ok=True)
        reference = capture("ua-pt", scene, "PathTracer=1,PathTracerSamples=256,PathTracerBounces=4,Bloom=0", config, spp(256))
        overlap = capture("ua-off", scene, bake_options, config, 900)
        check(sidecar.is_file(), "the overlapping mesh produced a lightmap")
        sidecar.unlink()
        unwrapped = capture("ua-on", scene, f"{bake_options},AutoUnwrap=1", config, 900)
        check(sidecar.is_file(), "the unwrapped mesh produced a lightmap")
        sidecar.unlink()
        unbaked = capture("ua-unbaked", scene, "Lightmaps=0,Bloom=0", config, 330)
        unbaked_unwrapped = capture("ua-on2", scene, "Lightmaps=0,Bloom=0,AutoUnwrap=1", config, 330)
    finally:
        if previous_sidecar is None:
            sidecar.unlink(missing_ok=True)
        else:
            sidecar.write_bytes(previous_sidecar)

    overlap_warning = r"submesh \d+ of 'LightmapOverlap\.gltf' has overlapping lightmap UVs"
    unwrap_report = r"unwrapped submesh \d+ of .*LightmapOverlap\.gltf.*"
    before_log = read_log("ua-off", scene)
    after_log = read_log("ua-on", scene)
    check(bool(re.search(overlap_warning, before_log)) and not re.search(unwrap_report, before_log),
          "the default import preserves the room's overlapping UVs and the bake reports them")
    first = re.findall(unwrap_report, after_log)
    second = re.findall(unwrap_report, read_log("ua-on2", scene))
    check(bool(first) and not re.search(overlap_warning, after_log), "auto-unwrap separates the overlapping lightmap charts")
    check(first == second, "a fresh import produces the same vertex and chart counts")

    unbaked_error = rmse(unbaked, unbaked_unwrapped)
    check(unbaked_error < 0.003, f"without lightmaps, unwrapping preserves the raster frame (RMSE {unbaked_error:.5f})")

    def surface_means(image):
        height, width = image.shape[:2]
        columns = slice(int(width * 0.49), int(width * 0.61))
        wall = image[int(height * 0.25):int(height * 0.39), columns]
        floor = image[int(height * 0.72):int(height * 0.84), columns]
        return np.array([wall.mean(axis=(0, 1)), floor.mean(axis=(0, 1))])

    expected = surface_means(reference)
    contrast = float((expected[1] - expected[0]).mean())
    check(contrast > 0.025, f"the reference separates the lit floor from the upper wall (mean difference {contrast:.4f})")
    before_error = rmse(surface_means(overlap), expected)
    after_error = rmse(surface_means(unwrapped), expected)
    check(after_error < before_error * 0.85,
          f"unwrapping improves the wall and floor lightmap means against the path tracer (RMSE {before_error:.4f} -> {after_error:.4f})")


def check_texlib(config):
    """#230: texture library. The editor lists Poly Haven and ambientCG textures and downloads one texture from each (needs network):
    the maps arrive as real JPEG files in Assets/Textures/Library. The panel itself is UI and was not clicked through."""
    import re
    library = ROOT / "Editor/LuxSampleProject/Assets/Textures/Library"
    try:
        capture("tl-fetch", "GI_CornellBox", "TexLibFetch=brick_wall_001:Bricks076A", config, 60)
        log = read_log("tl-fetch", "GI_CornellBox")
        listed = re.findall(r"polyhaven list ok: (\d+) textures", log)
        check(bool(listed) and int(listed[0]) > 100, f"Poly Haven lists its textures ({listed})")
        listed = re.findall(r"ambientcg list ok: (\d+) textures", log)
        check(bool(listed) and int(listed[0]) > 10, f"ambientCG lists its textures ({listed})")
        for folder in ("brick_wall_001", "Bricks076A"):
            files = [p for p in (library / folder).glob("*") if p.suffix.lower() in (".jpg", ".png")]
            check(len(files) >= 2, f"{folder}: {len(files)} map files downloaded")
            jpgs = [p for p in files if p.suffix.lower() == ".jpg"]
            check(all(p.read_bytes()[:2] == bytes([0xFF, 0xD8]) and p.stat().st_size > 10000 for p in jpgs), f"{folder}: the JPEG maps are real images")
    finally:
        import shutil
        shutil.rmtree(library, ignore_errors=True)


CHECKS = {"power": check_power, "mis": check_mis, "adaptive": check_adaptive, "restir_di": check_restir_di, "restir_gi": check_restir_gi, "denoise": check_denoise, "compact": check_compact, "tlas": check_tlas, "refit": check_refit, "sort": check_sort, "ser": check_ser, "lobes": check_lobes, "glass": check_glass, "fog": check_fog, "fog_sun": check_fog_sun, "glass_shadow": check_glass_shadow, "fog_gi": check_fog_gi, "sss": check_sss, "reflections": check_reflections, "reflection_sky": check_reflection_sky, "rough_reflection": check_rough_reflection, "shadows": check_shadows, "debug": check_debug, "lmdenoise": check_lmdenoise, "probes": check_probes, "adaptive_probes": check_adaptive_probes, "unwrap": check_unwrap, "unwrap_apply": check_unwrap_apply, "texlib": check_texlib}


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
