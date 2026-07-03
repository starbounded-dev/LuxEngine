# LuxEngine [![License](https://img.shields.io/github/license/starbounded-dev/luxengine.svg)](LICENSE) [![Build LuxEngine](https://github.com/starbounded-dev/LuxEngine/actions/workflows/main.yml/badge.svg)](https://github.com/starbounded-dev/LuxEngine/actions/workflows/main.yml)

![LuxEngine](/Resources/Branding/LuxEngineLogo.png?raw=true "LuxEngine")

LuxEngine is a C++20, Vulkan-based 3D game engine and editor for Windows, in active development. Its architecture descends from [Hazel](https://github.com/TheCherno/Hazel), but it has grown well beyond that starting point: a deferred, clustered PBR renderer with a render graph, volumetric clouds and physically-based sky, Jolt physics, C# scripting, a UUID-based asset pipeline with runtime asset packs, and a docking ImGui editor with a standalone runtime player.

This is a solo project that doubles as a learning vehicle for engine architecture. It is not production-ready and does not pretend to be — the sections below say plainly what works, what is partial, and what does not exist yet.

***

## What works today

### Rendering (Vulkan 1.4)
- **Deferred PBR pipeline** with a G-buffer, clustered (froxel) light culling for point/spot lights, and a separate forward pass for transparents.
- **Render graph** with compile caching and scratch-resource reuse; passes are skipped when their feature is off (zero-cost-when-disabled is an explicit goal).
- **Shadows** — cascaded directional shadow maps (2K default) and spot-light shadow maps.
- **Sky & atmosphere** — physically-based sky atmosphere, Preetham sky, HDR environment maps (equirect → cubemap, irradiance + prefiltered mips), skybox pass.
- **Volumetric clouds** — Nubis/RDR2-style system with baked 3D noise textures (base shape, detail, curl), temporal reprojection, and a composite pass.
- **Volumetric / atmospheric fog** — froxel fog with clustered local-light in-scattering, exponential height fog, and local fog volumes.
- **Post-processing** — GTAO (with temporal + denoise), screen-space reflections (with temporal + composite), TAA, bloom, depth of field, and HZB generation used for occlusion and SSR pre-integration.
- **Physical imaging** — exposure as manual multiplier, manual EV100, physical camera (aperture/shutter/ISO), or histogram auto-exposure; ACES and AgX tonemapping; physical light units.
- **Volume system** — blendable post-process, atmosphere, and fog volumes (box/sphere) that override settings per region.
- **GPU-driven bits** — GPU scene buffers, compute mesh culling, per-pass GPU timing.
- **2D batch renderer** — quads/sprites, circles, lines, and MSDF text rendering (msdf-atlas-gen).
- **Editor rendering** — jump-flood selection outlines, wireframe and G-buffer/AO debug views, infinite grid, debug renderer.

### Engine systems
- **ECS scenes** (EnTT) with entity hierarchies, prefabs, YAML scene serialization, and editor Play / Simulate / Stop.
- **3D physics** (Jolt) — rigid bodies, box/sphere/capsule/mesh/compound colliders, a character controller, physics layers, and a mesh-cooking cache. **2D physics** (Box2D) — rigid bodies, box and circle colliders.
- **C# scripting** (Mono) — script components with a managed `ScriptCore` assembly; entity, transform, and input bindings.
- **Asset pipeline** — UUID-handle asset manager with editor and runtime variants, an asset registry, Assimp mesh import, texture import, material assets, and binary **asset packs + shader packs** for shipping runtime builds.
- **Audio** (miniaudio) — audio source and listener components; play/stop with basic controls.
- **Standalone runtime** — `Lux-Runtime` plays a packaged project without the editor.
- **Multithreading** — optional dedicated render thread (validated on and off), a job system, and a simulation thread.
- **Tooling & debugging** — Tracy CPU/GPU profiling on every pass, Nvidia Aftermath GPU crash dumps, shader hot-reload with a SPIR-V reflection cache, validation-layer plumbing, memory tracking, and tiering/quality settings serialized per project.

### Editor
Docking ImGui editor with viewport + ImGuizmo gizmos, content browser with thumbnail cache, material editor, scene renderer and renderer debugger panels, render stats, light settings, asset manager panel, editor console, project settings, and a basic text editor. Ships with a sample project.

***

## Honest limitations

- **Windows only.** Linux paths exist in the build scripts but are untested and almost certainly broken. No macOS, no mobile.
- **Vulkan only.** No DirectX, Metal, or OpenGL backends (NVRHI is vendored but not the active path).
- **No skeletal animation yet.** Skeleton/bone import scaffolding and animated-mesh shader variants exist, but the animation importer and playback system are not wired up. Static meshes only, in practice.
- **No particle system.**
- **No terrain in-tree yet.** A GPU clipmap terrain with Jolt heightfield collision is in development on a branch, not merged.
- **Scripting API is thin.** The C# surface covers entities, transforms, and input — no physics, audio, or renderer bindings yet.
- **Audio is basic.** Play/stop and simple parameters; no mixer, DSP, or spatialization work.
- **No networking, no AI/navigation.**
- **2D is a renderer, not a toolset.** Sprites, circles, lines, and text render fine, but there are no tilemaps or 2D-specific editor workflows.
- **Rough edges everywhere.** One sample project, sparse docs (see `docs/`), no packaged releases — you build from source.

***

## Active development

Current focus is a measured performance campaign (see [docs/ENGINE_OPTIMIZATION_PLAN.md](docs/ENGINE_OPTIMIZATION_PLAN.md)): Tracy-instrumented baselines, render-graph compile caching, eliminating per-frame allocations on the submission path, descriptor-set churn fixes, and LTO'd Dist builds. Recent work also landed the clustered lighting rewrite (the old forward/tiled path was removed) and render-thread validation.

**Next up (roughly in order):**
- Skeletal animation (import → playback → animated passes, which already exist shader-side)
- Merging the procedural terrain system
- Broader C# scripting API
- Asset streaming / async upload hardening
- Particles
- Linux support, eventually — the build system keeps it in mind, nothing more

***

## Getting started

Visual Studio 2022 is the recommended CI-compatible target. Visual Studio 2026 generation is also supported for local development when the v145 toolset is installed. Other environments are untested.

**1. Clone recursively** (submodules are required):

```
git clone --recursive https://github.com/starbounded-dev/LuxEngine
```

If you cloned non-recursively, run `git submodule update --init --recursive`.

**2. Configure dependencies:**

1. Run [Setup.bat](scripts/Setup.bat) in the `scripts` folder. It validates Python packages, checks the Vulkan SDK, pulls Git LFS assets and submodules, and generates project files.
2. The **Vulkan SDK 1.4.x** is required. If missing, the script downloads the installer and prompts you; Debug builds additionally need the SDK's shader debug libraries.
3. After installing the SDK, run [Setup.bat](scripts/Setup.bat) again.
4. To regenerate project files later, run [Win-GenProjects.bat](scripts/Win-GenProjects.bat).

Then open `Lux.sln` and build. `Editor` is the main workspace app; `Lux-Runtime` is the standalone player.

***

## Continuous integration

The [Build LuxEngine](.github/workflows/main.yml) workflow builds Debug, Release, and Dist on Windows Server 2025: recursive LFS/submodule checkout, Python + Vulkan SDK install, VS2022 project generation via `scripts/Setup.py vs2022`, and an MSBuild of `Lux.sln`. Debug and Release upload an `editor-<configuration>` artifact with the built editor, sample project, and resources; MSBuild logs are uploaded per configuration.

***

## Technology

| Area | Library |
|---|---|
| Graphics | Vulkan 1.4, shaderc, SPIRV-Cross, SPIRV-Tools, DXC |
| Windowing / UI | GLFW, Dear ImGui (docking), ImGuizmo |
| Physics | Jolt Physics (3D), Box2D (2D) |
| ECS | EnTT |
| Scripting | Mono (C#) |
| Assets | Assimp, stb, yaml-cpp |
| Text | msdf-atlas-gen / msdfgen, FreeType |
| Audio | miniaudio |
| Profiling / debug | Tracy, Nvidia Aftermath |
| Math / util | glm, spdlog, magic_enum, choc, FastNoise |

***

## The plan

LuxEngine's purpose is two-fold: to become a capable 3D engine, and to serve as an education vehicle for game engine design and architecture. Everything is learned and implemented by one person, so development is deliberate rather than fast — depth over breadth, and honest status reporting over marketing.
