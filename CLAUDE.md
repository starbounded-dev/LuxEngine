# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

LuxEngine is a C++20, Vulkan-only 3D game engine and editor. It is a solo project. The codebase compiles as a static library (`Core`), a C# scripting assembly (`ScriptCore`), an editor application (`Editor`), and a standalone runtime player (`Lux-Runtime`). Everything lives in the `Lux` C++ namespace. The active development branch for Linux support is `feature/linux`.

---

## Build System

Premake5 is used to generate build files. The binary is checked in at `./premake5`.

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
