# LuxEngine Minimal VR Plan

How LuxEngine gets a native path to the **LUX OPTIC** DIY headset (3DoF IMU tracker, two 1440×1440
panels behind magnifier lenses, driven as one extra monitor), without SteamVR. The scope runs from
reading the tracker to a calibrated, lens-corrected stereo image on the headset. It is a planning
document, not a description of what exists. Once phases land, `.claude/docs/Architecture-LuxEngine.md`
(the new § 2.16 VR) is the authority for what is built. Hardware facts and the protocol come from
`docs/VR_SUPPORT.md`; where that guide and the engine disagree, this plan follows the engine and says
so (§ Deviations from VR_SUPPORT.md).

## Goal card

- **Goal:** play a LuxEngine scene on the LUX OPTIC headset natively, with head-tracked 3DoF stereo,
  lens correction, and presentation on the headset monitor at its refresh rate. It works from
  `Lux-Runtime --vr`, with a VR preview in the editor's Game View and a built-in calibration screen.
- **Success criteria**
  1. Turning the tracker turns the view the same way on all three axes. Unplugging and replugging
     recovers by itself. Without a tracker, a mouse fallback turns the head.
  2. Two eye images side by side show parallax. They differ only by the IPD offset, with the same
     objects, transforms and lighting in both.
  3. The output is barrel-distorted with black corners. Setting k1 = k2 = 0 reproduces the undistorted image.
  4. `Lux-Runtime --vr` goes fullscreen on the headset monitor and presents at that monitor's
     refresh rate (target 120 Hz, 8.33 ms). The desktop stays usable.
  5. The calibration screen tunes k1, k2, zoom, gap, FOV and IPD live and saves them to `LuxOptic.yaml`.
  6. The editor Game View's VR toggle shows the same distorted stereo image.
  7. Without `--vr` or the toggle, nothing changes: same frames, same files, same cost.
- **Non-goals:** OpenXR/SteamVR backends, positional tracking, controllers, world-space VR UI, a C#
  VR API, pose prediction / late latching / reprojection, a runtime mirror window, multiview or
  instanced stereo, audio listener following the head.
- **Constraints:** Windows first (the PC is Windows + RTX 4070 Ti); Linux must compile in every phase
  and gets best-effort verification at the end. No new dependency, no new install for game makers. No
  temporal techniques. Existing `.luxproj` / `.luxscene` / exports load unchanged. Must be correct under both
  threading policies and in Dist.
- **User's emphatic requirements** (VR_SUPPORT.md): *"LuxEngine must not depend on SteamVR"*; *"Do
  not break normal (non-VR) mode"*; every config value stays configurable because the display and
  lenses have not been measured; **stop after each phase so the user can test it**.

## Decisions this plan is built on

| Decision | Choice | Consequence |
|---|---|---|
| Where VR runs (user) | `Lux-Runtime --vr` **and** an editor Game View VR preview | The runtime owns the headset window. The editor preview can be dragged onto the headset monitor, but it is paced by the editor's main monitor. |
| How far "minimal" goes (user) | Through the calibration screen | Seven implementation phases after a measurement phase. |
| Config scope (user) | Per machine: `LuxOptic.yaml` in `FileSystem::GetPersistentStoragePath()` | One calibration shared by the editor and every exported game. It is never in a project or export. |
| Tracker I/O | Extend `Lux::HID` with input reads; **not** hidapi | Zero new dependency. Linux needs a udev rule for hidraw read access (documented, degrades to mouse fallback). |
| Stereo method | Two eye `SceneRenderer`s (sequential stereo). The runtime packet and its mirror sync are built **once** and submitted to both, with only the camera swapped | 2× GPU cost, shadow maps included. No multiview rewrite of ~30 passes. Refines a Rendering.md rule (Phase 2). |
| Camera composition | Camera entity position + **yaw only**, then head rotation, then ±IPD/2 along head X | The game's pitch/roll is ignored in VR, so the horizon follows the real head (VR_SUPPORT.md Phase 1: `world = playerYaw * head`). |
| Projection | Symmetric, aspect 1, `FOV` from config, reverse-Z via `Camera::SetPerspectiveProjectionMatrix` | Asymmetric frusta only if `EyeGapOffset` proves insufficient (follow-up). |
| Composite | Own fullscreen pass into an offscreen output image, outside the RenderGraph. The runtime blits it with the existing `TexturePass` | One path for runtime and editor; one extra 2880×1440 copy in the runtime. |
| Post effects in VR | DOF forced off; both eyes forced to `ResolutionScaleMode::Native` | No per-eye focus or resolution mismatch (binocular rivalry). Auto-exposure stays per eye (risk R4). |
| Overlays | Debug and calibration text/grid drawn per eye with `Renderer2D` into each eye image (works in Dist). Runtime ImGui overlays are off in VR | Accessibility captions are not shown in runtime VR yet (follow-up: world-space). |
| Keys | `Ctrl+F5` recenter, `Ctrl+F6` VR stats, `Ctrl+F7` calibration, `Ctrl+F8` reload config; hold `Alt` (runtime) / `Alt`+drag (Game View) for mouse head | Deviates from the guide's `R` / `F2`, which games use and the runtime cannot intercept. |
| Opt-in | `--vr` flag, or `Runtime.VR: true` in `RuntimeSettings.yaml`. Editor toggle not persisted | Normal mode is untouched; no project file gains a key. |
| Pose timing | Read on the main thread right after the scene update, right before the packet is built | About 2–3 frames pose-to-photon under `Multi` (risk R3). Late latching is a follow-up. |

### Deviations from VR_SUPPORT.md

1. **No hidapi.** `LuxTracker.h` needs hidapi; the engine already has `Lux::HID` (SetupAPI + `hid.lib`
   on Windows, hidraw on Linux, no vendored library). The tracker reader is a port of
   `LuxTracker.h`'s logic onto it.
2. **YAML, not JSON.** `LuxOptic.yaml`, PascalCase keys like `RuntimeSettings.yaml`, through `Lux::Yaml`.
3. **No axis conversion.** The engine is right-handed, Y up, −Z forward, the same as the tracker.
4. **Keys** (above), and **`--vr` placement**: `CommandLineParser` treats the raw argument after a
   bare named option as that option's value, so the documented form is `--project=<path> --vr`.
5. **"Make the renderer accept a camera and a target"** becomes one `SceneRenderer` per eye, which is
   how this engine already renders several views (Scene View + Game View).
6. **Mirror window** is replaced by the editor preview for now. A runtime mirror needs a second swapchain (follow-up).
7. **Hot reload** is a key (`Ctrl+F8`) plus the calibration screen's save, rather than a file watcher (follow-up).
8. **Phase order.** The editor preview comes before distortion, so the distortion shader is developed
   with the editor's shader hot reload.

---

## Part 0 — Where we are

Everything below was read in this session. *Working* means it is in daily use; *built, unproven*
means the code exists but has never been used this way.

| Capability | State | Evidence |
|---|---|---|
| Raw HID output | ✅ Working (DualSense / DS4 lights, triggers, rumble) | `Core/Source/Lux/Core/HID.h` — `HID::Enumerate/Open/Write/Close`, `DeviceGroup` |
| Raw HID **input** | ❌ Missing | `WindowsHID.cpp` `Open` uses `GENERIC_WRITE`; `LinuxHID.cpp` `O_WRONLY`; `DeviceInfo` has no input report size. Header says "Main thread only". |
| Named threads | ✅ | `Core/Source/Lux/Core/Thread.h` — `Lux::Thread::Dispatch/SetName/Join` |
| Several `SceneRenderer`s per frame | ✅ Working | Scene View + Game View (`GameViewPanel::InitRenderer`), material preview and thumbnailer |
| Runtime packet with a camera swap | ⚠️ Built, never used that way | `Scene::BuildRenderPacketRuntime` / `SubmitRenderPacket` are public (`Scene.h:212-213`); `packet.Camera` is a `SceneRendererCamera` (`SceneRenderer.h:392`: `Camera`, `ViewMatrix`, `Near`, `Far`, `FOV`); `Overlay2DView/ViewProjection` sit beside it |
| One mirror sync feeding two renderers | ⚠️ Read-verified only | `GPUScene::BeginSync` clears dirty ranges (`GPUScene.cpp:28`). `SceneRenderer::FlushDrawList` (from `EndScene`, caller thread) copies rows and keeps per-renderer `m_PendingGPUSceneRangeUploads` epochs (`SceneRenderer.cpp:6820-6900`). Rendering.md § Feeding the renderer forbids two *syncs* of one mirror per frame. `MaterialScene` / `TextureScene` dirty handling was **not** read. |
| Reverse-Z perspective helper | ✅ | `Camera::SetPerspectiveProjectionMatrix(radFov, w, h, near, far)` builds reversed + unreversed (`Camera.h:28`) |
| Engine axes match the tracker | ✅ Read | `EditorCamera::GetForwardDirection` = rotate(−Z) (`EditorCamera.cpp:343`); `Core/premake5.lua:78` defines only `GLM_FORCE_DEPTH_ZERO_TO_ONE` |
| Runtime final-image blit | ✅ Working | `RuntimeLayer::SubmitFinalImageToSwapChain` + `TexturePass.glsl` (straight sample, no gamma: the final image is display-encoded, the swapchain is 8-bit UNORM) |
| Per-draw push constants from main | ✅ | `Renderer::SubmitFullscreenQuadWithOverrides` copies the buffers into its submit lambda (`Renderer.cpp:1701`) |
| New shader in exports | ⚠️ Trap | Exports pack every library shader (`ShaderPack::CreateFromLibrary`, `EditorLayer.cpp:304`). With a pack loaded, `ShaderLibrary::Load` never compiles from source (`Shader.cpp:64-80`), and `Get` asserts on a missing name. An export made before the shader existed cannot run VR. |
| Runtime resources for a dev loop | ✅ | `Lux-Runtime/premake5.lua:72,79` copy `Editor/Resources` into the runtime bin folder, so Debug/Release compile from source when the export has no `ShaderPack.lsp` |
| Fullscreen on a chosen monitor | ❌ Missing | `Window::Init` uses only `glfwGetPrimaryMonitor()` (`Window.cpp:155,164`); nothing enumerates monitors. The GLFW fork is 3.5 and has `GLFW_AUTO_ICONIFY`. |
| Bare CLI flag | ⚠️ Broken for this use | `CommandLineParser::HaveOpt` is `!GetOpt(name).empty()`, so a trailing bare `--vr` reads false; a bare option swallows the next raw argument. `HaveOpt` has no callers. |
| YAML settings + per-user folder | ✅ | `Lux::Yaml`; `ApplyRuntimeSettings` (`RuntimeApplication.cpp`); `FileSystem::GetPersistentStoragePath()` |
| Dist-safe text in the game image | ✅ | `RuntimeLayer::DrawRuntimeOverlay` draws with `Renderer2D` into `GetExternalCompositeFramebuffer()`; Dist has no ImGui |
| Dynamic resolution per renderer | ⚠️ Conflict | `SceneRendererOptions::ResolutionScaleMode::Dynamic` follows each renderer's own GPU time |
| Temporal techniques | ✅ None ship | `SceneRenderer.h:313`: removed outright |
| Earlier VR / stereo / OpenXR work | ❌ None | `git log` and source grep find nothing |
| Hardware | Tracker ✅ (`vr/tracker_test.cpp` works for the user); display board ❌ not received; lenses not chosen | `docs/VR_SUPPORT.md § 1` |

---

## Part 1 — Goals and non-goals

See the Goal card. Two consequences of the hardware state shape every phase:

- **The display board has not arrived.** Every phase must verify on an ordinary monitor. Phase 5
  (headset output) verifies on any second monitor by pointing `Display.MatchResolution` or
  `Display.Index` at it. The checks that need the real headset are listed separately and do not gate the phase.
- **The lenses are not chosen.** Distortion values are placeholders until Phase 6's calibration
  screen is used through the real lenses.

---

## Part 2 — Design

### New system: `Core/Source/Lux/VR/` (all NEW)

| Unit | Holds |
|---|---|
| `LuxOpticConfig.{h,cpp}` | The config struct with defaults, `Load()` / `Save()` through `Lux::Yaml` at `GetPersistentStoragePath() / "LuxOptic.yaml"`, and clamping of loaded values. Writes a default file only when none exists, and otherwise writes only on an explicit save. |
| `LuxOpticTracker.{h,cpp}` | Port of `LuxTracker.h` onto `Lux::HID`: a named reader thread, auto-reconnect every 500 ms, report parsing, yaw-only recenter, `IsConnected()`, a reports-per-second counter. |
| `StereoRig.{h,cpp}` | Pure math: camera world → yaw-only base; head quaternion; per-eye `SceneRendererCamera` (view, reverse-Z projection, near, far, FOV) and overlay view-projection. |
| `StereoRenderer.{h,cpp}` | Two eye `SceneRenderer`s, two overlay `Renderer2D`s, the distortion `RenderPass` / `Pipeline` / `Material`, the output `Framebuffer`. `Render(scene, head)` builds the runtime packet once, submits it per eye, then distorts into the output image. |
| `VRSession.{h,cpp}` | Glue owned by a host (the runtime layer or the Game View): config, tracker, mouse fallback, keys, stats overlay, calibration state. `OnUpdate(ts, inputFocused)`, `Render(scene)`, `GetOutputImage()`. |
| `Editor/Resources/Shaders/LuxOpticDistortion.glsl` | Fullscreen side-by-side composite (Phase 2), then lens warp, gap, rotation, layout and chromatic correction (Phase 4). Set-0 textures + sampler and push constants only. |

`LuxOptic*` names the headset-specific parts; `Stereo*` / `VRSession` are generic, so a later OpenXR
backend can supply poses, projections and swapchain images to the same `StereoRenderer`.

**Config schema** (`LuxOptic.yaml`, every key optional, missing → default):

```yaml
LuxOptic:
  IPD: 0.062                 # metres
  FOV: 90.0                  # degrees per eye, horizontal = vertical (square panels)
  EyeResolution: [1440, 1440]
  RenderScale: 1.0           # eye render size = round(EyeResolution * RenderScale); Phase 0: 1.4 does not fit 120 Hz. Keep ≈ max(1, Zoom)
  Display: { MatchResolution: [2880, 1440], Index: -1, RefreshRate: 120 }
  Layout: SideBySide         # or TopBottom, until the board's real layout is known
  EyeRotation: 0             # 0 / 90 / 180 / 270
  EyeGapOffset: 0            # pixels, + = outward
  Distortion: { K1: 0.22, K2: 0.24, Zoom: 1.0 }
  Chromatic: { Enabled: false, RedScale: 0.996, BlueScale: 1.014 }
  Near: 0.05
  Far: 1000.0
```

### How it fits the engine

| Concern | Answer |
|---|---|
| **System** | New system **VR** (`Core/Source/Lux/VR/`). It may depend on Scene (packet build/submit), Renderer, Core (`HID`, `Thread`, `Input`), Utilities (`FileSystem`) and Serialization (`Yaml`). It must not depend on `Editor/Source/**`, Physics or ScriptEngine. Nothing in Scene or Renderer may include VR. Hosts are `RuntimeLayer` and `GameViewPanel`. New row in Architecture Part 1's dependency table. |
| **Thread** | Tracker loop: its own `Lux::Thread` "LuxOptic Tracker" under **both** policies (it never touches the engine), `LUX_PROFILE_THREAD`, HID reads only, latest pose under a `std::mutex`. `GetOrientation` / `Recenter`, `VRSession::OnUpdate` and `StereoRenderer::Render` run on the main thread; GPU work goes through `Renderer::Submit` (render thread under `Multi`, inline/`Pump` under `Single`). Distortion parameters are push-constant overrides copied by value (`SubmitFullscreenQuadWithOverrides`). Eye-image bindings change only on create/resize, inside a `Renderer::Submit` lambda (Threading.md: per-frame main-thread `Material::Set` of a changing value races `Prepare`). Monitor selection runs in `Window::Init` (main). Config I/O is a tiny YAML read/write on the main thread, at startup or on an explicit key/button. |
| **Ownership** | Host holds `Ref<VRSession>`. `VRSession` holds `Scope<LuxOpticTracker>` and `Scope<StereoRenderer>`. `StereoRenderer` holds `Ref`s to the eye `SceneRenderer`s, `Renderer2D`s and output `Framebuffer`; their GPU resources free through `Renderer::SubmitResourceFree` (Rendering.md Invariant 3). The tracker thread is joined in `~LuxOpticTracker` before its HID handle closes; the 100 ms read timeout bounds the join. Mid-session teardown (editor toggle off, scene switch) relies on the release queue. No raw pointer to a main-thread object is captured in a submit lambda. Phase 3's exit test toggles repeatedly under validation to prove it. |
| **Renderer data flow** | Through `FrameRenderPacket` only: built once by `BuildRenderPacketRuntime`. Per eye, `packet.Camera`, `Overlay2DView` and `Overlay2DViewProjection` are rewritten and `packet.PostProcess.DOFEnabled` forced off, then `SubmitRenderPacket`. No new packet fields. |
| **Renderer invariants** | No UBO/SSBO, so no global `(set, binding)` entry: textures at set 0 bindings 0–2 plus push constants, like `TexturePass`. Pipeline, pass and material are created once in `StereoRenderer`'s constructor and registered with `Renderer::RegisterShaderDependency`. No RenderGraph change (no `PassDesc` / `TextureDesc` fields, so no structure-hash impact). Zero cost when off: nothing is constructed. The shader is loaded in `Renderer::Init`'s list so `ShaderPack.lsp` carries it. `StereoRenderer` checks `ShaderLibrary::GetShaders().contains(...)` and disables VR with a logged error on an old export instead of hitting `Get`'s assert. |
| **Serialization** | Only `LuxOptic.yaml` (per machine). Optional `Runtime.VR` in `RuntimeSettings.yaml`, default false. No `.luxproj` / `.luxscene` / asset change, no migration. The editor toggle is not saved; `LuxSample.luxproj` must not change (diff it after Phase 3). |
| **Editor** | The Game View gains a VR toggle and a VR settings popup. While VR is on, the Game View's own renderer does not render, so `m_RuntimeRenderScene` is still synced once per frame. The ImGui checks are listed in Phase 3. No undo: the config is machine-local, like Application Settings. |
| **Scripting** | None in this plan. A C# `Lux.VR` API (head pose, recenter, IsActive) is a follow-up. |
| **Runtime and Dist** | Runtime hosts the headset path. In Dist there is no shader compiler, so a re-export is required after Phase 2, and no ImGui, so all VR text is `Renderer2D`. Tracy compiles out; the tracker still names its thread. |
| **Linux** | Phase 1 implements `LinuxHID.cpp` input reads (`O_RDONLY`, `poll`) so every phase compiles. Hidraw needs a udev rule (`TAG+="uaccess"` for 303a:1001); without it the tracker reports "no access" and the mouse fallback works. Fullscreen-on-monitor works on X11 and Wayland through `glfwSetWindowMonitor`. The editor preview cannot be dragged out (no platform viewports on Linux). Linux defaults to `Single`, which is covered above. |
| **Build** | New files → `scripts\Win-GenProjects.bat` (and `./premake5 gmake2` on Linux). No new dependency, no build toggle. |
| **Docs** | Architecture § 2.16 VR (new) + Part 1 dependency row + Part 5 directory map (Phase 1, grown in 2/3/5); § 2.9 HID paragraph (Phase 1); § 2.9 Game View (Phase 3); § 2.2 Window (Phase 5). Threading.md quick-reference row for the tracker thread (Phase 1). Rendering.md § Feeding the renderer refined rule + VR composite note (Phase 2). User guide `docs/Editor/VR.md` + `docs/Editor/Keybindings.md` (Phase 7). |

---

## Part 3 — Phases

Stop after every phase for the user to test. Implement each with `/dev`, review with `/cr`, ship with `/send-pr`.

### Phase 0 — Measure the stereo budget (no code)

**Goal:** know whether two full eye renders fit in 8.33 ms before building them, and pick the default `RenderScale`.

**Changes:** none.

**Verification**
- `python3 tests/rendering/golden_run.py --label vr-budget-1.4 --size 2016x2016 --scenes Benchmark`
  and `--label vr-budget-1.0 --size 1440x1440 --scenes Benchmark` (Release, focused-window caveat
  from Rendering.md § Present mode). That is two editor launches, within the launch budget.
- Read `GPU` and the per-pass totals from `<name>.perf.json`. Two eyes ≈ 2 × GPU + ~0.1 ms composite.
- Confirm the tracker on this PC with the existing `vr/tracker_test.exe` (`LINK`, angles move, Enter recenters).

**Exit criteria:** numbers recorded under Part 4. Default `RenderScale` chosen (1.4 if 2× GPU ≤ ~7 ms,
otherwise lower, or name the project quality tier that fits). The user is told the result.

**Rollback:** nothing to roll back.

**Result (2026-10-18, done):** see Part 4. One eye costs 3.72 ms GPU at 1440² and 5.31 ms at 2016²
on Benchmark at the sample's Cinematic preset. With DOF forced off, stereo comes to ≈ 7.1 ms at
1440² (85 % of 8.33 ms) and ≈ 9.9 ms at 2016², which is over budget. **Default `RenderScale` = 1.0.**
Under the guide's warp the centre is magnified by `Zoom` (`src = p·f/Zoom`, `f ≈ 1` at the
centre), so after calibration set `RenderScale ≈ max(1, Zoom)` and lower GTAO/SSR to half
resolution if that breaks the budget. The tracker check with `tracker_test.exe` is left to the user.

### Phase 1 — Tracker head-look in Lux-Runtime (mono)

**Goal:** `Lux-Runtime --vr` turns the primary camera's view with the tracker (or the mouse
fallback). The image is still mono and full-window.

**Changes**
- `Core/Source/Lux/Core/HID.h` — **NEW** `DeviceInfo::InputReportSize`. **NEW** read API: an
  input-capable open and `Read(buffer, size, timeoutMs)` returning bytes / 0 on timeout / −1 when
  gone, shaped as a small class so Windows can own its `OVERLAPPED` event. The threading comment
  becomes "a handle is used by one thread at a time; enumeration from any thread".
- `Core/Platform/Windows/WindowsHID.cpp` — `GENERIC_READ` + `FILE_FLAG_OVERLAPPED` open, `ReadFile` +
  `WaitForSingleObject(timeout)` + `CancelIoEx`, `HidD_SetNumInputBuffers(handle, 2)` and
  `HidD_FlushQueue` on open (each report is an absolute orientation, so queued reports only add
  staleness), `InputReportByteLength` from `HidP_GetCaps`.
- `Core/Platform/Linux/LinuxHID.cpp` — `O_RDONLY | O_CLOEXEC`, `poll` + `read`, draining to the newest report.
- `Core/Source/Lux/VR/LuxOpticConfig.{h,cpp}`, `LuxOpticTracker.{h,cpp}`, `StereoRig.{h,cpp}`, `VRSession.{h,cpp}` — **NEW** (Part 2).
  Parsing rejects reports shorter than 17 bytes or without ID 1, `memcpy`s four LE floats, rejects
  NaN, and normalizes. VID 0x303A / PID 0x1001 come from config defaults in code, not the YAML.
  In Phase 1 `VRSession::Render` draws mono: build packet → set a center-eye camera → `SubmitRenderPacket`.
- `Core/Source/Lux/Utilities/CommandLineParser.cpp` — `HaveOpt` = "option present" (no callers today).
- `Lux-Runtime/src/RuntimeApplication.cpp` — read `--vr` and `Runtime.VR`; pass to `RuntimeLayer`.
- `Lux-Runtime/src/RuntimeLayer.{h,cpp}` — own `Ref<VRSession>` when VR is on; call
  `VRSession::OnUpdate` / `Render` instead of `OnRenderRuntime`; `Ctrl+F5/F6/F8` in `OnEvent`; VR
  stats overlay (tracker link, reports/s, yaw/pitch/roll, frame/GPU ms) through the existing
  `Renderer2D` path; skip ImGui overlays while VR is on.

**Playbook:** *Add a new thread* (`Lux::Thread`, `LUX_PROFILE_THREAD`, no ECS/asset access, no GPU
work at all). *Add files* → regenerate projects.

**Thread and lifetime:** see Part 2. The tracker is constructed by `VRSession` and stopped in its destructor.

**Verification**
- Build: Debug + Release, Windows; regenerate projects; Linux `make config=debug Lux-Runtime` compiles.
- Run: export the sample project once from the editor. Run
  `bin/Debug-windows-x86_64/Lux-Runtime/Lux-Runtime.exe --project=<export> --vr`.
  - Axis check: turn right → view turns right; look up → view looks up; tilt head right → horizon tilts left.
  - `Ctrl+F5` makes the current direction forward. Unplug → overlay shows "no tracker", view holds; replug → recovers within ~1 s.
  - No tracker: hold `Alt` + move mouse → head turns; release → game input unaffected.
  - Without `--vr`: the runtime looks and logs exactly as before.
- Log: one `LuxOptic` info line (config path, tracker found/not found); no new warnings without `--vr`.
- Numbers (R9): runtime on Benchmark with `Ctrl+F3`, record main-thread CPU and frame ms in Part 4. If CPU > 8.33 ms, `/profile` it before Phase 2.
- Files: first `--vr` launch creates `%APPDATA%\LuxEngine\LuxOptic.yaml` with defaults; later launches never rewrite it.
- Tracy (Release): a "LuxOptic Tracker" thread appears.

**Docs:** Architecture § 2.16 VR (new: units, dependency rule, threading), Part 1 dependency row,
Part 5 map, § 2.9 HID input reads. Threading.md quick-reference row "LuxOptic tracker thread".

**Exit criteria:** success criterion 1 met on the monitor; normal mode unchanged.

**Rollback:** revert the commit. HID read additions are unused by anything else.

### Phase 2 — Side-by-side stereo in Lux-Runtime

**Goal:** the runtime window shows left and right eye images side by side, each a full render from its own eye.

**Changes**
- `Core/Source/Lux/VR/StereoRenderer.{h,cpp}` — **NEW**. Eye renderers: `EnableEditorRenderTargets = false`,
  size = `round(EyeResolution * RenderScale)`, `ApplyProjectSettings`, `ShowGrid = false`, `Final` view,
  then `ResolutionScaleMode = Native` on both. One `BuildRenderPacketRuntime` per frame; per eye
  rewrite camera + overlay matrices, force DOF off, `SubmitRenderPacket`. Output framebuffer:
  display size (`EyeResolution` × 2 for side-by-side), RGBA8. Composite with
  `SubmitFullscreenQuadWithOverrides`.
- `Editor/Resources/Shaders/LuxOpticDistortion.glsl` — **NEW**: side-by-side sample (left half →
  left eye, right half → right eye), push-constant block declared for the later parameters.
- `Core/Source/Lux/Renderer/Renderer.cpp` — `Renderer::Init` loads the shader next to `TexturePass.glsl`.
- `VRSession` — stereo replaces mono; `GetOutputImage()`.
- `RuntimeLayer::SubmitFinalImageToSwapChain` — in VR, `u_Texture` is the session's output image.
- Missing-shader guard (Part 2) → VR disables with "re-export the game: ShaderPack.lsp predates VR".

**Playbook:** *Add a new render pass*, adapted. The pass sits outside the graph like the runtime's
swapchain pass: shader in `Editor/Resources/Shaders/`, pipeline/material created once,
`RegisterShaderDependency`, no transient textures, zero cost when off.

**Thread and lifetime:** all main thread; eye-image `Material::Set` inside a submit lambda on create/resize only.

**Verification**
- Build: Debug + Release; regenerate projects.
- Dev loop: rename the export's `ShaderPack.lsp` so Debug/Release compile shaders from the runtime
  bin folder's `Resources/Shaders`. Restore it by re-exporting for the final check.
- Run: near objects shift between the halves. Close one eye at the monitor: the depth cue reads.
  Shared-sync check on `Physics.luxscene` (or a moving scripted object): falling bodies are in the same
  place in both eyes every frame, with no one-frame lag in the right eye. Both eyes have identical
  textures and materials.
- Log (Debug): no validation errors, no `binding collision`, no `Shader ... failed to load`.
- Numbers: Ctrl+F3 / Ctrl+F6 overlay GPU ms vs Phase 0's 2× estimate (≈ 7.1 ms at scale 1.0 on
  Benchmark), both still and **while turning the head** (mouse fallback) so shadow-cascade refits are
  included; VRAM before/after.
- Re-export, then run Release with the pack: works. With an old export: clean error, normal runtime keeps running.

**Docs:** Rendering.md § Feeding the renderer: one sync may feed several `SceneRenderer`s **when
every one submits it in the same frame** (`StereoRenderer`), and two syncs of one mirror per frame
remain forbidden. Plus a short "VR composite runs outside the graph" note. Architecture § 2.16 updated.

**Exit criteria:** success criterion 2; criterion 7 still holds without `--vr`.

**Rollback:** revert; Phase 1 mono path remains.

### Phase 3 — Editor Game View VR preview

**Goal:** a VR toggle on the Game View renders the same stereo output in the editor, with live config editing.

**Changes**
- `Editor/Source/Panels/GameViewPanel.{h,cpp}` — VR toggle button overlaid on the image. While
  on, the panel holds a `Ref<VRSession>`; `Render()` calls `VRSession::Render` instead of
  `OnRenderRuntime`; `DrawGameImage` shows the output image letterboxed to its aspect. Keys are handled
  only while the Game View is focused. `Alt` + drag over the image drives the mouse head. The tracker
  starts with the toggle and stops when it is turned off or the panel closes.
  A VR settings popup offers `ImGuiEx::Property` sliders for every config value, plus **Save** and **Reload**.
- `VRSession` — `SetScene` for scene switches (mirrors `GameViewPanel::SetSceneContext`).

**Playbook:** none applies (no new panel). If the settings popup grows past a popup, *Add a new editor panel*.

**Thread and lifetime:** main thread. Toggle off drops the session; GPU frees go through the release queue.

**Verification**
- Build: Debug editor.
- ImGui audit (`Conventions.md § ImGui correctness`): every `Begin/End`, `BeginPopup/EndPopup` and
  `Push/Pop` pairs on every path, including the early-outs. Scoped guards die before `EndPopup`.
  Slider labels are unique per popup (`##`). The toggle uses `###` if its label changes.
- Run every UI state: toggle on/off 20× (VRAM returns, no validation errors); Edit → Play → Stop
  with VR on; open another scene with VR on; close and reopen the Game View; settings popup
  open/edit/Save/Reload; on Windows, drag the Game View out to a second monitor.
- Files: `git diff Editor/LuxSampleProject/LuxSample.luxproj` is empty; only Save touches `LuxOptic.yaml`.
- With VR off, the Game View is unchanged.

**Docs:** Architecture § 2.9 Game View paragraph (VR toggle, single-sync rule).

**Exit criteria:** success criterion 6 (undistorted until Phase 4).

**Rollback:** revert; the runtime path is independent.

### Phase 4 — Lens distortion

**Goal:** the composite applies the barrel pre-warp that cancels the lenses' pincushion.

**Changes**
- `LuxOpticDistortion.glsl` — per eye half, VR_SUPPORT.md § 5 Phase 3: `p = (uv − 0.5) · 2`, gap
  offset (sign by eye, in pixels → this space), rotation by `EyeRotation`, `f = 1 + k1·r² + k2·r⁴`,
  `src = p·f / Zoom`, black outside [0,1]. Chromatic: R/G/B at `RedScale` / 1 / `BlueScale`.
  `Layout` picks the split axis. Work in UV only; the fullscreen quad's clip position is passed
  through, so the viewport Y flip (Rendering.md) does not apply.
- `StereoRenderer` — fills the push constants from `LuxOpticConfig` every frame.

**Playbook:** none beyond Phase 2's.

**Thread and lifetime:** unchanged.

**Verification** (develop in the editor preview with shader hot reload)
- K1 = K2 = 0, Zoom = 1, gap 0, rotation 0 → identical to Phase 3's image.
- Defaults → barrel-warped eyes with black corners. Increasing Zoom fills more of each half.
- `EyeRotation` 90 / 180 / 270 rotate each eye image the expected way. A positive gap moves each eye
  image outward. Chromatic on → colour fringes increase toward the edges.
- Runtime: re-export, `--vr`, same image as the editor preview.
- Log: no validation errors, no shader errors (a failed stage falls back to a cached binary,
  Rendering.md § bindless note; check the compile log after each edit).

**Docs:** Architecture § 2.16 (distortion parameters and where they live).

**Exit criteria:** success criterion 3. Through-lens straightness waits for Phase 6 + hardware.

**Rollback:** set K1 = K2 = 0 in the config (no rebuild), or revert the shader.

### Phase 5 — Output to the headset monitor

**Goal:** `--vr` opens the runtime fullscreen on the headset monitor, presenting at its refresh rate, with the desktop still usable.

**Changes**
- `Core/Source/Lux/Core/Window.{h,cpp}` — **NEW** `WindowSpecification` monitor selector (index,
  or match by current video mode width × height) and `AutoIconify` (default true). `Window::Init`
  enumerates `glfwGetMonitors`. On a match it creates the window fullscreen on that monitor with its
  **current** video mode (no mode switch) and its refresh rate, and sets `GLFW_AUTO_ICONIFY` false when asked.
- `Core/Source/Lux/Core/Application.{h,cpp}` — pass-through fields on `ApplicationSpecification`.
- `Lux-Runtime/src/RuntimeApplication.cpp` — with VR on, load `LuxOpticConfig` before the
  `Application` exists (like `ApplyRuntimeSettings`). Fill the monitor selector from `Display`; set
  `VSync = true`, `AutoIconify = false`. If no monitor matches, open a 2:1 window on the primary
  monitor and log the monitors found.
- VR stats overlay adds the display name, mode and refresh rate; warns when the refresh rate differs from `Display.RefreshRate`.

**Playbook:** none applies (no new files; Window is existing platform code with no per-OS split needed: GLFW only).

**Thread and lifetime:** main thread, at window creation.

**Verification** (no headset needed)
- Point `Display.MatchResolution` (or `Index`) at a second monitor. The runtime goes fullscreen there;
  clicking the other monitor does **not** minimize it; keyboard keeps working after clicking back.
- Frame time in the overlay equals 1 / that monitor's refresh rate (FIFO). The editor and desktop stay responsive.
- Wrong match → windowed fallback + log listing monitors.
- Without `--vr`, window creation is unchanged (the selector defaults to "none").
- **With the headset, when it arrives:** the board's real mode, orientation and layout are recorded.
  It runs at 2880×1440@120, 8.33 ms frames, with no judder in the overlay's frame-time graph.

**Docs:** Architecture § 2.2 Window (monitor selection, auto-iconify).

**Exit criteria:** success criterion 4 on a second monitor; the headset run is noted separately.

**Rollback:** revert; VR falls back to the Phase 4 windowed output.

### Phase 6 — Calibration screen

**Goal:** the user tunes the distortion, gap, FOV and IPD while wearing the headset, and saves them.

**Changes**
- `VRSession` — `Ctrl+F7` toggles calibration. In the runtime it pauses the scene and restores the
  previous pause state on exit (the accessibility menu's pattern in `RuntimeLayer`). Per eye, a dark
  quad, fine grid, centre cross and concentric circles are drawn with that eye's `Renderer2D` into the
  eye image, before distortion, plus the values (K1, K2, Zoom, EyeGapOffset, FOV, IPD) near the
  centre where the warp is smallest. `Tab` / `Shift+Tab` select a value; `Up` / `Down` adjust it
  (`Shift` fine, `Ctrl` coarse); `Ctrl+S` saves; `Esc` or `Ctrl+F7` leaves. Keys are captured only while calibrating.
- Editor popup (Phase 3) gains a "Calibration pattern" checkbox driving the same state.

**Playbook:** none applies.

**Thread and lifetime:** main thread; the overlay `Renderer2D`s already exist per eye.

**Verification**
- Runtime (Debug, then **Dist** with a fresh export): grid in both eyes, values change live and are
  visible, `Ctrl+S` writes `LuxOptic.yaml` (diff shows only the changed keys), `Ctrl+F8` reloads it,
  scene pauses and resumes. Dist has no ImGui and still shows everything.
- Editor: checkbox shows the pattern; sliders and keys agree.
- **Through the lenses (hardware):** grid lines straight; both crosses fuse into one.

**Docs:** Architecture § 2.16 (calibration state, keys).

**Exit criteria:** success criterion 5.

**Rollback:** revert; values can still be edited by hand + `Ctrl+F8`.

### Phase 7 — Hardening and user guide

**Goal:** Linux and Dist verified, edge cases handled, and the feature documented for the person wearing the headset.

**Changes**
- Linux: run the runtime with `--vr` on X11 and Wayland. Tracker with and without the udev rule;
  fullscreen on a chosen monitor.
- Edge cases: tracker present at startup but never sends reports (overlay says so); `LuxOptic.yaml`
  with bad values (clamped, logged once); eye size changes on Reload (renderers resize, no leak);
  SteamVR/Relativty running at the same time (shared read works on Windows; if open fails, the log tells the user to close SteamVR).
- `docs/Editor/VR.md` — **NEW** user guide: launching (`--project=… --vr`, `Runtime.VR`), keys,
  every config field, calibration steps, Linux udev rule, coexistence with SteamVR.
  `docs/Editor/Keybindings.md` — VR keys.

**Playbook:** none applies.

**Verification:** `make config=debug Lux-Runtime` + run on Linux under the default `Single` policy;
Windows under `LUX_THREADING_POLICY=Single` and `Multi`; Dist export + run; every edge case above
observed in the log/overlay.

**Docs:** user guide above; recheck every shared doc touched in Phases 1–6 against the final code.

**Exit criteria:** all seven success criteria hold on Windows; Linux runs with the mouse fallback and, with the udev rule, the tracker.

**Rollback:** documentation-only and edge-case fixes; revert individually.

---

## Part 4 — Verification of the whole

- **Normal mode is unchanged (criterion 7):** before Phase 1 and after Phase 7, run
  `golden_run.py --label vr-off-before --scenes Benchmark` / `--label vr-off-after --scenes Benchmark`
  (editor, VR off, one launch each) and `--compare` them. They must pass. Runtime without `--vr` is
  checked by eye and log in each phase.
- **Budget:** Phase 0 numbers vs Phase 2/5 overlay numbers on the same scene and render scale.
  Phase 0 captures: Release editor at HEAD `bb209835`, sample project at QualityPreset Cinematic
  (GTAO and SSR full resolution, 8K shadow limit, DOF on). `bin/golden/vr-budget-1440/` and
  `vr-budget-2016/`, 300 frames each, 0 validation messages.

  | Scene | Eye size | GPU (1 eye, golden) | 1 eye without DOF | Stereo estimate (2× + ~0.1) | GPU (stereo, runtime) | Frame (headset) |
  |---|---|---|---|---|---|---|
  | Benchmark | 2016² (scale 1.4) | 5.31 ms | 4.90 ms | ≈ 9.9 ms ❌ | — | — |
  | Benchmark | 1440² (scale 1.0) | 3.72 ms | 3.50 ms | ≈ 7.1 ms ✅ (85 %) | — | — |

  A linear fit gives **≈ 2.0 ms of resolution-independent cost per eye**, paid twice in stereo
  (G-buffer 0.55 ms is geometry-bound and flat, plus pre-depth, pre-convolution, cluster culling and
  HZB), and ≈ 0.70 ms per megapixel. The largest scaling passes at 1440² are GTAO 0.54, GTAO-Denoise
  0.38, SSR 0.43 and deferred lighting 0.46 ms. Half-resolution GTAO/SSR is the first lever (spatial,
  not temporal). Scale 1.0 leaves ≈ 1.2 ms of headroom; the break-even is ≈ scale 1.19.

  **Not in these numbers** (the capture pins the camera):
  - **Shadow maps read 0 ms** because the directional cache stays valid. It refits when the view
    direction moves past ~1.8° (`directionDotThreshold = 0.9995`) or the camera moves 0.25 m
    (`SceneRenderer.cpp` ~5466). A turning head therefore re-renders cascades every few frames,
    and each eye's renderer does so. Measure in Phase 2 while turning (mouse fallback) — risk R1.
  - **CPU.** The editor's main thread is ~17.6–17.9 ms on Benchmark at both sizes (frame 18–20 ms,
    so the run was main-thread bound and the GPU was mostly idle; its clocks may have been low,
    which would make these GPU figures pessimistic). The runtime has no editor UI, but its main
    thread on Benchmark is unmeasured. Measure in Phase 1 — risk R9.

- **Hardware acceptance** (when the board and lenses exist): criteria 1–6 through the lenses at 120 Hz, after a calibration session.

---

## Part 5 — Risks

| # | Risk | Likelihood | Detection | Mitigation |
|---|---|---|---|---|
| R1 | Two full eye pipelines (shadows included) exceed 8.33 ms at 2016² | High for rich scenes | Phase 0 | Lower `RenderScale`, a lower project quality tier for VR, or the follow-ups: shared shadow maps between eyes, then multiview. |
| R2 | One sync feeding two renderers misbehaves in `MaterialScene` / `TextureScene` (not read) | Low–medium | Phase 2 shared-sync check (identical textures, no right-eye lag) | Give the right eye its own mirror (`Scene` keeps a third `RenderScene`) at the cost of a second sync. |
| R3 | Pose-to-photon latency of ~2–3 frames under `Multi` feels laggy | Medium | Hardware test | `Single` policy for VR as a quick test; follow-ups: late latching the view on the render thread, prediction. |
| R4 | Per-eye auto-exposure or bloom differs between eyes | Low | Phase 2 side-by-side, scenes with bright edges | Drive both eyes' exposure from the left eye (follow-up). |
| R5 | Windows composes the headset window with the 60 Hz desktop → judder | Medium | Phase 5 frame-time graph on the headset | Fullscreen with the current mode usually gets independent flip; else test exclusive mode switch, or make the headset the only high-Hz output. |
| R6 | The board's real layout/orientation differs (portrait, rotated, 2560×1440) | Medium | Hardware arrival | `Layout`, `EyeRotation`, `Display.*` are all config, no code change. |
| R7 | Destroying eye renderers mid-session (editor toggle) leaks or frees in-flight resources | Low | Phase 3 toggle-20× under validation, VRAM stat | Defer destruction a frame, or reuse the session instead of rebuilding it. |
| R8 | HID read on Windows blocks shutdown or returns stale reports | Low | Phase 1 unplug/exit tests | Timeout + `CancelIoEx`; 2-report queue + flush on open. |
| R9 | The runtime's main thread exceeds 8.33 ms on heavy scenes (the editor's is ~17.7 ms on Benchmark) | Medium–high on Benchmark | Phase 1: Ctrl+F3 CPU on Benchmark in the runtime | `/profile` the runtime main thread before Phase 2. VR on lighter scenes first. Stereo adds only one extra `SubmitRenderPacket` + `FlushDrawList` (~0.5 ms), not a second packet build. |

**What would invalidate the plan:** if Phase 0 shows that even 1440² per eye at a low tier cannot fit
in 8.33 ms, sequential stereo is the wrong foundation. Go back to Step 3 and plan shared shadows or
multiview first, or accept a 60 Hz headset mode.

## Part 6 — Open questions

| Question | How it resolves |
|---|---|
| Real display mode, orientation and eye layout of the Wisecoco board | Hardware arrival; Phase 5 records it. |
| Lens model and focal length, so the real K1/K2/FOV | Lens choice + Phase 6 calibration. |
| Whether `MaterialScene` / `TextureScene` dirty consumption is per renderer like `GPUScene` | Read during Phase 2 before writing `StereoRenderer`; R2 has the fallback. |
| Whether the Wisecoco board reports a non-desktop EDID (hidden from Windows' desktop) | Hardware arrival. Extended mode is assumed; direct mode is out of scope. |
| VRAM for two 2016² eye renderers | Phase 2 overlay. |
| Does the firmware's recenter button need an engine event (to reset the engine's own yaw reference too)? | Phase 1 hardware test; the firmware recenters at the source, so the engine reference may simply stay. |

## Follow-ups (not in this plan)

- Runtime mirror window (second swapchain) and a "fill headset monitor" button for the editor preview.
- Late latching / pose prediction (`prediction_ms`). **Rotational reprojection** in the distortion
  pass (warp the finished frame by the newest head rotation) is *not* temporal accumulation (no
  history blend, no ghosting), but it sits next to the no-temporal rule, so ask before planning it.
- Shared shadow maps between eyes; multiview / instanced stereo.
- World-space HUD and VR accessibility captions; snap turning; neck model.
- C# `Lux.VR` API; audio listener following the head rotation.
- File-watch hot reload of `LuxOptic.yaml` (`Core/vendor/filewatch` is already vendored).
- OpenXR backend behind the same `StereoRenderer` (VR_SUPPORT.md Phase 7), optional and auto-detected.

---

## Part 7 — Research notes

**Goal (from the Goal card):** native, calibrated 3DoF stereo on a DIY extended-mode headset.

- **Concept:** viewer distortion is a radial polynomial in distance from the lens centre. Cardboard
  exposes k1/k2 and documents manual tuning until straight lines and right angles hold. Its C API
  also returns **per-eye asymmetric** FOV half-angles, which is why asymmetric frusta stay a follow-up.
  [Cardboard viewer parameters](https://support.google.com/cardboard/manufacturers/answer/6324808),
  [Cardboard lens distortion API](https://developers.google.com/cardboard/reference/c/group/lens-distortion)
- **Prior art:** Monado's OpenHMD driver carries exactly this push-constant set: warp parameters,
  per-channel aberration scales, lens centre, warp scale. Coefficient order differs between its
  panotools and OpenHMD models, so coefficients must not be copied across models.
  [openhmd_values](https://monado.pages.freedesktop.org/monado/structopenhmd__values.html),
  [u_panotools_values](https://lubosz.pages.freedesktop.org/monado/structu__panotools__values.html)
- **Prior art:** Relativty is the open DIY 3DoF headset + SteamVR driver the LUX OPTIC already runs
  on. This plan replaces SteamVR for LuxEngine only. [Relativty](https://github.com/relativty/Relativty)
- **Pitfall:** a GLFW fullscreen window iconifies when it loses focus, so clicking the desktop monitor
  would hide the headset image. Set `GLFW_AUTO_ICONIFY` false. Use the monitor's current video mode
  for a fast, mode-switch-free "windowed fullscreen" (Phase 5).
  [GLFW window guide](https://www.glfw.org/docs/latest/window.html),
  [GLFW forum](https://discourse.glfw.org/t/fullscreen-minimizes-on-clicking-on-another-monitor/1451)
- **Pitfall:** Windows' HID input queue holds 32 reports by default (minimum 2). For a device that
  sends absolute state, a deep queue only adds staleness. Hence `HidD_SetNumInputBuffers(2)` +
  `HidD_FlushQueue` (Phase 1).
  [HidD_SetNumInputBuffers](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidsdi/nf-hidsdi-hidd_setnuminputbuffers)
- **Pitfall:** mixed-refresh desktops judder when DWM composes them together. Since Windows 10 2004,
  DWM composes at the highest refresh rate, with smaller residual stutter. This is why R5 exists and
  why Phase 5 measures pacing on the headset.
  [Blur Busters on 2004 mixed-Hz](https://blurbusters.com/new-windows-update-fixes-mixed-hz-multimonitor/)
- **Concept:** comfort post FX. Avoid DOF, motion blur, chromatic aberration and lens-distortion
  effects in VR, and apply effects consistently to both eyes. Hence DOF is forced off and resolution
  mode is forced equal.
  [Unity URP post-processing in XR](https://docs-alpha.unity3d.com/Manual/urp/integration-with-post-processing.html),
  [Unity glossary: post FX for VR](https://unity.com/glossary/post-fx-for-vr)
- **Concept:** prediction cuts effective latency but errs when motion changes direction. ~60 ms is the
  historical ceiling. So prediction stays at 0 and becomes a follow-up after hardware latency is felt.
  [LaValle, The Latent Power of Prediction](https://developers.meta.com/horizon/blog/the-latent-power-of-prediction/)
- **Rejected:** hidapi (`LuxTracker.h`). `Lux::HID` already covers SetupAPI and hidraw, so a vendored
  library would add size for nothing.
- **Rejected:** OpenXR via Monado/SteamVR as the LUX OPTIC path. It is an extra runtime for game
  makers, and VR_SUPPORT.md forbids depending on SteamVR. It stays an optional later backend.
- **Rejected for now:** multiview / instanced stereo. Every graph pass would need layered targets;
  revisit only if R1 hits.

**Informs decisions:** tracker I/O, composite, post effects in VR, Phase 5 window mode, pose timing.
**Still unknown:** whether DWM gives the headset window independent flip; the board's EDID and
layout; real lens coefficients.
