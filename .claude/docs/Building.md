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
scripts/Linux-Fetch.sh          # vendored Vulkan SDK + premake
scripts/Linux-Build.sh          # prompts for a config, or: Linux-Build.sh release
scripts/Linux-Run.sh            # editor
scripts/Linux-RunRuntime.sh     # standalone runtime player
```

`Linux-Build.sh` is one-shot and idempotent: it checks prerequisites (`dotnet make clang pkg-config
curl tar`, plus `gtk+-3.0` for NFD-Extended file dialogs), syncs submodules, locates or downloads a
**pinned, checksum-verified** premake (`5.0.0-beta4`) into `vendor/bin/premake5`, generates
makefiles, and builds. Only the Windows premake binary is committed — any file named `premake5` is
gitignored — so a clean checkout has no Linux binary and the script must fetch one. Prefer the
script over invoking premake directly.

`VULKAN_SDK` defaults to the vendored `Core/vendor/VulkanSDK/x86_64` if unset.

---

## Configurations

Declared in `premake5.lua`: `Debug`, `Debug-AS`, `Release`, `Dist`.

| Config | Optimize | Symbols | Notes |
|---|---|---|---|
| `Debug` | Off | On | `LUX_ENABLE_ASSERTS`, `LUX_TRACK_MEMORY`, Tracy on |
| `Debug-AS` | Off | On | Debug + AddressSanitizer (Windows: `NoRuntimeChecks`, `NoIncrementalLink`, `editandcontinue "Off"`) |
| `Release` | On | Default | `NDEBUG`; asserts compile out, `VERIFY` stays, Tracy on |
| `Dist` | Full | Off | `NDEBUG` + LTO; Tracy and Aftermath compiled out |

`LUX_CORE_VERIFY` is enabled in **every** config (`LUX_ENABLE_VERIFY` is unconditional in
`Assert.h`) — it is the assert that survives into Dist.

---

## Optional features

Two kinds of toggle, both driven off the single `OPTIONS` table in `scripts/BuildOptions.py`:

**Premake flags** (forwarded to premake as `--<flag>`; `premake5.lua` declares a matching
`newoption`):

| Flag | Effect |
|---|---|
| `--discord` | Enables the Discord Social SDK integration; defines `LUX_ENABLE_DISCORD`. Requires `Core/vendor/discord_social_sdk/` (fetched manually — `Configure.warn_missing_discord_sdk` warns if absent). |
| `--no-tracy` | Omits `TRACY_ENABLE` / `TRACY_ON_DEMAND` / `TRACY_CALLSTACK`, reducing vendored Tracy to a stub and compiling every `LUX_PROFILE_*` away. Cuts link times. |
| `--no-aftermath` | Defines `LUX_DISABLE_AFTERMATH` **and** `removefiles` the `Platform/Vulkan/Debug/**.cpp` crash-tracker sources (they include `GFSDK_Aftermath.h` unconditionally, so `#ifdef` alone isn't enough). |

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
| Runtime | `Lux-Runtime` | Standalone runtime player |
| Dependencies | Box2D, JoltPhysics, GLFW, imgui, Tracy, NFD-Extended, Coral.Native, Coral.Managed, Coral.Generator (stub) | |
| Dependencies/Text | msdf-atlas-gen | |
| Dependencies/Renderer | NVRHI, NVRHI-Vulkan, NVRHI-D3D11, NVRHI-D3D12 | |

**C# is built by MSBuild inside the solution.** `Coral.Managed` and `ScriptCore` are
premake-generated C# projects, not a separate `dotnet build` step. The .NET 9 SDK is still required
for MSBuild to build them. `Core`'s post-build copies `Coral.Managed.dll` / `.runtimeconfig.json` /
`.deps.json` (and `.pdb` outside Dist) into `Editor/DotNet/`, which is the `CoralDirectory` the
`ScriptEngine` host points at.

### Vendored-submodule workarounds live in `premake5.lua`, not in the submodules

Three deliberate hacks, all documented inline — don't "fix" them by editing the submodule, because
those edits wouldn't travel with the repo and would break fresh checkouts:

- **Coral** upstream only defines Debug/Release, so `Coral.Native` is reopened here to add
  `Debug-AS`/`Dist`.
- **`Coral.Generator`** is a `kind "Utility"` stub so `Coral.Managed`'s `dependson` resolves at our
  pinned commit.
- **NVRHI**'s cmake-branch premake expects `HazelRootDirectory` and `DefaultTargetParams` from the
  original Hazel build system; both are defined in `premake5.lua` (the former pointing at
  `scripts/compat`). The NVRHI projects are then reopened to add Vulkan include dirs and
  `NVRHI_WITH_RTXMU=1`.
- **Tracy** on Linux force-includes `<cstring>` (its `TracyFastVector.hpp` uses `memcpy` in a
  template instantiated before `<string.h>` is fully parsed under GCC).

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

### Aftermath headers not found

The crash tracker needs the Nvidia Aftermath SDK. Regenerate with `no-aftermath` to drop
`Platform/Vulkan/Debug/**.cpp` from the build entirely.

### Missing textures / fonts, or asset files that are tiny text stubs

Git LFS content wasn't pulled. `git lfs pull`, or re-run `scripts\Setup.bat`.

### Editor starts, then fails in `ScriptEngine` with a missing Coral host

`Editor/DotNet/Coral.Managed.dll` isn't there. That file is produced by `Core`'s post-build step —
build `Core` (not just `Editor`), and confirm the .NET 9 SDK is installed so the C# projects
actually built.

### Linux: `NFD-Extended` fails to configure

`gtk+-3.0` development files are missing. `Linux-Build.sh` checks for this up front and tells you the
Arch package; on other distros install the equivalent `gtk3-devel`.

### Anything else "this clearly should compile"

Regenerate first (`Win-GenProjects.bat --last`), then delete `bin-int/` for the affected config and
rebuild. Stale-project and stale-PCH are the two dominant classes of mystery breakage.

---

## CI

`.github/workflows/main.yml` — builds on `windows-2025` for `Debug`, `Release`, `Dist`
(`fail-fast: false`), triggered on push to `master` / `dev` / `features/**` and on PRs to `master` /
`dev`. Checks out with `submodules: recursive` and `lfs: true`, installs Vulkan SDK `1.4.335.0`,
generates with `vs2022`, and builds `Lux.sln` with platform `Mixed Platforms`.

Note the branch globs: CI matches `features/**`, so a branch named `feature/foo` (singular) will
**not** be built.

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

- `LUX_DIR` — repo root. Set by `Setup.py` via `setx` on Windows; derived from the script location
  on Linux.
- `VULKAN_SDK` — used if set, otherwise the vendored `Core/vendor/VulkanSDK/x86_64` on Linux.
  `Dependencies.lua` and the NVRHI project overrides both read it, so they compile against the same
  Vulkan headers (a mismatch produces C++ wrapper ABI errors).
- `VULKAN_VERSION`, `PREMAKE_VERSION` — override the pins in `scripts/Linux-Fetch.sh`.
- `BUILD_CONFIG`, `JOBS` — consumed by `scripts/Linux-Build.sh`.
