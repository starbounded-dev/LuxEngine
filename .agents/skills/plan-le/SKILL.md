---
name: plan-le
description: LuxEngine implementation planning — a source-grounded, phased plan with a verified current-state ledger, user decisions, engine-fit analysis (systems, threads, ownership), independently verifiable phases, risks, and open questions. Use before multi-file or multi-session work, or when asked for a plan, design, or roadmap. Plans only.
---

# plan-le (Codex adapter)

**The workflow body is `.claude/skills/plan-le/SKILL.md`. Read it and follow it.** This file exists
only for Codex discovery and tool translation; it deliberately does not restate the workflow, so the
two cannot drift.

## Translation notes

- `/plan-le` in shared docs means this skill, normally invoked as `$plan-le`.
- `$ARGUMENTS` is the goal to plan. With no arguments, ask what to plan.
- "Ask the user" and "plan mode" in the body describe intended actions. Use Codex's equivalent
  (a direct question, or its planning/approval flow); where none exists, present the plan in chat
  and wait for approval.
- Claude-specific tool names describe an intended action. Use the equivalent Codex-native tool.
- Step 4 (web research) needs a web search/fetch capability. If this Codex environment has none,
  say so in the plan, mark the Research brief as not done, and list what should be researched.
- Paths starting with `.claude/`, `.agents/`, `docs/`, or `scripts/` are relative to the repository
  root.
- `/dev`, `/cr`, `/send-pr`, and `/profile` in the body refer to the repository skills of the same
  names.

## Boundary

`plan-le` does not edit engine code, commit, push, or open pull requests. Writing a
`docs/<AREA>_PLAN.md` file is the only file it may create, and only when the user agrees.
