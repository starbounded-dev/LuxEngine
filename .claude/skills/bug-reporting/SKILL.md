---
name: bug-reporting
description: LuxEngine bug reporting. Turns a defect into a GitHub issue grounded in source — reproduces or verifies it, finds the root cause with file:line, checks for duplicates, proposes a fix with options and a recommendation, then files it using the repo's bug template. Use when the user says "file a bug", "issue a bug for that", or a defect should be tracked rather than fixed now.
---

# bug-reporting — LuxEngine bug issues

Files **one** issue per defect, with evidence and a proposed fix. It reports; it does not fix.
Invoking this skill (or "issue a bug for that") is the user's go-ahead to create the issue. Anything
beyond that (assigning, milestones, closing others, opening a fix PR) still needs a separate ask.

## Procedure

1. **State the defect in one sentence** and who it hits (editor users, game makers, CI).
2. **Verify it from source, not memory.** Read the code path; run the smallest check that shows it
   (grep, a diff, a headless run). Record concrete numbers: handles, file:line, counts, versions.
   Do not launch the editor without explicit OK that turn.
3. **Find the root cause**, not the symptom. Cite `path:line` for each link in the chain. Say what you
   could not verify.
4. **Search for duplicates**: `gh issue list -S "<keywords>" --state all -L 10`. If one exists, comment
   on it instead of filing, and tell the user.
5. **Propose a solution.** Give 1-3 options, one recommended, each with its cost and risk. Respect the
   Product Principle in `CLAUDE.md` (built in, small, no new mandatory installs). Include migration
   impact for anything touching saved data (scenes, registry, asset packs, project files).
6. **File it** with `gh issue create --label bug`, body from `.github/ISSUE_TEMPLATE/bug_report.md`
   sections (Describe / To Reproduce / Expected) plus **Root cause**, **Proposed solution**,
   **Out of scope / risks**. Title format: `bug : <short symptom and cause>`. Write the body to a
   temp file and pass `--body-file`.
7. **Report** the issue URL and the recommended option in two lines.

## Rules

- Never put personal details (name, email, machine paths outside the repo) in the issue.
- No `Co-Authored-By` or generated-by footers (project rule).
- Keep it factual: what is true, what is unverified, what is a guess.
- One defect per issue. Split unrelated findings and offer to file each.
- Link related issues/PRs as `owner/repo#N`, never a bare `#N`.
