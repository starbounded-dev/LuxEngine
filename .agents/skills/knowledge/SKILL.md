---
name: knowledge
description: LuxEngine knowledge lookup — answers engine, editor, and runtime questions by querying the graphify graph first, then confirming in source and .claude/docs/. Use for architecture and code-location questions.
---

# knowledge (Codex adapter)

**The workflow body is `.claude/skills/knowledge/SKILL.md`. Read it and follow it.** This file exists
only for Codex discovery and tool translation; it deliberately does not restate the workflow.

## Translation notes

- `/knowledge` in shared docs means this skill, normally invoked as `$knowledge`.
- `$ARGUMENTS` is the question. With none, ask.
- The graph lives in the main checkout's `graphify-out/` (gitignored); pass `--graph` in worktrees.
