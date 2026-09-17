---
name: profile
description: LuxEngine performance investigation — rule out the present/VSync ceiling, decide CPU- vs GPU-bound, capture Tracy/RenderDoc when needed, and report before/after numbers on a fixed protocol. Use for low FPS, hitches, slow loads, or optimization and benchmark requests.
---

# profile (Codex adapter)

**The workflow body is `.claude/skills/profile/SKILL.md`. Read it and follow it.** This file exists
only for Codex discovery and tool translation; it deliberately does not restate the workflow, so the
two cannot drift.

## Translation notes

- `/profile` in shared docs means this skill, normally invoked as `$profile`.
- `$ARGUMENTS` is the symptom or target supplied with the invocation. With no arguments, run the
  triage in Step 1.
- Claude-specific tool names in the body describe an intended action. Use the equivalent
  Codex-native tool.
- Paths starting with `.claude/`, `.agents/`, `docs/`, `scripts/`, or `tests/` are relative to the
  repository root.
- Steps that need the running editor (panels, Tracy connection, RenderDoc) require the user unless
  you can launch and observe the app; say which measurements you could not take yourself.
- `/cr` in the body refers to the `cr` repository skill.

## Boundary

`profile` does not commit, push, or open pull requests.
