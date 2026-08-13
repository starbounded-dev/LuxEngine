---
name: cr
description: LuxEngine local pre-commit review — scan working-tree changes against the shared rule list, apply must-fix and should-fix repairs, report consider-tier nits. Local only.
---

# cr (Codex adapter)

**The workflow body is `.claude/skills/cr/SKILL.md`. Read it and follow it.** This file exists only
for Codex discovery and tool translation; it deliberately does not restate the workflow, so the two
cannot drift.

## Translation notes

- `/cr` in shared docs means this skill, normally invoked as `$cr`.
- The rule list referenced by the body lives in `.claude/skills/send-pr/SKILL.md § The rule list`.
- Claude-specific tool names describe an intended action; use the equivalent Codex-native tool.
- Paths starting with `.claude/`, `.agents/`, `docs/`, or `scripts/` are relative to the repository
  root.

## Boundary — enforced

`cr` is **local only**. It must not run `git commit`, `git push`, `gh pr create`, or any other
publishing command. If the user wants a pull request, hand off to `send-pr`.
