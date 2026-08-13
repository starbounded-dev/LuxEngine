---
name: dev
description: LuxEngine coding preflight. Loads the conventions, threading model, renderer invariants, and architecture index so subsequent code respects them from the first line. Invoke once at the start of a substantive coding session; "/dev <task>" primes and then executes the task.
---

# dev — LuxEngine coding preflight

Run this **once**, before writing code, when the work is more than a one-line edit. It exists
because the expensive mistakes in this repo are the ones made in the first thirty seconds — picking
a descriptor binding that collides, putting GPU work on the wrong thread, adding a component that
never serializes, adding a file and never regenerating the project.

`/dev` with no argument primes and stops. `/dev <task>` primes and then does the task.

---

## Step 1 — Load the shared context

Read these now, not later:

| Doc | Read when |
|---|---|
| `.claude/docs/Conventions.md` | **always** — style + which helper already exists |
| `.claude/docs/Threading.md` | **always** — even "pure logic" changes run on some thread |
| `.claude/docs/Architecture-LuxEngine.md` | the section for the system you're touching |
| `.claude/docs/Rendering.md` | anything under `Renderer/`, `Platform/Vulkan/`, or `Editor/Resources/Shaders/` |
| `.claude/docs/Building.md` | if adding/removing files, or if a build fails |
| `.claude/skills/send-pr/SKILL.md` | the rule list you will be reviewed against |

Then read the actual headers for the system in question. The docs are structural; **the header is
the source of truth for the API**.

---

## Step 2 — Orient before editing

Establish, and state briefly:

1. **Which system** the change belongs to, and which systems it is allowed to depend on
   (`Architecture-LuxEngine.md § Part 1` dependency table).
2. **Which thread** the new code runs on. If you cannot answer this, you are not ready to write it.
3. **Whether a playbook already covers it** — `Architecture-LuxEngine.md § Part 4` has the
   multi-step checklists for components, asset types, render passes, editor panels, internal calls,
   threads, dependencies, and build toggles. These are the changes that fail *silently* when a step
   is skipped.
4. **Whether an existing helper does this** — `Conventions.md § Helper reuse`.
5. **Whether project regeneration will be needed** (any added, removed, or renamed file).

---

## Step 3 — The five things that most often go wrong here

Keep these in working memory while writing:

1. **`(set, binding)` is a global namespace across every shader.** A new uniform or storage buffer
   slot is not a local decision. Grep the shader corpus before choosing one.
2. **The editor runs a real render thread by default on Windows** (`Single` on Linux). Do not reason
   as though main and render are the same thread — and make the code correct under both policies.
3. **Premake does not regenerate itself.** A new `.cpp` that isn't in the generated project shows up
   as an unresolved-external linker error, not a missing-file error.
4. **Renderer-visible scene state must go through `FrameRenderPacket`**, not a direct ECS read at
   submission time.
5. **A new component needs five edits, not one** — declaration, copy/duplicate, serialize,
   deserialize, editor UI.

---

## Step 4 — Write the code

Follow `Conventions.md` for the lines you touch; do not reformat the lines you don't
(`send-pr` §1). Match the surrounding density for logging and profiling instrumentation rather than
instrumenting everything or nothing.

If you discover mid-task that the right fix is larger than the request, say so and finish the
requested scope under a stated assumption — don't silently expand or silently narrow it.

---

## Step 5 — Hand off

When the code is written, tell the user to run `/cr` before committing. Do not commit, push, or open
a PR from `/dev`.

If the change altered a system boundary, interface, ownership rule, or integration point, update
`.claude/docs/Architecture-LuxEngine.md` in the same change — `CLAUDE.md` requires it.
