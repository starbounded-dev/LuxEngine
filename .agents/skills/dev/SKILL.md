---
name: dev
description: LuxEngine coding preflight — load conventions, threading model, renderer invariants, and architecture index before writing code. Invoke once per substantive coding session.
---

# dev (Codex adapter)

**The workflow body is `.claude/skills/dev/SKILL.md`. Read it and follow it.** This file exists only
for Codex discovery and tool translation; it deliberately does not restate the workflow, so the two
cannot drift.

## Translation notes

- `/dev` in shared docs means this skill, normally invoked as `$dev`.
- `$ARGUMENTS` is the task text supplied with the invocation. With no arguments, prime and stop;
  with arguments, prime and then execute the task.
- Claude-specific tool names in the body describe an intended action. Use the equivalent
  Codex-native tool and preserve every safety gate.
- Paths starting with `.claude/`, `.agents/`, `docs/`, or `scripts/` are relative to the repository
  root.
- `/cr` and `/send-pr` in the body refer to the `cr` and `send-pr` repository skills.

## Boundary

`dev` does not commit, push, or open pull requests.
