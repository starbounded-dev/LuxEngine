# LuxEngine [![License](https://img.shields.io/github/license/starbounded-dev/luxengine.svg)](LICENSE) [![Build LuxEngine](https://github.com/starbounded-dev/LuxEngine/actions/workflows/main.yml/badge.svg)](https://github.com/starbounded-dev/LuxEngine/actions/workflows/main.yml)

![LuxEngine](/Resources/Branding/LuxEngineLogo.png?raw=true "LuxEngine")

LuxEngine is a C++20, Vulkan-only 3D game engine and editor for **Windows and Linux**, in active development. Its architecture descends from [Hazel](https://github.com/TheCherno/Hazel), but it has grown well beyond that starting point: a deferred, clustered PBR renderer with a render graph, Jolt physics, C# scripting, a UUID-based asset pipeline with runtime asset packs, an FMOD Studio + ray-traced acoustics audio stack, and a docking ImGui editor with a standalone runtime player.

This is a solo project that doubles as a learning vehicle for engine architecture. It is not production-ready and does not pretend to be — the sections below say plainly what works, what is partial, and what does not exist yet.

***

## Platform support

| Platform | Status | Notes |
|---|---|---|
| **Windows 10/11 x64** | Supported | Visual Studio 2022 (CI target) or 2026; MSBuild. Dedicated render thread on by default. |
| **Linux x64** | Supported | Built and verified in CI on Ubuntu 24.04; developed daily on Arch. Clang + GNU make. |
| macOS | Not supported | No Metal/MoltenVK backend, no plan today. |
| Consoles / mobile | Not supported | On hold — no SDK access. |

Both platforms build all three targets: `Editor`, `Lux-Runtime` (standalone player), and the `Core` static library.

***

## Quick start

```bash
# 1. Clone with submodules
git clone --recursive https://github.com/starbounded-dev/LuxEngine
cd LuxEngine

# 2. Install the required SDKs (see "Required SDKs" — FMOD and Vercidium Audio are mandatory)

# 3a. Linux: one command does everything
./scripts/Linux-Build.sh release
./scripts/Linux-Run.sh release

# 3b. Windows: generate, then build in Visual Studio
scripts\Setup.bat
#    -> open Lux.sln (VS2022) or Lux.slnx (VS2026), build, run Editor
```

If you cloned non-recursively, run `git submodule update --init --recursive` first.

***

## Required SDKs

Four external SDKs are needed. **None of them are vendored** — the repository ships build glue, not licensed binaries.

| SDK | Version | Why | Where |
|---|---|---|---|
| **Vulkan SDK** | 1.4.335.0 (pinned) | Renderer, shader compilation (shaderc/DXC), validation layers | [LunarG](https://vulkan.lunarg.com/) — the Linux script downloads it for you |
| **.NET SDK** | **9.0 specifically** | C# scripting (Coral hardcodes hostfxr major 9; `ScriptCore`/`Coral.Managed` target `net9.0`) | [dotnet.microsoft.com](https://dotnet.microsoft.com/download) |
| **FMOD Engine** | 2.03.x (Core + Studio APIs) | Audio playback — **required, not optional** | [fmod.com](https://www.fmod.com/download) |
| **Vercidium Audio (VA)** | 1.8.x | Ray-traced acoustics — **required, not optional** | [vercidium.com/docs](https://vercidium.com/docs) |

You also want the **FMOD Studio** authoring tool (same 2.03.x family) to author and build banks, and its command-line tool `fmodstudiocl` if you want the editor to rebuild banks on Play.

A Vulkan **1.2+ capable driver** is the runtime requirement (the instance is created at API 1.2); the 1.4 SDK is a build/tooling dependency.

### Installing the audio SDKs

Premake validates the complete SDK layout at generation time and aborts with the exact missing file path, so a half-extracted package fails fast rather than at link time.

**Default locations** (extract the packages here):

```
Core/vendor/FMOD/<any-subdirectory>/     e.g. Core/vendor/FMOD/fmodstudioapi20314linux/
Core/vendor/VA_RAY/
```

**Or point at them with environment variables** before generating:

```bash
# Linux
export LUX_FMOD_SDK=/opt/fmodstudioapi20314linux   # root containing api/
export LUX_VA_SDK=/opt/VA_RAY                      # root containing 3d/
export LUX_FMOD_STUDIO_CL=/opt/fmodstudio/fmodstudiocl   # optional
```

```powershell
# Windows
$env:LUX_FMOD_SDK = 'C:\SDKs\FMOD Studio API Windows'
$env:LUX_VA_SDK   = 'C:\SDKs\VA_RAY'
$env:LUX_FMOD_STUDIO_CL = 'C:\Program Files\FMOD SoundSystem\FMOD Studio 2.03.14\fmodstudiocl.exe'
```

Required files per platform:

| | Windows | Linux |
|---|---|---|
| FMOD Core | `api/core/inc/fmod.hpp`, `api/core/lib/x64/fmod_vc.lib`, `fmod.dll` | `api/core/inc/fmod.hpp`, `api/core/lib/x86_64/libfmod.so`, `libfmod.so.14` |
| FMOD Studio | `api/studio/inc/fmod_studio.hpp`, `api/studio/lib/x64/fmodstudio_vc.lib`, `fmodstudio.dll` | `api/studio/inc/fmod_studio.hpp`, `api/studio/lib/x86_64/libfmodstudio.so`, `libfmodstudio.so.14` |
| VA | `3d/native/include/vaudio.h`, `3d/native/production/windows/vaudionative.lib` + `.dll` | `3d/native/include/vaudio.h`, `3d/native/production/linux/libvaudionative.so` |

Regenerate project files after changing an SDK root. Never commit SDK packages.

***

## Building on Linux

### 1. Install distro packages

The build needs Clang, GNU make, pkg-config, the .NET 9 SDK, GTK3 (file dialogs via NFD-Extended), X11 **and** Wayland development files (GLFW is built with both backends), and libdw/libunwind (crash backtraces).

<details open>
<summary><b>Arch / Manjaro / EndeavourOS</b></summary>

```bash
sudo pacman -S --needed base-devel clang make pkgconf curl tar git \
    gtk3 libx11 libxrandr libxinerama libxcursor libxi \
    libxkbcommon wayland wayland-protocols \
    elfutils libunwind onetbb zlib \
    vulkan-icd-loader
# .NET 9 specifically — the `dotnet-sdk` metapackage is 10.x and will not work:
sudo pacman -S --needed dotnet-sdk-9.0
# GPU driver (pick yours):
sudo pacman -S --needed vulkan-radeon      # AMD
sudo pacman -S --needed vulkan-intel       # Intel
sudo pacman -S --needed nvidia-utils       # NVIDIA
```
</details>

<details>
<summary><b>Debian / Ubuntu / Pop!_OS / Mint</b> (this is the CI configuration)</summary>

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    clang llvm make pkg-config git curl \
    libgtk-3-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libxkbcommon-dev libwayland-dev libwayland-bin wayland-protocols \
    libdw-dev libunwind-dev libtbb-dev zlib1g-dev \
    mesa-vulkan-drivers
# .NET 9 SDK (Ubuntu 24.04+):
sudo apt-get install -y dotnet-sdk-9.0
```

For the `dist` configuration (LTO), make sure an unversioned `llvm-ar` resolves — Ubuntu ships it version-suffixed and premake's clang toolset invokes the plain name:

```bash
sudo ln -sf "$(ls -1 /usr/bin/llvm-ar-* | sort -V | tail -1)" /usr/bin/llvm-ar
```
</details>

<details>
<summary><b>Fedora / RHEL / Nobara</b> (untested — equivalents, please report corrections)</summary>

```bash
sudo dnf install -y clang llvm make pkgconf-pkg-config git curl \
    gtk3-devel \
    libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel \
    libxkbcommon-devel wayland-devel wayland-protocols-devel \
    elfutils-devel libunwind-devel tbb-devel zlib-devel \
    vulkan-loader mesa-vulkan-drivers dotnet-sdk-9.0
```
</details>

<details>
<summary><b>openSUSE Tumbleweed</b> (untested)</summary>

```bash
sudo zypper install -y clang llvm make pkg-config git curl \
    gtk3-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel \
    libxkbcommon-devel wayland-devel wayland-protocols-devel \
    libdw-devel libunwind-devel tbb-devel zlib-devel \
    vulkan-tools Mesa-vulkan-device-select dotnet-sdk-9.0
```
</details>

### 2. Build

`scripts/Linux-Build.sh` is the single entry point. It is idempotent and safe to re-run — every step checks before it acts.

```bash
./scripts/Linux-Build.sh            # prompts for a configuration
./scripts/Linux-Build.sh release    # non-interactive
JOBS=4 ./scripts/Linux-Build.sh debug
LUX_PREMAKE_OPTIONS="--no-tracy" ./scripts/Linux-Build.sh release
```

What it does, in order:

1. Checks prerequisites (`dotnet`, `make`, `clang`, `pkg-config`, `curl`, `tar`, GTK3) and names the missing one.
2. `git submodule update --init --recursive`.
3. Downloads the pinned **Vulkan SDK 1.4.335.0** into `Core/vendor/VulkanSDK/` if `VULKAN_SDK` is unset and none is vendored.
4. Downloads a checksum-verified **premake5 5.0.0-beta4** into `vendor/bin/` (only the Windows premake binary is committed).
5. Generates the C# projects (via the `vs2022` action — `gmake2`'s C# generator needs `csc`, which the .NET SDK does not put on PATH), then builds `Coral.Managed` and `ScriptCore` with `dotnet`.
6. Generates makefiles (`premake5 gmake2 --cc=clang`) and builds `Dependencies`, `Core`, `Editor`, `Lux-Runtime`.
7. Generates and builds the sample project's C# script assembly.

Useful environment variables:

| Variable | Effect |
|---|---|
| `BUILD_CONFIG` | `Debug` / `Release` / `Dist` — skips the prompt |
| `JOBS` | make parallelism (defaults to `nproc`) |
| `LUX_PREMAKE_OPTIONS` | Extra premake flags, e.g. `--no-tracy --no-aftermath` |
| `VULKAN_SDK` | Use a system Vulkan SDK instead of the vendored one |
| `VULKAN_VERSION` | Override the SDK version to download |
| `LUX_SKIP_BUILD` | (run scripts) launch without the rebuild check |

### 3. Manual build (no script)

Once premake5 exists and the SDKs are in place:

```bash
./premake5 gmake2 --cc=clang
make -j$(nproc) config=debug            # everything
make -j$(nproc) config=release Editor   # one project
make help                               # list configurations and targets
make clean
```

Group targets: `Dependencies`, `Dependencies/Renderer`, `Dependencies/Text`, `Core`, `Tools` (Editor), `Runtime` (Lux-Runtime).

> **Premake does not regenerate itself.** After adding or removing a source file, re-run the generation step or you get an unresolved-symbol link error.
>
> **Feature flags are baked into the generated makefiles.** Pass the same `--no-tracy` / `--no-aftermath` / `--discord` set to *every* generation; a generation that omits them silently compiles the feature out and produces a confusing undefined-reference failure against already-built objects.

***

## Building on Windows

### 1. Prerequisites

- **Visual Studio 2022** (Desktop development with C++) — the CI-verified target. **Visual Studio 2026** also works locally with the v145 toolset installed.
- **Python 3.9+** on PATH (the setup scripts validate and install their own pip packages).
- **Vulkan SDK 1.4.335.0** — the setup script downloads the installer and prompts you if it is missing. Debug builds additionally need the SDK's shader debug libraries.
- **.NET 9 SDK**.
- **FMOD Engine + Vercidium Audio SDKs** (see above).

### 2. Generate and build

```bat
scripts\Setup.bat
```

`Setup.bat` runs an interactive configuration: a checklist of optional settings, then the Visual Studio version. It sets the `LUX_DIR` environment variable, verifies the Vulkan SDK, pulls submodules and Git LFS assets, warns about missing optional/required SDKs, and generates the solution. Your choices are remembered in `scripts/.luxsetup.json`.

Then open **`Lux.sln`** (VS2022) or **`Lux.slnx`** (VS2026) and build. `Editor` is the main app; `Lux-Runtime` is the standalone player.

To regenerate project files later without re-running the full setup:

```bat
scripts\Win-GenProjects.bat
scripts\Win-GenProjects.bat --last      :: reuse the saved configuration, no prompts
scripts\Win-GenProjects.bat vs2022 no-tracy    :: fully non-interactive
```

Or build from the command line:

```bat
MSBuild Lux.sln /restore /m /p:Configuration=Release /p:Platform="Mixed Platforms"
```

`/restore` is required: `ScriptCore` and `Coral.Managed` are SDK-style C# projects inside the solution and cannot build without a NuGet restore (error `NETSDK1004`).

***

## Build configurations

| Config | Optimisation | Symbols | Notes |
|---|---|---|---|
| `debug` | off | on | Assertions, Jolt FP exceptions, Vulkan validation |
| `debug-as` | off | on | AddressSanitizer — **Windows only**; on Linux it is a plain debug build |
| `release` | on | default | AVX2 + BMI/POPCNT/LZCNT/F16C, `NDEBUG` |
| `dist` | full | off | LTO, no Tracy, no Aftermath — shipping builds |

Binaries land in `bin/<Config>-<os>-x86_64/<Project>/`, e.g. `bin/Release-linux-x86_64/Editor/Editor`.

### Premake options

| Flag | Effect |
|---|---|
| `--no-tracy` | Build without Tracy instrumentation (smaller, faster to link) |
| `--no-aftermath` | Build without the Nvidia Aftermath GPU crash tracker |
| `--discord` | Enable the Discord Social SDK integration (needs `Core/vendor/discord_social_sdk/`) |
| `--fmod`, `--raytraced-audio` | Accepted for compatibility; both SDKs are always required |

On Windows these are offered as checkboxes by `Setup.bat`; on Linux pass them via `LUX_PREMAKE_OPTIONS` or directly to `premake5`.

***

## Running

### Linux

```bash
./scripts/Linux-Run.sh release              # rebuilds if needed, then launches the editor
LUX_SKIP_BUILD=1 ./scripts/Linux-Run.sh release   # launch as-is
./scripts/Linux-RunRuntime.sh release       # standalone player
```

The run scripts set `VULKAN_SDK`, `VK_LAYER_PATH` and `LD_LIBRARY_PATH` for the vendored Vulkan, Assimp and Aftermath libraries, and `cd` into `Editor/` so relative resource paths resolve. Launching the binary directly from `bin/` without that environment will fail to find its shared libraries.

Both X11 and Wayland sessions work (GLFW is built with both backends; libdecor is deliberately disabled to avoid a double titlebar under Wayland).

### Windows

Set `Editor` as the startup project in Visual Studio and run — the working directory is already configured. Outside VS, run `bin\<Config>-windows-x86_64\Editor\Editor.exe` from the `Editor/` directory. `fmod.dll`, `fmodstudio.dll` and `vaudionative.dll` are copied next to the executables by the post-build step.

### First launch

The editor opens the bundled sample project at `Editor/LuxSampleProject/LuxSample.luxproj`. See [`Editor/LuxSampleProject/FMOD_SETUP.md`](Editor/LuxSampleProject/FMOD_SETUP.md) for the FMOD demo scene, and [`docs/Editor/`](docs/Editor/README.md) for the full editor manual.

***

## Tests

Headless regression suites (no renderer, no audio device — FMOD runs in `NOSOUND` mode):

```bash
python tests/audio/run.py                 # audio: surfaces, music, dialogue, geometry, budgets
python tests/audio/run_managed.py         # C# audio bindings: payloads, ABI, subscriptions
python tests/audio/run_sdk_layout.py      # SDK layout validation
python tests/rendering/run_shadow_shader.py
python tests/runtime/run_resources.py
```

The audio runner needs `clang++`, both audio SDKs and `fmodstudiocl`; it copies the sample FMOD project to a temporary directory and never mutates the repo. See [`tests/audio/README.md`](tests/audio/README.md) for fixture requirements.

***

## Packaging

```bash
./packaging/linux/build-appimage.sh release editor    # LuxEditor.AppImage
./packaging/linux/build-appimage.sh release runtime   # LuxRuntime.AppImage
```

Bundles the binary, the vendored Vulkan/Assimp/Aftermath shared libraries, resources and the .NET host into an AppDir. There are no packaged releases yet — you build from source.

***

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Required audio SDK file missing: ...` at generation | FMOD or VA is missing or incompletely extracted. The message names the exact file; fix that path or set `LUX_FMOD_SDK` / `LUX_VA_SDK`. |
| `hostfxr` / runtime not found at editor startup | You have .NET 10 (or 8), not 9. Coral hardcodes major version 9 — install `dotnet-sdk-9.0`. Distro `dotnet-sdk` metapackages now pull 10.x. |
| Undefined references after toggling `--no-tracy` | The flag must be passed to every premake generation. Regenerate with the same flags, then rebuild. |
| Unresolved externals after adding a source file | Premake does not regenerate itself. Re-run `scripts\Win-GenProjects.bat` / `./premake5 gmake2 --cc=clang`. |
| `gtk+-3.0 development files not found` | Install GTK3 dev packages (NFD-Extended uses them for file dialogs). |
| `llvm-ar: not found` in a `dist` build | Symlink the version-suffixed binary (see the Debian/Ubuntu block). |
| "Script project file not found" in the editor | The sample project's `.csproj` is generated output. Run `Editor/LuxSampleProject/Assets/Scripts/Linux-GenProjects.sh`, or just re-run `Linux-Build.sh`. |
| `error NETSDK1004` on Windows | MSBuild was run without `/restore`. |
| Editor starts but shows no banks/events | Project Settings → Audio: the `.fspro` path is relative to `Assets/`, and **Bank Output is a directory** (`Build/Desktop`), not a `.bank` file. |

***

## What works today

### Rendering (Vulkan)
- **Deferred PBR pipeline** with a G-buffer, clustered (froxel) light culling for point/spot lights, and a separate forward pass for transparents.
- **Render graph** with compile caching and scratch-resource reuse; passes are skipped when their feature is off (zero-cost-when-disabled is an explicit goal).
- **Shadows** — cascaded directional shadow maps (2K default) and spot-light shadow maps.
- **Sky** — Preetham sky, HDR environment maps (equirect → cubemap, irradiance + prefiltered mips), skybox pass.
- **Post-processing** — GTAO (with temporal + denoise), screen-space reflections (with temporal + composite), bloom, depth of field, and HZB generation used for occlusion and SSR pre-integration.
- **Physical imaging** — exposure as manual multiplier, manual EV100, physical camera (aperture/shutter/ISO), or histogram auto-exposure; ACES and AgX tonemapping; physical light units.
- **Volume system** — blendable post-process, atmosphere, and fog volumes (box/sphere) that override settings per region.
- **GPU-driven bits** — GPU scene buffers, compute mesh culling, per-pass GPU timing.
- **2D batch renderer** — quads/sprites, circles, lines, and MSDF text rendering.
- **Editor rendering** — jump-flood selection outlines, depth-tested collider pass, wireframe and G-buffer/AO debug views, infinite grid, debug renderer.
- Sky atmosphere, volumetric clouds and TAA were removed during the renderer simplification pass and are not currently available.

### Audio (FMOD Studio + Vercidium Audio)
- **FMOD Studio events** as the only playback path — banks, event instances, parameters, buses, snapshots, and in-editor bank building (`fmodstudiocl`).
- **Ray-traced acoustics (VA)** — occlusion, muffling and reverb from real scene geometry, with dynamic geometry updates, room portals and per-surface acoustic materials.
- **Zones and ambience** blending, an **interactive music director** (states, intensity, layers, stingers, timeline callbacks), and **physics-driven footsteps/impacts** from Jolt contacts via surface tables.
- **Dialogue** — localized dialogue tables, programmer sounds, subtitles and narration.
- **Accessibility** — category gains, mono/compressor DSPs, narration ducking, captions and directional cues.
- **Performance controls** — voice budgets, distance culling, multi-listener attenuation, bus peak/RMS monitoring, and a validation pass surfaced in the Audio Debugger.
- **Desktop platform profiles** — per-OS Studio platform, bank output directory, budgets, and focus/mute behaviour.

### Engine systems
- **ECS scenes** (EnTT) with entity hierarchies, organizational folders, **prefabs with variants** (per-component overrides, revert/apply, propagation), YAML serialization, and editor Play / Simulate / Stop.
- **3D physics** (Jolt) — rigid bodies, box/sphere/capsule/mesh/compound colliders, a character controller, physics layers, and a mesh-cooking cache. **2D physics** (Box2D) — rigid bodies, box and circle colliders.
- **C# scripting** (.NET 9 via [Coral](https://github.com/StudioCherno/Coral)) — script components with hot reload, attribute-driven inspectors (`[Range]`/`[Header]`/`[Tooltip]`), and bindings for entities, transforms, input, assets, audio, music, dialogue and accessibility.
- **Asset pipeline** — UUID-handle asset manager with editor and runtime variants, an asset registry, Assimp mesh import, texture import, material assets, and binary **asset packs + shader packs** (including FMOD banks) for shipping runtime builds.
- **Standalone runtime** — `Lux-Runtime` plays a packaged project without the editor.
- **Multithreading** — optional dedicated render thread (validated on and off), a job system, and an experimental simulation thread.
- **Tooling & debugging** — Tracy CPU/GPU profiling on every pass, Nvidia Aftermath GPU crash dumps, shader hot-reload with a SPIR-V reflection cache, memory tracking, and tiering/quality settings serialized per project.

### Editor
Docking ImGui editor ("Monolith, warmed" theme) with viewport + ImGuizmo gizmos, **snapshot-based undo/redo** with a History panel, a **command palette**, an in-editor **Profiler**, a rebuilt **Content Browser** (filter chips, grid/list, sort, favourites, thumbnail cache), **Beam** multi-tab text editor, material editor, scene renderer and renderer debugger panels, render stats, light settings, asset manager, **Audio Debugger**, project settings, entity lock/colour labels, pinned bookmarks and numbered camera bookmarks. Full manual in [`docs/Editor/`](docs/Editor/README.md).

***

## Honest limitations

- **No macOS, no mobile, no consoles.** Windows and Linux x64 only.
- **Vulkan only.** No DirectX, Metal, or OpenGL backends (NVRHI is vendored but not the active path).
- **FMOD and Vercidium Audio are hard requirements.** There is no fallback backend — the build will not generate without both SDKs, and neither is redistributable, so there is no zero-setup path.
- **No skeletal animation yet.** Skeleton/bone import scaffolding and animated-mesh shader variants exist, but the animation importer and playback system are not wired up. Static meshes only, in practice.
- **No particle system.**
- **No terrain in-tree yet.** A GPU clipmap terrain with Jolt heightfield collision is in development on a branch, not merged.
- **Scripting API is still partial.** No physics or renderer bindings yet.
- **No networking, no AI/navigation.**
- **2D is a renderer, not a toolset.** Sprites, circles, lines, and text render fine, but there are no tilemaps or 2D-specific editor workflows.
- **Rough edges everywhere.** One sample project, no packaged releases — you build from source.

***

## Active development

Recent work has been the **audio system** (15 phases: FMOD Studio integration, VA acoustics, zones, music, dialogue, accessibility, budgets, desktop profiles) and **Linux parity** — Linux is now a first-class, CI-verified target rather than an aspiration.

Ongoing: a measured performance campaign (see [docs/ENGINE_OPTIMIZATION_PLAN.md](docs/ENGINE_OPTIMIZATION_PLAN.md)) — Tracy-instrumented baselines, render-graph compile caching, eliminating per-frame allocations on the submission path, descriptor-set churn fixes, and LTO'd Dist builds.

**Next up (roughly in order):**
- Skeletal animation (import → playback → animated passes, which already exist shader-side)
- Merging the procedural terrain system
- Broader C# scripting API (physics, renderer)
- Asset streaming / async upload hardening
- Particles

***

## Continuous integration

The [Build LuxEngine](.github/workflows/main.yml) workflow builds Debug, Release and Dist on **two** runners:

- **Windows Server 2025** — recursive LFS/submodule checkout, Python + Vulkan SDK + .NET 9, `scripts/Setup.py vs2022`, then MSBuild of `Lux.sln`.
- **Ubuntu 24.04** — apt build dependencies, Vulkan SDK + .NET 9, then `scripts/Linux-Build.sh`, followed by an artifact verification step and a bundling step that resolves the binary's vendored shared libraries via `ldd` into `lib/` (the binary's RPATH is `$ORIGIN/lib`).

Release uploads an `editor-<config>` / `editor-linux-<config>` artifact with the built editor, sample project and resources; build logs are uploaded per configuration.

> CI does not have the FMOD/VA SDKs, so audio-SDK-dependent generation is the one thing the public workflow cannot cover end to end.

***

## Technology

| Area | Library |
|---|---|
| Graphics | Vulkan (SDK 1.4), shaderc, SPIRV-Cross, SPIRV-Tools, DXC |
| Windowing / UI | GLFW (X11 + Wayland), Dear ImGui (docking), ImGuizmo |
| Physics | Jolt Physics (3D), Box2D (2D) |
| ECS | EnTT |
| Scripting | Coral (.NET 9 / C#) |
| Assets | Assimp, stb, yaml-cpp |
| Text | msdf-atlas-gen / msdfgen, FreeType |
| Audio | FMOD Core + FMOD Studio, Vercidium Audio |
| Profiling / debug | Tracy, Nvidia Aftermath, backward-cpp |
| Math / util | glm, spdlog, magic_enum, choc, FastNoise |

***

## Repository layout

```
Core/            Engine static library
  Source/Lux/      C++ source by subsystem (Renderer, Scene, Audio, Physics, Asset, Scripting, ...)
  Platform/        Linux/ and Windows/ platform implementations
  vendor/          Vendored C++ dependencies (+ FMOD/ and VA_RAY/ SDK drop points)
ScriptCore/      C# scripting API (net9.0)
Editor/          Editor application + LuxSampleProject
Lux-Runtime/     Standalone runtime player
scripts/         Setup, project generation, build and run scripts
packaging/linux/ AppImage packaging
tests/           Headless regression suites (audio, rendering, runtime)
docs/            Editor manual + audio system design docs
Dependencies.lua Centralized dependency table
premake5.lua     Workspace definition
```

Adding a dependency means one entry in `Dependencies.lua` — the include/link wiring is generated from it.

***

## License

LuxEngine is licensed under the **[Apache License 2.0](LICENSE)**. Attribution for the vendored
third-party components and for the Hazel lineage this engine descends from lives in [NOTICE](NOTICE).

That license covers LuxEngine's own source code only. Building also requires the **FMOD Engine** and
**Vercidium Audio** SDKs, which are proprietary, are not included in this repository, and are
governed by their own EULAs. If you distribute binaries built from this source, satisfying those
terms — and NVIDIA's for the bundled Aftermath libraries — is on you.

***

## The plan

LuxEngine's purpose is two-fold: to become a capable 3D engine, and to serve as an education vehicle for game engine design and architecture. Everything is learned and implemented by one person, so development is deliberate rather than fast — depth over breadth, and honest status reporting over marketing.
