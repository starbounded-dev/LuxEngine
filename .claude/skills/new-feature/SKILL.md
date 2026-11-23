---
name: new-feature
description: LuxEngine feature workflow driven by the GitHub roadmap board. Takes a must-have or wanted feature (e.g. "M2 skeletal animation"), reads its sub-issues from the board, and makes each sub-issue a checkpoint instead of a "phase". Creates the feature branch and a draft PR whose body is a checklist of those sub-issues, then ticks one box per finished, verified, pushed feature. Use when starting work on a roadmap feature or milestone.
---

# new-feature — sub-issues are the checkpoints

The unit of progress is a **feature the board already names** (A1, R12, U2, ...), never a generic
"Phase 1/2/3". One sub-issue = one checkpoint = one checkbox = one commit (or small series).

`/new-feature <feature or issue #>` — e.g. `/new-feature M2`, `/new-feature 38`. With no argument,
ask which board item.

## Step 1 — Resolve the checkpoints from the board

Repo: `starbounded-dev/LuxEngine`; board: project 3 (`LuxEngine Project Roadmap`).

```bash
gh issue view <N> --repo starbounded-dev/LuxEngine --json number,title,body,state
gh api repos/starbounded-dev/LuxEngine/issues/<N>/sub_issues --jq '.[]|"#\(.number) \(.title) \(.state)"'
```

- Each **open** sub-issue is a checkpoint. Keep the board's ID and title verbatim (`A1: Import
  skeletons and clips`).
- Order them by real dependency (importer before playback before graph before C# API before editor
  tools), not by number. State the order and why in one line each.
- A parent with no sub-issues: stop and ask the user to split it on the board first.
- Do not invent checkpoints. If the work needs something the board lacks, say so and ask whether to
  add a sub-issue; do not hide it inside another one.

## Step 2 — Branch

Base is **`dev`**, never master. Check `git status` and the current branch first (a shared checkout
can be switched or mid-merge). If the checkout is dirty, conflicted, or on someone else's branch,
use a worktree instead of disturbing it:

```bash
git fetch origin dev
git worktree add -b feature/<slug> ../luxengine-<slug> origin/dev
```

## Step 3 — Draft PR with the checklist

Open the PR as a **draft** against `dev` as soon as the branch has one commit (the first commit may
be the checklist/plan note). The body is the checklist, one line per sub-issue, in dependency order:

```markdown
Part of #<parent>

## Checkpoints
- [ ] #65 A1: Import skeletons and clips
- [ ] #66 A2: Playback, blending, GPU skinning
...

## Notes
<decisions, open questions>
```

- Use `gh pr create --draft --base dev --body-file <file>`.
- **No co-author trailer and no "Generated with" footer** on commits or the PR.
- After creating it, bind it with the `ccd_pr` tools if running in the desktop app.

## Step 4 — Work a checkpoint

For each sub-issue, in order:

1. Run `/dev` once per session first; read the sub-issue body and the code it touches. Cite only
   what you read; mark anything that doesn't exist yet as NEW.
2. Implement only that sub-issue. Respect `.claude/docs/` (Conventions, Threading, Rendering,
   Building, Architecture) and the standing rules: ray tracing, terrain and
   DDGI start from scratch; DX11/DX12 scaffolding stays; no new installs for game makers.
3. Add or edit source files -> run `scripts\Win-GenProjects.bat`, then build.
4. **Done means:** it builds; the editor/runtime runs it; one concrete check proves it works (name
   the check and its result); the architecture doc is updated if an interface changed; `/cr` is
   clean.
5. Commit as `<ID>: <summary>` (e.g. `A1: Import skeletons and clips from glTF`), push.
6. **Tick the box** in the PR body and link the commit:

   ```bash
   gh pr view <PR> --json body --jq .body > body.md   # edit - [ ] -> - [x] <short sha>
   gh pr edit <PR> --body-file body.md
   ```

7. Stopping after any checkpoint must leave the engine building and shippable.

A checkpoint that cannot be verified yet (needs a later feature) is split on the board, not ticked.

## Step 5 — Finish

When every box is ticked, run `/send-pr` (the gate). It marks the draft ready. Sub-issues are closed
by `Closes #<n>` lines added at that point, the parent only if every sub-issue is closed.

## Boundaries

Ask before: merging, tagging, opening a non-draft PR, closing issues, or changing the board.
Committing and pushing finished checkpoints needs no extra ask.
