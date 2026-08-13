---
name: send-pr
description: LuxEngine pull-request gate — run the full shared rule list against the branch diff, fix what must be fixed, verify the build, then create the pull request.
---

# send-pr (Codex adapter)

**The workflow body — and the canonical shared rule list — is `.claude/skills/send-pr/SKILL.md`.
Read it and follow it.** This file exists only for Codex discovery and tool translation; it
deliberately does not restate the rules, so the two cannot drift.

## Translation notes

- `/send-pr` in shared docs means this skill, normally invoked as `$send-pr`.
- The rule list in the body (rules 1–19, tiered must-fix / should-fix / consider) is shared with
  `dev` and `cr`. It is maintained in one place; do not fork it here.
- Claude-specific tool names describe an intended action; use the equivalent Codex-native tool and
  preserve every safety gate.
- Build commands in the body are written for the PowerShell tool on Windows. Use the Codex shell
  equivalent, and keep the rule that a build is verified by **artifact timestamp**, never by exit
  code.
- Paths starting with `.claude/`, `.agents/`, `docs/`, or `scripts/` are relative to the repository
  root.

## Boundary

`send-pr` is the only one of the three skills permitted to commit, push, and open a pull request —
and only after the build has been verified. If the user wants the same checks **without** a PR, run
the body's steps 1–4 and stop.
