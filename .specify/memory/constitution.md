# LuxEngine Constitution

LuxEngine is a solo-developed C++20, Vulkan-only 3D engine and editor (`Core`, `ScriptCore`,
`Editor`, `Lux-Runtime`). This constitution is the gate every Spec Kit spec, plan, and task list is
checked against. It does not restate the engine's rules: `CLAUDE.md` and `.claude/docs/` are the
authority for their subjects, and a plan that conflicts with them is wrong.

## Core Principles

### I. Self-Contained, Small, Refined

People make games inside LuxEngine and must not install other software for an editor feature to
work.

- Build the feature into the engine, or vendor a small permissively licensed library (MIT / BSD /
  zlib / Apache-2.0) through `Dependencies.lua`.
- Reuse what the editor already requires: the .NET SDK, shaderc/glslang, the vendored miniaudio.
- No new mandatory installs for game makers. An unavoidable external tool is optional,
  auto-detected, degrades gracefully, and is put to the user before it is planned.
- One well-made feature beats several half-finished ones. Every plan states what a new dependency
  adds in binary size, startup time, and memory.

### II. Engine Conventions and Existing Helpers First

Code follows `.claude/docs/Conventions.md`: the `Lux` namespace, `Ref<T>` / `Scope<T>` ownership,
tagged logging, `LUX_PROFILE_*`, and platform divergence in separate translation units. Before a
plan adds a utility, it names the existing helper it checked (`Lux::FileSystem`, `Lux::Utils`,
`Lux::ImGuiEx`, `AssetManager`, ...). Every ImGui scope is closed and every widget has a unique ID.

### III. Threading and Renderer Invariants (NON-NEGOTIABLE)

- The editor runs a real render thread on Windows and is single-threaded on Linux. Plans touching
  `Renderer::Submit`, the JobSystem, asset loading, or events follow `.claude/docs/Threading.md`
  and state which thread each piece runs on.
- Renderer work follows `.claude/docs/Rendering.md`: `(set, binding)` is a global namespace;
  pipelines, shaders, and layouts are cached, never built per frame; GPU resources are freed through
  the release queue; validation errors are bugs.
- Shaders are GLSL-first (`Editor/Resources/Shaders/*.glsl`).
- No temporal rendering techniques (TAA, temporal upscalers, temporal accumulation). Ghosting is a
  red line; quality is bought spatially.

### IV. Independently Verifiable Phases

Every plan is split into phases that each build and are verified on their own before the next one
starts.

- A phase that adds or removes source files re-runs premake (`scripts\Win-GenProjects.bat`, or
  `scripts/Linux-Build.sh`); see `.claude/docs/Building.md`.
- Verification is concrete: a build of the touched projects, the headless harness or tests where
  they exist, a golden-image or profiler comparison for renderer or performance work. Claims of
  "done" cite what was run.
- The editor is launched only with the user's explicit OK for that step.
- Performance work measures before it changes anything (`/profile`).

### V. Docs Stay True

A change that alters an interface, adds an integration point, or changes a dependency between
systems updates `.claude/docs/Architecture-LuxEngine.md` in the same change. A new threading
primitive, renderer invariant, build toggle, or canonical helper updates `Threading.md`,
`Rendering.md`, `Building.md`, or `Conventions.md` respectively. Spec Kit artifacts under `specs/`
are point-in-time planning records, like `docs/*_PLAN.md`, and never stand in for those docs.

## Platform and Scope Constraints

- Targets Windows and Linux; a feature that diverges per platform says how each platform behaves.
- Vulkan through NRI is the only backend. `LUX_HAS_DX11` / `LUX_HAS_DX12` branches are reserved for
  a future backend and are not removed as dead code.
- Optional integrations (Discord, Tracy, Aftermath) stay behind their premake flags.

## Development Workflow

- `/speckit-specify` → `/speckit-clarify` (optional) → `/speckit-plan` → `/speckit-tasks` →
  implementation. `/plan-le` remains the native alternative for engine planning; either output must
  pass this constitution.
- Implementation runs under `/dev`, which loads the conventions, threading model, and renderer
  invariants first.
- `/cr` runs before each commit; `/send-pr` is the PR gate. Pull requests target `dev` unless told
  otherwise.

## Governance

This constitution sits under `CLAUDE.md` and `.claude/docs/`; where they disagree, those win and
this file is amended to match. Amendments bump the version (MAJOR for removing or redefining a
principle, MINOR for adding one, PATCH for wording) and update the date below. `/speckit-analyze`
and `/speckit-plan` treat a principle violation as a blocker unless the plan justifies it in its
Complexity Tracking section.

**Version**: 1.0.1 | **Ratified**: 2026-11-01 | **Last Amended**: 2026-11-02
