---
name: new-feature
description: LuxEngine feature workflow driven by the roadmap board — sub-issues become checkpoints, a feature branch and draft PR carry a sub-issue checklist, and one box is ticked per verified, pushed feature. Use when starting a roadmap feature or milestone.
---

# new-feature (Codex adapter)

**The workflow body is `.claude/skills/new-feature/SKILL.md`. Read it and follow it.** This file
exists only for Codex discovery and tool translation; it deliberately does not restate the workflow.

## Translation notes

- `/new-feature` in shared docs means this skill, normally invoked as `$new-feature`.
- `$ARGUMENTS` is the feature or issue number. With none, ask which board item.
- Paths starting with `.claude/`, `.agents/`, `docs/`, `scripts/` are relative to the repository root.
- `/dev`, `/cr`, `/send-pr` refer to the repository skills of those names.
