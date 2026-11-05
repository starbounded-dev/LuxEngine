# LuxEngine — Building & Compiling

How the engine compiles, and which script to re-run when it stops compiling.

The build system is **Premake5**, not CMake. Premake generates a Visual Studio solution on Windows
and GNU makefiles on Linux; a small Python layer (`scripts/`) fetches dependencies, checks the
Vulkan SDK, and drives premake with the right feature flags.

The single biggest cause of "I changed one file and now nothing compiles" is a stale generated
project — premake does **not** auto-regenerate the way CMake reconfigures. Adding, removing, or
renaming a source file, or changing a `premake5.lua`, requires re-running project generation by
hand.

---

## Canonical flow

1. **Bootstrap** (first time, or after a dependency change) — `scripts/Setup.bat`.
   Sets `LUX_DIR`, checks the Vulkan SDK, `git lfs pull`, `git submodule update --init --recursive`,
   creates `Editor/DotNet/`, then generates the solution.
2. **Regenerate only** (day to day, after adding/removing files) — `scripts/Win-GenProjects.bat`.
   Same generation step, skipping the Vulkan/LFS/submodule work.
3. **Build** — Visual Studio (`Lux.sln` / `Lux.slnx`), or `msbuild`, or `scripts/Linux-Build.sh`.
   To debug a frame, `scripts\Win-RenderDoc.bat <config>` (Windows) or `scripts/Linux-RenderDoc.sh <config>` opens RenderDoc with the built editor launched and hooked; `.bat` files check out with CRLF (`.gitattributes`) because cmd labels misbehave with LF.
4. **Output** — `bin/<Config>-<system>-<arch>/<Project>/`, intermediates in `bin-int/…`.
   The `outputdir` pattern is `%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}`, e.g.
   `bin/Debug-windows-x86_64/Editor/Editor.exe`.

Both Python entry points are thin wrappers: `Setup.bat` → `Setup.py`,
`Win-GenProjects.bat` → `Win-GenProjects.py`. Both call `Configure.run(...)`, which shows the
interactive menus, remembers your choices in `scripts/.luxsetup.json`, and invokes
`vendor/bin/premake5.exe`.

---

## Quick start — fresh clone to running editor (Windows)

```cmd
scripts\Setup.bat
```

Pick a generator and any optional features in the menus, then open the generated solution:

- **Visual Studio 2022** → `Lux.sln`
- **Visual Studio 2026** → `Lux.slnx`

Startup project is `Core` per `premake5.lua` (`startproject "Core"`) — switch it to **Editor** to
run the editor. Platform is **x64**.

### Linux

```bash
scripts/Linux-Build.sh          # prompts for a config, or: Linux-Build.sh release
LUX_RUN_AUDIO_TESTS=1 scripts/Linux-Build.sh debug-as  # instrumented build and audio checks
scripts/Linux-Run.sh            # rebuilds if needed, then runs the editor
scripts/Linux-RunRuntime.sh     # standalone runtime player
scripts/Linux-RenderDoc.sh      # the editor inside RenderDoc (LUX_RENDERDOC=1 Linux-Run.sh)
```

`Linux-Build.sh` is one-shot and idempotent: it checks prerequisites (`dotnet make clang pkg-config
curl tar`, plus `gtk+-3.0` for NFD-Extended file dialogs), syncs submodules, locates or downloads a
**pinned, checksum-verified** premake (`5.0.0-beta4`) into `vendor/bin/premake5`, downloads a
**pinned** Vulkan SDK (`1.4.335.0`, for Lux's own Vulkan code and the shader tools: NRI brings its
own headers; override with `VULKAN_VERSION`) into
`Core/vendor/VulkanSDK/x86_64` if the vendored copy is missing, generates makefiles, and builds.
Only the Windows premake binary is committed — any file named `premake5` is gitignored, and the
Vulkan SDK itself is gitignored — so a clean checkout has neither and the script fetches both on
first run. Prefer the script over invoking premake directly.

`VULKAN_SDK` defaults to the vendored `Core/vendor/VulkanSDK/x86_64` if unset; setting it explicitly
to a path that doesn't exist is a hard error rather than triggering a download (the auto-fetch only
kicks in for the vendored default).

`scripts/Linux-Fetch.sh` is a standalone alternative that fetches the same two pieces without
running a build — still useful for pre-warming a cache image, but no longer a required step before
`Linux-Build.sh`.

`Linux-Run.sh` runs `Linux-Build.sh` before launching the editor, so a forgotten recompile can't
result in running a stale binary — `make`'s incremental rebuild makes this cheap when nothing
changed. Set `LUX_SKIP_BUILD=1` to skip that check and launch immediately.

---

## Configurations

`python tests/editor/run_browser_panels.py` runs browser/navigation and asset-picker regressions
under Single/Multi in disposable projects. The non-Dist editor hook is
`LUX_BROWSER_PANELS_SELFTEST=<active disposable project directory>`; it writes a sibling `.status`
report. Re-run project generation when adding the test source to an existing solution.
The browser, workspace and material runners accept `LUX_TEST_EDITOR=<executable>` for testing
a separately linked review executable while the regular editor remains open. Verify that
executable's timestamp; their default remains the ordinary Debug Editor.

Declared in `premake5.lua`: `Debug`, `Debug-AS`, `Release`, `Dist`.

| Config | Optimize | Symbols | Notes |
|---|---|---|---|
| `Debug` | Off | On | `LUX_ENABLE_ASSERTS`, `LUX_TRACK_MEMORY`, Tracy on, Jolt `JPH_FLOATING_POINT_EXCEPTIONS_ENABLED` |
| `Debug-AS` | Off | On | Debug + AddressSanitizer (Windows: `NoRuntimeChecks`, `NoIncrementalLink`, `editandcontinue "Off"`) |
| `Release` | On | Default | `NDEBUG`; asserts compile out, `VERIFY` stays, Tracy on |
| `Dist` | Full | Off | `NDEBUG` + LTO; Tracy and Aftermath compiled out |

Jolt's `JPH_FLOATING_POINT_EXCEPTIONS_ENABLED` (`Core/vendor/JoltPhysics/JoltPhysicsPremake.lua`) traps
FP divide-by-zero/invalid/overflow as a hardware exception on physics worker threads — useful for
catching NaN/Inf bugs while developing, but it turns any degenerate physics state (not necessarily
an engine bug — Jolt's own math is written to tolerate producing Inf/NaN internally) into a hard
crash. Keep it **Debug-only**; do not add it back to `Release` or `Dist`.

Linux `Debug-AS` enables `-fsanitize=address` for native C/C++ compilation and executable
linking, with frame pointers retained. It uses its own `Debug-AS-linux-x86_64` dependency and
engine output directories. The Linux build/run helpers accept `debug-as`; no new Premake option
is needed. Clang requires matching compiler-rt development libraries; Ubuntu CI installs
[libclang-rt-dev](https://packages.ubuntu.com/resolute/libclang-rt-dev). These are developer
build tools, not game runtime prerequisites.

With `LUX_RUN_AUDIO_TESTS=1`, Debug and Debug-AS both run the independent Soundwave checks
and the complete headless audio suite before the engine build. Debug-AS instruments those
test compilations/links too, uses matching Jolt/ImGui archives, and checks the production
`AudioAcoustics.o` for ASan memory-access instrumentation. CI builds Core, Editor and Runtime
in this additional Linux configuration, verifies fresh artifacts, and inspects Core/Runtime
sanitizer symbols. Audio logs have separate per-configuration artifact names. No sanitizer
errors or leak reports are suppressed by this gate.

`LUX_CORE_VERIFY` is enabled in **every** config (`LUX_ENABLE_VERIFY` is unconditional in
`Assert.h`) — it is the assert that survives into Dist.

`Release` and `Dist` target AVX2 (`vectorextensions "AVX2"`, `isaextensions { "BMI", "POPCNT",
"LZCNT", "F16C" }`) in **Core, Editor and Lux-Runtime alike**. Projects linked into one program must
share the target: with LTO, headers shared with Core compile differently on each side, and the
Dist link reports it as `-Wodr` ("violates the C++ One Definition Rule") on `RenderCommandBuffer`
methods. Change the target in all three `premake5.lua` files together.

---

## Optional features

Two kinds of toggle, both driven off the single `OPTIONS` table in `scripts/BuildOptions.py`:

**Premake flags** (forwarded to premake as `--<flag>`; `premake5.lua` declares a matching
`newoption`):

| Flag | Effect |
|---|---|
| `--discord` | Enables the Discord Social SDK integration; defines `LUX_ENABLE_DISCORD`. Requires `Core/vendor/discord_social_sdk/` (fetched manually — `Configure.warn_missing_discord_sdk` warns if absent). |
| `--no-tracy` | Omits `TRACY_ENABLE` / `TRACY_ON_DEMAND` / `TRACY_CALLSTACK`, reducing vendored Tracy to a stub and compiling every `LUX_PROFILE_*` away. Cuts link times. |
| `--no-aftermath` | Defines `LUX_DISABLE_AFTERMATH` **and** `removefiles` the `Platform/Vulkan/Debug/**.cpp` crash-tracker sources (they include `GFSDK_Aftermath.h` unconditionally, so `#ifdef` alone isn't enough). `Debug/Aftermath.h` then supplies inline no-ops, so callers need no `#if`. |
| Audio | Always the vendored miniaudio (`Audio/Native/`; its one implementation unit, `Audio/Native/MiniaudioImplementation.c`, is `NoPCH`) and the MiniaudioCpp wrapper (`Core/vendor/MiniaudioCpp/src`, `NoPCH`). No SDK, no flag, no deployed libraries. The `--audio-backend` option and the FMOD backend were removed in November 2026. |
| Acoustics | The in-tree Soundwave library (`Core/vendor/soundwave/`, globbed into `Core`): no SDK and no flag. |

**Script options** (change what the Python does; premake never sees them): `skip-submodules`,
`skip-vulkan-check`, `skip-scripts`.

Adding a new toggle = one entry in `BuildOptions.OPTIONS` + (for a premake kind) one `newoption` in
`premake5.lua`. The checklist UI, the saved config, and the command line are all driven off that
list — don't wire them up by hand.

### Non-interactive invocation

`BuildOptions.parse_cli` accepts flags with or without leading dashes:

```bash
python scripts/Win-GenProjects.py vs2022 no-tracy    # explicit
python scripts/Win-GenProjects.py --last             # reuse the saved config, no prompts
```

`--last` / `--reuse` is the one to use in scripts and CI. With no arguments the menus appear; when
stdin isn't a TTY, `Menu.py` degrades to plain prompts so the same path works unattended.

---

## Projects in the workspace

| Group | Project | Kind |
|---|---|---|
| Core | `Core` | StaticLib — the engine. PCH `lpch.h`. |
| Core | `ScriptCore` | C# (.NET 9) scripting assembly |
| Tools | `Editor` | The editor application |
| Tools | `Lux-Watchdog` | Windows only: the editor's crash watchdog, built into the editor's output folder (`Editor` depends on it) |
| Runtime | `Lux-Runtime` | Standalone runtime player |
| Dependencies | Box2D, JoltPhysics, GLFW, imgui, Tracy, NFD-Extended, Coral.Native, Coral.Managed, Coral.Generator (stub) | |
| Dependencies/Text | msdf-atlas-gen | |
| Dependencies/Renderer | NRI, NRI-VK, NRI-Validation, NRI-NONE, NRI-Shared | NRI's own `premake5.lua` (see below) |

**C# is built by MSBuild inside the solution.** `Coral.Managed` and `ScriptCore` are
premake-generated C# projects, not a separate `dotnet build` step. The .NET 9 SDK is still required
for MSBuild to build them. `Core`'s post-build copies `Coral.Managed.dll` / `.runtimeconfig.json` /
`.deps.json` (and `.pdb` outside Dist) into `Editor/DotNet/`, which is the `CoralDirectory` the
`ScriptEngine` host points at.

**The manual's C# API reference is generated.** `ScriptCore` writes `ScriptCore.xml` next to the DLL
(`GenerateDocumentationFile`; CS1591/CS1573 are off because the generator's check replaces them).
`tools/ScriptApiDocs` (a developer-only .NET 9 console tool, not in the solution) reads both and writes
`Editor/Resources/Docs/api/*.md`, the Help panel's "API Reference" section:
`dotnet run --project tools/ScriptApiDocs -c Release -- Editor/Resources/Scripts/ScriptCore.dll Editor/Resources/Docs`.
With `--check` it writes nothing and fails on a stale or extra page, a public member (enum values and
default constructors aside) without a `<summary>`, or a doc comment on something no longer public. CI
runs the check after the Windows Release build, so a public ScriptCore change needs its `///` comment
and the regenerated pages in the same commit.

### Soundwave source library

`Core/vendor/soundwave/include` is registered in `Dependencies.lua`. Its `src/**.cpp` files
compile directly into `Core` with `NoPCH`, so they include no engine headers and add no DLL
or separate link input. New files still require regeneration on both platforms.

### Acoustic verification commands

`scripts/Lux-BuildTest.ps1` regenerates the Visual Studio solution, builds Debug/Release/Dist and
checks nonempty Core/Runtime artifacts against their source timestamps, plus Editor in Debug/Release.
The solution intentionally excludes Editor in Dist; an older Dist Editor binary is not evidence.
It also runs the independent Soundwave and production occlusion checks. An x64 Visual Studio
developer prompt can run these individually with `tests/audio/run_soundwave.py` and
`tests/audio/run_occlusion_windows.py`.

`scripts/Lux-GoldenRun.ps1` wraps the existing renderer capture
runner with fresh-artifact and zero-validation checks; `scripts/lm9-golden-run.py` is a compatibility
entry point to that same runner. Golden summaries record the editor exit code, including crashes
after a status file was written.

### Vendored-submodule workarounds live in `premake5.lua`, not in the submodules

Three deliberate hacks, all documented inline — don't "fix" them by editing the submodule, because
those edits wouldn't travel with the repo and would break fresh checkouts:

- **Coral** upstream only defines Debug/Release, so `Coral.Native` is reopened here to add
  `Debug-AS`/`Dist`.
- **`Coral.Generator`** is a `kind "Utility"` stub so `Coral.Managed`'s `dependson` resolves at our
  pinned commit.
- **Tracy** on Linux force-includes `<cstring>` (its `TracyFastVector.hpp` uses `memcpy` in a
  template instantiated before `<string.h>` is fully parsed under GCC).

### NRI is a Lux fork with its own `premake5.lua`

`Core/vendor/NRI` is the submodule `https://github.com/starbounded-dev/NRI`, branch `lux`
(StudioCherno's `hazel` merged with NVIDIA-RTX/NRI `main`). Unlike the hacks above, NRI's build and
patches live **in the fork**: its `premake5.lua` builds the five static libraries, every source
change is marked `(Lux patch)` / `(Lux/Hazel patch)`, and the fork's README lists Lux's patches under
`LUX-PATCHES` (add yours there). Changing NRI means a commit on the fork's
`lux` branch, pushed **before** the Lux commit that moves the submodule pointer (CI checks out
submodules recursively and fails on an unpushed commit).

- NRI compiles against its **own** Vulkan headers (`External/VulkanHeaders`, v1.4.364, the version
  NRI's CMake fetches), not `VULKAN_SDK`: it needs extensions newer than the SDK Lux pins. NRI's
  public headers have no Vulkan types, so Core keeps using the SDK headers.
- VMA is NRI's (`External/VMA`, the commit NRI's CMake pins); `VMA_IMPLEMENTATION` is compiled once,
  in NRI-VK. Core does not use VMA.
- Linux defines `NRI_ENABLE_XLIB_SUPPORT=1` and `NRI_ENABLE_WAYLAND_SUPPORT=1` for every NRI project,
  and `VK_USE_PLATFORM_XLIB_KHR` / `VK_USE_PLATFORM_WAYLAND_KHR` only for NRI-VK (as NRI's CMake
  does), so no other NRI file sees X11's global `Window` typedef. Windows defines
  `VK_USE_PLATFORM_WIN32_KHR` for NRI-VK. D3D11/D3D12, WGPU, NVTX, NRIImgui and the upscaler SDKs are
  not built.
- `Dependencies.lua` links `NRI, NRI-VK, NRI-Validation, NRI-NONE, NRI-Shared` in that order (GNU ld
  resolves static archives left to right). Core defines `NRI_STATIC_LIBRARY=1`; without it `NRI_API`
  is `dllimport` on Windows.

---

## When you must re-run a script — fast lookup

Read this **before** opening the file you think is broken.

| Change you just made | Re-run | Why |
|---|---|---|
| Added, removed, or renamed a source file | `scripts\Win-GenProjects.bat` (or `--last`) | Premake globs `Source/**` at generation time; the `.vcxproj` is a snapshot |
| Edited `premake5.lua`, `Dependencies.lua`, or a project's `premake5.lua` | `scripts\Win-GenProjects.bat` | Nothing regenerates automatically |
| Added a new vendored dependency | Edit `Dependencies.lua`, then regenerate | `ProcessDependencies()` / `IncludeDependencies()` iterate the table; no manual `links`/`includedirs` |
| Added a build toggle | Add to `BuildOptions.OPTIONS` + `newoption` in `premake5.lua`, then regenerate | |
| Pulled a branch that bumped a submodule | `scripts\Setup.bat` | Runs `git submodule update --init --recursive` + `git lfs pull` |
| Pulled a branch that bumped the Vulkan SDK | `scripts\Setup.bat` | Re-runs `Vulkan.CheckVulkanSDK()` |
| Changed a shader (`.glsl` / `.glslh`) | nothing — hot-reload, or restart | Shaders compile at runtime and are cached; see `.claude/docs/Rendering.md` |
| Changed C# in `ScriptCore` or a project's scripts | rebuild the solution | MSBuild builds the C# projects in-solution |
| Changed `ScriptCore`'s public API | document it (`///`), rebuild, rerun `tools/ScriptApiDocs` | CI's API reference check fails otherwise |

If in doubt, `scripts\Win-GenProjects.bat --last` is fast and always safe. `scripts\Setup.bat` is
the heavier hammer and is also safe to re-run.

---

## Common failure modes

### "My new .cpp isn't being compiled" / "unresolved external symbol" for a function you just wrote

The file isn't in the generated project. Re-run `scripts\Win-GenProjects.bat`. This is the single
most common LuxEngine build failure and it does not look like a missing-file error — it looks like a
linker error.

### `premake5.lua` errors about an unknown option

`Configure.run_premake` passes the feature flags only when `include_options=True`. The sample
project's script solution is generated with `include_options=False` precisely because its
`premake5.lua` declares none of our `newoption`s, and premake hard-errors on an unrecognised
`--flag`. If you add a nested premake file, follow the same rule.

### Discord build fails on missing headers

`--discord` was enabled without `Core/vendor/discord_social_sdk/` present. The SDK is fetched
manually and is gitignored (it is very large). Either check it out or re-run generation without the
option.

### miniaudio struct mismatches

Native code includes miniaudio only through `Audio/Native/Miniaudio.h`, never `<miniaudio.h>`
directly: the wrapper sets `MA_ENGINE_MAX_LISTENERS`, which changes miniaudio's struct sizes.

### Aftermath headers not found

The crash tracker needs the Nvidia Aftermath SDK headers (`Core/vendor/NvidiaAftermath/include`).
Regenerate with `no-aftermath` to drop `Platform/Vulkan/Debug/**.cpp` from the build entirely.
Nothing links against Aftermath: `Aftermath.cpp` loads `GFSDK_Aftermath_Lib.x64.dll` /
`libGFSDK_Aftermath_Lib.x64.so` at runtime, and the Editor and Lux-Runtime post-build steps copy it
beside the executable (Linux: `lib/`, found through the `$ORIGIN/lib` rpath) in every config but
Dist. Without the file, the log says `Aftermath unavailable (… not found)` and the engine runs
without crash dumps.

### Missing textures / fonts, or asset files that are tiny text stubs

Git LFS content wasn't pulled. `git lfs pull`, or re-run `scripts\Setup.bat`.

### Editor starts, then fails in `ScriptEngine` with a missing Coral host

`Editor/DotNet/Coral.Managed.dll` isn't there. That file is produced by `Core`'s post-build step —
build `Core` (not just `Editor`), and confirm the .NET 9 SDK is installed so the C# projects
actually built.

### Windows: "nethost.dll was not found" (Editor, Lux-Runtime, or an exported game)

Coral links `nethost.lib`, so the exe needs `nethost.dll` beside it. The Editor and Lux-Runtime
post-build steps copy it from the .NET SDK pack `Coral.Native/premake5.lua` found
(`CORAL_NETHOST_BASE_PATH`). Runtime export copies every DLL in the Lux-Runtime output folder, so an
exported game gets it from there. Before this copy existed, only hand-placed copies in the Debug and
Release folders made it work, and Dist exports shipped without it. Regenerate and relink after
pulling the change: an up-to-date exe does not rerun post-build commands.

### Linux runtime fails to load `Archivo-Bold.ttf`

The runtime needs `Resources/Fonts/Archivo/static/Archivo-Bold.ttf` relative to its working
directory. Linux post-build copies must merge `Editor/Resources` and `Editor/DotNet` into the
runtime **parent directory**. Copying to an existing `Resources`/`DotNet` destination instead
creates nested `Resources/Resources` and `DotNet/DotNet`, leaving the files used at startup stale.
Regenerate and relink Lux-Runtime after changing the copy rules; an up-to-date executable does not
rerun post-build commands. `tests/runtime/run_resources.py` checks clean and repeated copies for
all Linux configurations using the generated Makefile.

`scripts/Linux-RunRuntime.sh` starts the build-folder player; it does not export a game. Export
from the editor first and launch the executable/launcher in the export folder, or pass that folder
as `--project=/absolute/path/to/export` to the build-folder player. An unexported build folder has no
`Assets/Project.luxruntime` or `AssetPack.lap` to load.

### Linux: `NFD-Extended` fails to configure

`gtk+-3.0` development files are missing. `Linux-Build.sh` checks for this up front and tells you the
Arch package; on other distros install the equivalent `gtk3-devel`.

### Linux: a deleted file's symbols still link

gmake2 archives `libCore.a` with `ar -rcs`, which adds and replaces members but never removes
them, so the objects of deleted sources used to linger and satisfy references that should fail.
`Core/premake5.lua` now deletes the archive in a Linux `prelinkcommands` step. If a pre-existing
`libCore.a` predates that (or a vendor static lib had a file removed), delete the archive once and
rebuild; `ar t <lib>` lists what is inside.

### Anything else "this clearly should compile"

Regenerate first (`Win-GenProjects.bat --last`), then delete `bin-int/` for the affected config and
rebuild. Stale-project and stale-PCH are the two dominant classes of mystery breakage.

---

## CI

`.github/workflows/main.yml` — builds on `windows-2025` for `Debug`, `Release`, `Dist`
(`fail-fast: false`), triggered on push to `master` / `dev` / `feature/**` and on PRs to `master` /
`dev`. Checks out with `submodules: recursive` and `lfs: true`, installs Vulkan SDK `1.4.335.0`,
generates with `vs2022`, and builds `Lux.sln` with platform `Mixed Platforms`.

Linux builds run on `ubuntu-26.04` for the same configurations via `scripts/Linux-Build.sh`.

Before building, every job runs `tests/audio/native` (the vendored miniaudio's offline
decode/render checks, and the engine's native backend driven through `AudioEngine`/
`AudioEventInstance`); it needs no audio SDK or device.

The jobs read no secrets, so they also run for fork pull requests, and packaging fails if an FMOD
library left by an old build reaches an editor artifact.

If a change affects generation (new premake option, new project, new dependency), make sure the CI
path still works with the non-interactive generator invocation — CI never gets a TTY.

---

## Running a build from an agent (tool-invocation notes)

Invoke `.bat` / `.ps1` scripts through the **PowerShell** tool, not the Bash tool. The Bash tool here
is MSYS/git-bash, and MSYS path conversion mangles `.bat` arguments — a build can no-op while still
reporting exit 0.

Verify by artifact, never by exit code:

```powershell
Get-Item 'bin\Debug-windows-x86_64\Editor\Editor.exe' | Select-Object Name, LastWriteTime
```

Builds write to the real filesystem, so run them with the sandbox disabled, and prefer
`run_in_background` with a log file for anything longer than a minute.

---

## Environment variables

- `LUX_AUDIO_PANELS_SELFTEST` — Debug/Release editor-only active disposable project directory.
  `python tests/editor/run_audio_panels.py` exercises Sound Bank automatic-save rejection/retry,
  undo and cached inventory, checked Sound saves and authored asset-pack round trips, Sound Editor
  section/tree reflow, real waveform/marker/curve input and undo, clip reload and audition teardown, plus Mixer
  reflow, state, save/retry, Apply refusal during Play and snapshot audition teardown. Audio Debugger
  probes cover captured source ownership, search, pause/refresh, narrow pages and context teardown. The runner
  uses Single/Multi, writes sibling `.status` files and exits. New C++ tests require regeneration;
  `LUX_TEST_EDITOR` can select a separately verified executable.

- `LUX_SETTINGS_PANELS_SELFTEST` — Debug/Release editor-only active disposable project directory.
  `python tests/editor/run_settings_panels.py` runs Application/Project Settings and offline Help
  reflow, optional panel-state round trips, text isolation, project save/failure/retry, acoustic
  controls and Light Settings context checks under Single/Multi. It writes a sibling `.status`
  and exits. New C++ tests require project
  regeneration; `LUX_TEST_EDITOR` can select a separately verified executable.

- `LUX_DIAGNOSTIC_CONTROLS_SELFTEST` — Debug/Release editor-only active disposable project directory.
  `python tests/editor/run_diagnostic_controls.py` runs Log filtering/follow/retention/detail lifetime,
  asset metadata refresh and History navigation under Single/Multi. It writes a sibling `.status`
  and exits. `LUX_TEST_EDITOR` optionally selects a separately verified executable. New C++ test
  files require project regeneration.

- `LUX_HIERARCHY_PANELS_SELFTEST` — Debug/Release editor-only active disposable project directory.
  `python tests/editor/run_hierarchy_panels.py` drives Single/Multi coverage of hierarchy/inspector
  reflow, text isolation, multi-entity edits, component search and scene snapshot persistence.
  Writes a sibling `.status` and an authored scene inside the fixture, then exits. The runner
  accepts optional `LUX_TEST_EDITOR`, like the browser/workspace/material runners.

- `LUX_DIAGNOSTIC_PLOTS_SELFTEST` — Debug/Release editor-only status-file stem. Exercises the
  production bounded timing history, missing GPU data, legend toggling, first-gesture inspection,
  pause/resume, outliers, scale/width changes and frozen GPU pass snapshots in isolated ImGui/ImPlot
  contexts. Writes `<stem>.status` and exits. `python tests/editor/run_diagnostic_plots.py` creates
  separate user data for Single and Multi without opening a project.

- `LUX_MATERIAL_PANELS_SELFTEST` — Debug/Release editor-only disposable project directory. The
  active project's directory must match. After startup project loading, exercises material save,
  failed-save/retry/reload, undo, removed-graph Revert, graph scope balance, responsive composition
  and presentation state in an isolated ImGui context, then drives normal application frames for
  shader-permutation reloads, invalid-graph repair, node previews and close/project teardown.
  Writes `<directory>.status` and exits.
  `python tests/editor/run_material_panels.py` creates separate Single/Multi projects and user data.

- `LUX_PANEL_CONTROLS_SELFTEST` — Debug/Release editor-only fixture directory, which must not
  exist. Exercises shared action-bar sizing/overflow in an isolated ImGui context and Beam's save,
  immediate edit/close, retry and document-retention APIs; also covers duplicate names, path aliases,
  Unicode paths, cancelled/failed Save As, failed close-save, many-tab navigation, popup focus/Enter
  handoff and the real vendored Find/Replace layout at multiple scales. Writes `<directory>.status` and exits.
  `python tests/editor/run_panel_controls.py` runs this under Single and Multi. Native Beam UI
  verification covers text entry, dialogs, shortcuts, workspace switching and resizing separately.

- `LUX_USER_DATA_DIR` — optional complete per-user storage override on Windows/Linux; use a new
  directory for editor verification so settings, layouts and logs never touch real preferences.
- `LUX_WORKSPACES_SELFTEST` — Debug/Release editor-only workspace fixture directory, which must
  not already exist. Runs production layout lifecycle/persistence tests in a separate ImGui context,
  writes `<directory>.status`, then exits normally. `python tests/editor/run_workspace_smoke.py`
  drives Single and Multi launches. `python tests/editor/run_workspaces.py` exercises docking/DPI
  against the vendored ImGui without a GPU.

- `LUX_DIR` — repo root. Set by `Setup.py` via `setx` on Windows; derived from the script location
  on Linux.
- `VULKAN_SDK` — used if set, otherwise the vendored `Core/vendor/VulkanSDK/x86_64` on Linux.
  `Dependencies.lua` reads it for Core's Vulkan headers and libraries.
- `VULKAN_VERSION`, `PREMAKE_VERSION` — override the pins in `scripts/Linux-Fetch.sh`.
- `BUILD_CONFIG`, `JOBS` — consumed by `scripts/Linux-Build.sh`.
- `LUX_RUN_AUDIO_TESTS=1`: optional Debug verification in `scripts/Linux-Build.sh`: runs the
  complete audio suite after dependency builds and before Core/Editor, then continues the full
  engine build. CI enables it for Debug. Compiled test outputs are checked for freshness.
- `LUX_GOLDEN_*` — read by the editor's golden-image capture (`Rendering.md § Golden image
  capture`). `LUX_GOLDEN_PLAY=1` enters normal Play and adds fresh Soundwave/Scene gain evidence. Its output goes to `bin/golden/`, which `/bin` in `.gitignore` already keeps out of
  commits.

### Linux CI Vulkan capability probe

The existing Build LuxEngine workflow accepts a manual `linux_vulkan_probe=true` input. It
runs only an Ubuntu 26.04 Xvfb/lavapipe capability probe; normal build jobs are skipped for
that explicit invocation. Push/PR/default manual builds retain the ordinary matrix. The probe
installs tools only on its disposable CI runner, accesses no licensed audio SDK and uploads
Vulkan summary/full-feature evidence. It checks the core features unconditionally requested by
VulkanDevice before attempting a native player check on CI. Passing the probe proves neither
engine startup nor audio playback/export, and cannot substitute for the real runtime gate.

Manual `linux_audio_check=runtime` selects only the Linux Debug job and drives the actual
Sponza Play/export/player runner under Xvfb/lavapipe and a PulseAudio null sink. `soundwave_cases`
is a space-separated list accepted by the native runner (default `corner`). The normal build,
full audio regressions, export validations and native report checks still apply; the CI job
uploads only logs/reports, never licensed SDK libraries or complete game packages. This tests
correctness on software Vulkan, not GPU performance or Phase 4 hardware parity. The native
fixture copies the sample's `Audio` folder (Sounds, clips, the demo Sound Bank) into its own tree;
it never writes through a link into the original project.

Manual `linux_audio_check=debug-as` selects only the Linux Debug-AS job. Native Core/Editor/Runtime
build before optional audio regressions; a regression failure still fails the job, while the
fresh-artifact and native sanitizer-symbol check runs afterward to retain independent evidence.
The ordinary push/PR/default manual matrix is unchanged. Use either audio mode or the standalone
Vulkan probe in one invocation. These switches only change disposable CI orchestration.

The runtime CI services create their private XDG runtime directory before exporting it to
subsequent steps: .NET's NuGet named mutexes use that directory during managed builds, before
the native window/audio test starts. An exported but nonexistent directory fails those builds.

### Linux exported runtime cannot find a vendored shared library

Every Linux Lux-Runtime configuration runs `scripts/Stage-LinuxRuntimeLibraries.sh` after linking
and explicit audio-library deployment. It uses the build system's bash/ldd/coreutils to copy only
the resolved Assimp/Aftermath/Vulkan-SDK shared-library closure into the runtime's `lib/`, keeping
loader filenames and dereferencing input symlinks. It checks resolution again with only that
directory in LD_LIBRARY_PATH and fails on unresolved or unstaged vendored libraries. It does not
copy all Vulkan tooling, replace system desktop libraries, or run tools during editor export.

Regenerate projects after the premake change and rebuild Lux-Runtime. Export copies staged
shared objects into the package's `lib/` and stops on copy/enumeration failure. Native acoustic
package checks use only that package directory for LD_LIBRARY_PATH; build-tree paths cannot hide
missing dependencies. A build or Play pass alone does not prove the exported loader path works.

Linux Debug package verification still enables Vulkan validation. Its developer runner writes a
capture-local copy of the SDK Khronos layer manifest with an absolute library path and sets
VK_LAYER_PATH to it. Only that diagnostic tool is resolved from the SDK; LD_LIBRARY_PATH remains
the package's lib directory, so SDK paths cannot hide missing game dependencies. Source SDK
manifests/settings are untouched. Release/Dist package checks need no validation-layer override.
The manual Linux Vulkan probe now exercises this exact helper and loader setup before engine
compilation, without fetching private audio SDKs; its manifest and tool output are retained.
