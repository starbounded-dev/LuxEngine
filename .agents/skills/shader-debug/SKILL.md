---
name: shader-debug
description: LuxEngine shader debugging — compile errors, edits with no visible effect, black or garbage output, startup crashes after a shader change, binding collisions, and device-lost or vendor-specific GPU faults. Use whenever a .glsl/.glslh/.hlsl change misbehaves or rendering output is wrong.
---

# shader-debug (Codex adapter)

**The workflow body is `.claude/skills/shader-debug/SKILL.md`. Read it and follow it.** This file
exists only for Codex discovery and tool translation; it deliberately does not restate the workflow,
so the two cannot drift.

## Translation notes

- `/shader-debug` in shared docs means this skill, normally invoked as `$shader-debug`.
- `$ARGUMENTS` is the symptom supplied with the invocation; use it to pick the triage section.
- Claude-specific tool names in the body describe an intended action. Use the equivalent
  Codex-native tool.
- Paths starting with `.claude/`, `.agents/`, `docs/`, `scripts/`, or `tests/` are relative to the
  repository root. Shader and cache paths under `Resources/` are relative to the `Editor/` folder.
- Steps that need the running editor (Ctrl+Shift+R, Renderer Debugger views, RenderDoc) require the
  user unless you can launch and observe the app; the offline `glslc` / `spirv-val` path works
  without it.
- `/cr` in the body refers to the `cr` repository skill.

## Boundary

`shader-debug` does not commit, push, or open pull requests.
