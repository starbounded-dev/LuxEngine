# VR support for LuxEngine — implementation guide

This document is written for a coding agent working in the LuxEngine repository.
It describes the target hardware, the data it sends, and the work needed to make
LuxEngine render to it. Read the whole file before writing code.

---

## 0. Before you start

1. **Explore the codebase first.** Find and note:
   - the language, build system and graphics API (OpenGL / Vulkan / DirectX…)
   - the main loop / frame timing
   - the window + swapchain creation
   - the camera class (how view and projection matrices are built)
   - the renderer entry point (where a frame is drawn), and whether it can render to an offscreen texture
   - the math library (vector / matrix / quaternion types, handedness)
2. **Do not break normal (non-VR) mode.** VR is opt-in: a config flag or command-line switch (e.g. `--vr`).
3. **Work in the phases below, in order.** Each phase ends with something visible and testable.
   Stop after each phase and let the user test it before moving on.
4. If something in this document does not match what you find in the code (for example the
   engine is left-handed), adapt this document to the engine, not the engine to this document.
   Tell the user about the mismatch.

---

## 1. The hardware: "LUX OPTIC" headset

A DIY, PC-tethered headset. Rotation-only tracking (3DoF: no positional tracking).

| Part | Details |
|---|---|
| Displays | 2 × Sharp LS029B3SX02, 2.9", **1440×1440 each, 120 Hz**, one per eye |
| Display driver board | Wisecoco kit, DisplayPort input. To Windows it should appear as **one extra monitor**, most likely **2880×1440 @ 120 Hz**, left eye = left half, right eye = right half. **Not yet received: verify the real resolution, orientation and layout when it arrives** and keep all of these configurable. |
| Lenses | Simple magnifier lenses, ~45–50 mm focal length (exact model not chosen yet) → pincushion distortion that must be corrected in software |
| IPD | User's IPD is **62 mm** |
| Tracker | ESP32-S3 + MPU-6500 IMU, USB HID, custom firmware ("LUX OPTIC tracker"), ~1000 updates/s |
| PC | Windows, RTX 4070 Ti |

The headset also works with SteamVR through the Relativty driver, but **LuxEngine must not depend
on SteamVR**. The goal is a native path: LuxEngine reads the tracker itself and draws directly to
the headset monitor.

---

## 2. Tracker protocol

| Field | Value |
|---|---|
| USB VID / PID | `0x303A` / `0x1001` (Espressif defaults) |
| Interface | USB HID, vendor-defined usage page, input report ID `1`, 63 bytes of payload |
| Rate | ~1000 reports per second |
| Report layout | `byte 0` = report ID (`1`) · `bytes 1–4` = `w` · `5–8` = `x` · `9–12` = `y` · `13–16` = `z` · rest unused |
| Number format | 32-bit IEEE float, little-endian |
| Meaning | Unit quaternion = head rotation relative to the world |
| Axes | SteamVR / OpenGL convention: **+X right, +Y up, −Z forward, right-handed** |
| Recenter | The firmware has a recenter button (yaw only). The engine should also offer its own recenter (key press). |

A ready-made reader exists: **`LuxTracker.h`** (single header, C++17, uses
[hidapi](https://github.com/libusb/hidapi)). It runs a background thread, auto-reconnects, and
exposes `start()`, `stop()`, `get()` (latest quaternion, recentered), `recenter()`, `connected()`.
`tracker_test.cpp` is a console test. If LuxEngine is not C++, port `LuxTracker.h` to the engine's
language using the protocol table above.

Notes:
- Windows allows several programs to read the same HID device, so SteamVR may be running at the
  same time. If opening fails, ask the user to close SteamVR.
- When the tracker is missing, VR mode must still run: fall back to **mouse-look** for the head
  rotation so everything can be developed without hardware.

---

## 3. Architecture

Add a small VR module with a clear boundary. Suggested shape (adapt names to the engine's style):

```
VRSystem
 ├─ IHeadTracker          get head rotation (quaternion), recenter()
 │    ├─ LuxTrackerSource   USB tracker (LuxTracker.h)
 │    └─ MouseLookSource    fallback / development
 ├─ VRConfig              loaded from lux_optic.json (see §4), hot-reloadable
 ├─ StereoRig             builds per-eye view + projection matrices
 ├─ EyeTargets            2 offscreen color(+depth) render targets
 ├─ DistortionPass        full-screen pass: eye textures -> lens-corrected output
 └─ HeadsetOutput         window on the headset monitor (or preview window), vsync, presentation
```

Per frame (VR mode):
1. Poll input as usual.
2. **As late as possible before rendering**, read the head rotation (optionally predict it, §6).
3. For each eye: compute view + projection, render the scene into that eye's target.
4. Run the distortion pass, writing both eyes side by side into the headset window's back buffer.
5. Present with vsync.

The scene is rendered **twice** with the normal renderer. Do not duplicate the renderer; make it
accept a camera (view + projection) and a target.

---

## 4. Config file: `lux_optic.json`

Everything below must be configurable, because the screens and lenses have not been measured yet.

```json
{
  "ipd_m": 0.062,
  "fov_deg": 90.0,
  "eye_resolution": [1440, 1440],
  "render_scale": 1.4,
  "display": { "match_resolution": [2880, 1440], "index": -1, "refresh_hz": 120 },
  "layout": "side_by_side",
  "eye_rotation_deg": 0,
  "eye_gap_offset_px": 0,
  "distortion": { "k1": 0.22, "k2": 0.24, "zoom": 1.0 },
  "chromatic": { "enabled": false, "red_scale": 0.996, "blue_scale": 1.014 },
  "prediction_ms": 0.0,
  "near_m": 0.05, "far_m": 1000.0
}
```

- `display.index = -1` means "find the monitor whose resolution equals `match_resolution`".
- `eye_rotation_deg` handles panels mounted rotated (0 / 90 / 180 / 270).
- `eye_gap_offset_px` shifts each eye's image horizontally (positive = towards the outside) to line
  the image centre up with the lens centre.
- The distortion numbers are placeholders. They are tuned with the calibration screen (§5.4).
- Reload the file when it changes on disk, or on a key press, so tuning does not need a restart.

---

## 5. Implementation phases

### Phase 1: head-look on the monitor
- Add `IHeadTracker`, `LuxTrackerSource`, `MouseLookSource`.
- In VR mode the main camera's rotation = head rotation. Keep player yaw (if the game has it) as a
  separate rotation applied before the head rotation: `world = playerYaw * head`.
- Recenter key: **R** (or whatever the engine's input system allows).
- Debug overlay: tracker connected yes/no, yaw/pitch/roll.

**Done when:** turning the physical sensor turns the in-game view the same way on all three axes,
and unplugging/replugging the tracker recovers by itself.

Axis check: turn right → view turns right; look up → view looks up; tilt head right → horizon tilts
left on screen. If one axis is wrong, fix the conversion in one place only (the tracker source).

### Phase 2: stereo rendering, preview on the monitor
- Two offscreen targets of size `eye_resolution * render_scale` (round to integers).
- Eye poses: `headPose` = rotation only (position = player/camera position).
  - left eye  = headPose × translate(−ipd/2, 0, 0)
  - right eye = headPose × translate(+ipd/2, 0, 0)
  - view matrix = inverse(eye pose)
- Projection: symmetric perspective, vertical and horizontal FOV = `fov_deg` (square panels),
  aspect 1.0, `near_m` / `far_m`. Each eye has its own panel centred on its lens, so no asymmetric
  frustum is needed. If the panels turn out not to be centred on the lenses, use
  `eye_gap_offset_px` first; only switch to asymmetric frustums if that is not enough.
- Composite both eyes side by side into a normal desktop window (no distortion yet).

**Done when:** a desktop window shows two slightly different views side by side; closing one eye
at a time on objects near the camera shows the parallax.

### Phase 3: lens distortion pass
A full-screen shader that, for each output pixel of an eye's half, samples the eye texture at a
**barrel-distorted** position, cancelling the lenses' pincushion distortion.

For each eye half, with `uv` in [0,1]:
```
p      = (uv - 0.5) * 2.0                  // -1..1, centre of the lens = 0
p.x   += eye_gap_offset (in this space, sign depends on eye)
r2     = dot(p, p)
f      = 1.0 + k1 * r2 + k2 * r2 * r2
src    = p * f / zoom                      // where to read in the eye image
src_uv = src * 0.5 + 0.5
if src_uv outside [0,1] -> black
color  = sample(eyeTexture, src_uv)        // linear filtering
```
- Apply `eye_rotation_deg` here too (rotate `p` before/after as needed).
- Optional chromatic aberration correction: sample red, green, blue with slightly different `src`
  scales (`red_scale`, 1.0, `blue_scale`).
- `render_scale > 1` is needed because the barrel warp magnifies the centre; 1.3–1.5 is typical.

**Done when:** a grid looks straight through the real lenses (needs hardware; until then, verify the
shader visually produces a barrel-warped image with black corners).

### Phase 4: output to the headset
- Enumerate monitors; pick the one matching `display` config. Create a **borderless window exactly
  covering that monitor** (exclusive fullscreen is optional; borderless is easier to debug).
- Present with vsync at the monitor's refresh rate (target **120 Hz**). The GPU is an RTX 4070 Ti;
  two 1440² eyes at `render_scale` 1.4 should fit in 8.3 ms for simple scenes. Show frame time in
  the debug overlay.
- Keep an optional small **mirror window** on the main monitor (same image, downscaled) so the user
  can see what the headset shows.
- If no matching monitor is found, fall back to the preview window from Phase 2 and say so in the log.

**Done when:** launching with `--vr` shows the distorted stereo image on the headset monitor at
120 Hz while the desktop stays usable.

### Phase 5: calibration screen
A built-in screen (key **F2** or similar) that draws, per eye: a fine grid, a centre cross, and
concentric circles, plus on-screen values of `k1`, `k2`, `zoom`, `eye_gap_offset_px`, `fov_deg`.
Keys adjust each value live; a key saves them back to `lux_optic.json`.

The user tunes until grid lines look straight and both eyes' crosses fuse into one.

### Phase 6: polish
- **Latency:** read the pose right before submitting draw calls, not at the start of the frame.
- **Prediction (optional):** estimate angular velocity from the last two quaternions and rotate the
  pose forward by `prediction_ms` (default 0, try 10–20 ms). Clamp it; bad prediction causes overshoot.
- **UI:** screen-space HUDs do not work in VR. Draw HUD elements as world-space quads ~1.5–2 m in
  front of the camera, or attached to the head with lag.
- **Comfort:** never move the camera's rotation without the user's head (no camera shake, no
  forced look-at). Smooth turning should be optional; offer snap turning (e.g. 30° steps).
- **3D media (later):** side-by-side 3D video/photos map directly: left half → left eye texture,
  right half → right eye texture, shown on a large virtual screen quad.

### Phase 7 (later, optional): OpenXR backend
Add an `OpenXR` implementation of the same interfaces so LuxEngine games also run on SteamVR /
commercial headsets. Keep the native path as the default for LUX OPTIC.

---

## 6. Math reference

- Quaternion → rotation matrix: standard formula for the engine's convention. Check handedness:
  the tracker is right-handed, Y up, −Z forward. For a left-handed Y-up +Z-forward engine, convert
  `(w, x, y, z) → (w, −x, −y, z)`.
- Recenter (yaw only, around +Y):
  `yaw = atan2(2(w·y + x·z), 1 − 2(y² + x²))`, `ref = quat(axis = +Y, angle = −yaw)`,
  `head = ref × raw`.
- Prediction: `dq = q_now × conjugate(q_prev)` over `dt`; angular velocity `ω = axis(dq) · angle(dq) / dt`;
  predicted = `quat(axis(ω), |ω| · t_pred) × q_now`.

---

## 7. Deliverables checklist

- [ ] VR module behind a flag; non-VR mode unchanged
- [ ] Tracker source + mouse-look fallback + recenter key
- [ ] Stereo rendering into two eye targets, with IPD from config
- [ ] Distortion shader with config values
- [ ] Headset monitor detection, borderless window, vsync 120 Hz, optional mirror window
- [ ] Calibration screen with live tuning and save
- [ ] `lux_optic.json` with defaults, hot reload
- [ ] Debug overlay: tracker status, angles, frame time
- [ ] Short `docs/VR.md` for the user: how to launch VR mode, keys, how to calibrate
