# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

LuxEngine is a C++20, Vulkan-only 3D game engine and editor. It is a solo project. The codebase compiles as a static library (`Core`), a C# scripting assembly (`ScriptCore`), an editor application (`Editor`), and a standalone runtime player (`Lux-Runtime`). Everything lives in the `Lux` C++ namespace. The active development branch for Linux support is `feature/linux`.

---

## Shared Context — read before substantive work

Detailed, source-grounded guidance lives in `.claude/docs/`. This file is the index; those are the
authority for their subject.

| Doc | Read before |
|---|---|
| `.claude/docs/Conventions.md` | writing or reviewing any C++ — style, and which helper already exists |
| `.claude/docs/Threading.md` | touching threads, `Renderer::Submit`, the JobSystem, asset loading, or events |
| `.claude/docs/Rendering.md` | anything in `Renderer/`, `Platform/Vulkan/`, or `Editor/Resources/Shaders/` |
| `.claude/docs/Building.md` | adding/removing files, changing premake, or diagnosing a build failure |
| `.claude/docs/Architecture-LuxEngine.md` | changing a system boundary, interface, ownership rule, or integration point |

The three facts most often gotten wrong by assuming this engine behaves like other Hazel-derived
codebases:

- **Shaders are GLSL-first**, not HLSL-only (`Editor/Resources/Shaders/*.glsl`).
- **The editor runs a real render thread by default on Windows** (single-threaded on Linux) — main
  and render are *not* the same thread.
- **Premake does not regenerate itself.** Adding a source file requires
  `scripts\Win-GenProjects.bat`, or you get an unresolved-external link error.

---

## Coding Workflow Skills

Two skills bracket a substantive coding session:

- **`/dev`** — preflight. Invoke once at the start to prime conventions, the threading model,
  renderer invariants, and the architecture index, so subsequent code respects them from the first
  line. `/dev <task>` primes and immediately executes.
- **`/cr`** — postflight. Invoke before committing to scan working-tree changes, auto-apply
  must-fix and should-fix issues, and report consider-tier nits. **Local only** — it does not
  commit, push, or open PRs.

**`/send-pr`** is the PR-time gate: same rule list, plus build verification, then it creates the PR.

The shared rule list lives in `.claude/skills/send-pr/SKILL.md` and is used by all three.

---

## Architecture Doc Maintenance

When implementation work changes an interface, adds an integration point, or modifies a dependency
between systems, you MUST update `.claude/docs/Architecture-LuxEngine.md` in the same change. It is
the single source of truth — there is no duplicate copy under `docs/`.

The same rule applies to the other shared docs when a change invalidates them: a new threading
primitive updates `Threading.md`, a new renderer invariant updates `Rendering.md`, a new build
toggle updates `Building.md`, a new canonical helper updates `Conventions.md`.

A stale architecture doc is worse than none, because it gets trusted.

**Not part of this repo:** `docs/` at the root holds point-in-time planning documents
(`ENGINE_OPTIMIZATION_PLAN.md`, `RENDERER_PERF_BASELINE.md`), not current architecture. Do not treat
them as authoritative, and do not update them to match code changes.

---

## Codex Compatibility

`AGENTS.md` is the Codex entry point. Codex-native skill adapters live under `.agents/skills/`,
while the complete shared workflows remain under `.claude/skills/` as the single source of truth.

When adding, renaming, or removing a skill under `.claude/skills/`, update the matching
`.agents/skills/<name>/SKILL.md` adapter in the same change. Keep Codex-specific content limited to
discovery metadata and tool/invocation translation, so the workflow itself does not fork.

---

## Build System

Premake5 is used to generate build files. Only the Windows binary is committed
(`vendor/bin/premake5.exe`); any file named `premake5` is gitignored, so a clean checkout has no
Linux binary. `scripts/Linux-Build.sh` downloads a pinned, checksum-verified build into
`vendor/bin/premake5` on first run — prefer that script over invoking premake directly.

**Generate Makefiles (Linux):**
```bash
./premake5 gmake2
```

**Premake options:**
- `--no-tracy` — exclude Tracy profiler (useful to reduce link times)
- `--no-aftermath` — exclude Nvidia Aftermath GPU crash tracker
- `--discord` — enable Discord Social SDK integration (requires `Core/vendor/discord_social_sdk/`)

**Build configs:** `debug`, `debug-as` (AddressSanitizer), `release`, `dist`

**Build everything (debug):**
```bash
make config=debug
```

**Build a single project:**
```bash
make config=debug Editor
make config=debug Core
make config=debug Lux-Runtime
```

**Convenience group targets:**
```bash
make config=debug Dependencies   # all third-party libs
make config=debug Core           # Core + ScriptCore
make config=debug Tools          # Editor
make config=debug Runtime        # Lux-Runtime
```

**Clean:**
```bash
make clean
```

**ScriptCore** (the C# assembly) is also built via dotnet independently. The Coral post-build step copies `Coral.Managed.dll` into `Editor/DotNet/` automatically after a Core build.

**Binaries** land in `bin/<config>-<os>-x86_64/<project>/`.

---

## Project Structure

```
Core/
  Source/Lux/          # Engine C++ source, organized by subsystem
  Platform/
    Linux/             # Linux-specific: FileSystem, RenderThread, Thread
    Windows/           # Windows-specific counterparts
  Source/Lux/Platform/Vulkan/   # Vulkan backend
  Source/lpch.h        # Precompiled header (include via lpch.h)
  vendor/              # All vendored C++ dependencies
ScriptCore/
  Source/Lux/          # C# scripting API (net9.0)
  ScriptCore.csproj
Editor/
  Source/              # Editor application (ImGui panels, EditorLayer)
Lux-Runtime/           # Standalone runtime player
Dependencies.lua       # Centralized dependency table (libs + include dirs)
premake5.lua           # Workspace definition
```

---

## Architecture

### Smart Pointers
- `Ref<T>` — intrusive reference-counted pointer. Engine objects (meshes, textures, shaders, scenes, etc.) almost universally use `Ref<T>`. Classes must inherit `RefCounted`. Use `Ref<T>::Create(...)`.
- `Scope<T>` — alias for `std::unique_ptr<T>`, used for non-shared ownership.

### ECS (Scene / Entity)
- `Scene` owns an `entt::registry`. `Entity` wraps an `entt::entity` + a `Scene*`.
- All component types are defined in `Core/Source/Lux/Scene/Components.h`.
- Scene serialization is YAML-based via `SceneSerializer`.
- Prefabs (`Prefab`) are serialized sub-hierarchies.

### Renderer
- `SceneRenderer` is the main high-level renderer. It owns and drives the `RenderGraph`.
- `RenderGraph` manages passes, scratch-resource reuse, and compile caching. Passes are skipped (zero cost) when their feature is off.
- The pipeline is deferred PBR with a G-buffer, clustered (froxel) light culling, and a separate forward pass for transparents.
- The Vulkan backend lives in `Core/Source/Lux/Platform/Vulkan/`. All renderer API types (`Shader`, `Texture`, `Pipeline`, etc.) are abstract; their Vulkan implementations are in that folder.
- `Renderer2D` provides a 2D batch renderer (quads, circles, lines, MSDF text).
- Shader hot-reload and SPIR-V reflection caching are handled by `VulkanShaderCompiler` / `VulkanShaderCache`.
- On Linux, HLSL shaders are compiled by shelling out to `dxc`. `HlslIncluder.cpp` is excluded from Linux builds.

### Asset Pipeline
- `AssetManager` is a static facade. Internally it delegates to `AssetManagerBase` (virtual interface).
- At runtime there are two concrete implementations: `EditorAssetManager` (editor, loads from source files) and `RuntimeAssetManager` (runtime, loads from binary asset packs).
- Every asset is identified by an `AssetHandle` (a `UUID`). Assets derive from `Asset`.
- Asset types are declared in `AssetTypes.h` and file extension mappings in `AssetExtensions.h`.

### Scripting (C# / Coral)
- `ScriptEngine` manages the .NET 9 runtime via Coral (`Core/vendor/Coral/`).
- `ScriptGlue` registers C++ internal calls that `ScriptCore` calls via `[MethodImpl(MethodImplOptions.InternalCall)]`.
- `ScriptCore` (C# assembly) lives in `ScriptCore/Source/Lux/` and provides the public API to game scripts.
- Built assemblies are deployed to `Editor/Resources/Scripts/`.

### Physics
- **3D**: Jolt Physics via `PhysicsSystem` / `PhysicsScene`. Jolt-specific wrappers in `Core/Source/Lux/Physics/JoltPhysics/`.
- **2D**: Box2D for 2D rigid bodies and colliders.
- Mesh colliders are cooked and cached by `MeshCookingFactory` / `MeshColliderCache`.

### Audio
- miniaudio via `AudioEngine`, `AudioSource`, `AudioListener`.

### Threading
- Optional dedicated render thread (`RenderThread`, platform-impl in `Core/Platform/<OS>/`).
- Optional simulation thread (`SimulationThread`) — experimental, off by default.
- Job system in `Core/Source/Lux/Core/JobSystem`.

### Profiling
- Tracy macros are wrapped in `Core/Source/Lux/Debug/Profiler.h` as `LUX_PROFILE_*`.
- Enabled by default in all configs except `dist` (or when `--no-tracy` is passed to premake).
- Nvidia Aftermath GPU crash dumps are in `Platform/Vulkan/Debug/` and excluded from `dist` builds.

### Configuration Macros
- `LUX_PLATFORM_WINDOWS` / `LUX_PLATFORM_LINUX`
- `LUX_DEBUG` / `LUX_RELEASE` / `LUX_DIST`
- `LUX_TRACK_MEMORY` (debug + release only)
- `LUX_HAS_VULKAN` (always defined)

### Adding a New Dependency
Edit `Dependencies.lua` — add an entry to the `Dependencies` table. Platform-specific lib names go in `Windows = { ... }` / `Linux = { ... }` sub-tables. The `ProcessDependencies()` / `IncludeDependencies()` helpers iterate it automatically; no manual `links {}` or `includedirs {}` needed in project files.
