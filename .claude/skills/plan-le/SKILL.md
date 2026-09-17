---
name: plan-le
description: LuxEngine implementation planning. Turns a feature, refactor, or fix into a source-grounded, phased plan — a verified ledger of what exists, the decisions that belong to the user, how the work fits the engine's systems, threads, and ownership rules, phases that each build, run, and verify on their own, and the risks and open questions. Use before any multi-file or multi-session change, when the user asks for a plan, design, or roadmap, or when a task crosses a system boundary. Plans only; never writes engine code.
---

# plan-le — LuxEngine implementation planning

A plan is only useful if every sentence in it is true of this repository today. The failure mode
this skill exists to prevent is the confident plan built on assumptions — an API that doesn't
exist, a thread the code doesn't run on, a system that "just needs wiring" but was never working, a
phase that can't be verified until three phases later. Those plans cost more than no plan, because
they get trusted.

`/plan-le <goal>` produces a plan for that goal. With no argument, ask what to plan.

**Boundary:** this skill researches and writes a plan. It does not edit engine code, generate
projects, commit, or push. Implementation happens afterwards, phase by phase, through `/dev`, `/cr`,
and `/send-pr`.

**Scale the output to the task.** A two-file change gets a short plan in chat. A new subsystem gets
a full planning document. Do not pad a small plan to fill the template, and do not compress a large
one into bullet points that hide the decisions.

---

## Non-negotiables

1. **Grounded, not remembered.** Every statement about existing code cites a path and symbol you
   read *in this session*. Anything that does not exist yet is marked **NEW**. If you did not verify
   it, it goes under *Open questions*, not into a phase.
2. **The ledger is honest.** "Built" is not "working", and "working" is not "verified at runtime".
   Say which. A plan that overstates the starting point fails in phase one.
3. **Decisions belong to the user.** Where there is a real trade-off, present options with a
   recommendation and ask. Do not bury a product decision inside a phase.
4. **Every phase stands alone.** It compiles, the editor runs, it is verifiable by a concrete check,
   and stopping after it leaves the engine shippable. No phase depends on a later one to be
   testable.
5. **Engine rules are constraints, not suggestions.** Plans must respect
   `.claude/docs/Conventions.md`, `Threading.md`, `Rendering.md`, `Building.md`, and
   `Architecture-LuxEngine.md`. A step that violates one is a defect in the plan.
6. **Standing product rules:**
   - **No temporal rendering.** Never plan TAA, SMAA T2x, or temporal accumulation for GTAO, SSR,
     clouds, or anything else, including as an optimization. Buy quality spatially.
   - **Ray tracing, terrain, and DDGI/GI start from scratch.** Earlier attempts were deleted and
     never worked as intended. Do not plan to resume, port, or mine them.
   - **The `LUX_HAS_DX11` / `LUX_HAS_DX12` scaffolding is intentional**, reserved for a future
     backend. Do not plan to remove it.
7. **`docs/` is history, not truth.** `docs/*_PLAN.md` and `docs/RENDERER_PERF_BASELINE.md` are
   point-in-time documents. Read them for intent and prior decisions, and verify every factual claim
   against the source before relying on it.

---

## Step 1 — Frame the goal

Write down, before researching:

- **Goal** in one sentence, in the user's terms.
- **Success criteria** that can be observed: what the user will see, measure, or be able to do.
- **Non-goals**: what this plan deliberately leaves out.
- **Constraints** the user stated or that obviously apply (platforms, performance budget, deadline,
  backward compatibility with existing `.luxproj` / `.luxscene` / asset files).

If the goal is ambiguous in a way that changes the plan's shape — not its details — ask now, with a
recommended option first. Otherwise state your assumption and continue.

---

## Step 2 — Load the context

1. `CLAUDE.md`, then the shared docs the goal touches (the table in `CLAUDE.md` says which).
2. `.claude/docs/Architecture-LuxEngine.md`:
   - **Part 1**, *System dependency rules* — which systems the work may depend on.
   - **Part 2**, the section for each system involved.
   - **Part 4**, *Implementation Playbook* — components, asset types, render passes, editor panels,
     C# internal calls, threads and jobs, dependencies, build toggles. These are the changes that
     fail silently when a step is skipped; any phase that matches one must list every step.
3. The silent-failure table in `.claude/skills/cr/SKILL.md` § 5 and the rule list in
   `.claude/skills/send-pr/SKILL.md` — the plan should pass the review it will later get.
4. Any existing plan in `docs/` for the same area (as history — see non-negotiable 7).
5. Recent history in the area: `git log --oneline -- <paths>`, and `git log -S <symbol>` to find
   earlier attempts and why they changed.

---

## Step 3 — Establish the current state

Research until you can fill this ledger with evidence. Read the headers — they are the API's source
of truth — and grep for every symbol you intend to use or extend.

| Capability | State | Evidence |
|---|---|---|
| … | ✅ Working (verified how) / ⚠️ Built, unproven / ❌ Missing or broken | `path/File.h` — `Symbol`; or the observation |

Also find:

- **Existing helpers** that already do part of the job (`Conventions.md § Helper reuse`). A plan
  that reinvents one is wrong.
- **Integration points** the work must plug into, and their exact entry functions.
- **What must be measured** before a performance-motivated plan can be justified. If the premise is
  "X is slow", the first phase is a measurement with `/profile`, not an optimization.
- **Unknowns** you cannot resolve by reading. Each becomes either a spike phase or an open question.

---

## Step 4 — Fit it into the engine

For each new piece of the design, answer every row. "N/A" needs a reason.

| Concern | Question | Authority |
|---|---|---|
| System | Which system owns it? Which may it depend on? | Architecture Part 1 |
| Thread | Which thread does each new function run on, under **both** `MultiThreaded` and `SingleThreaded`? ImGui main-only; nvrhi via `Renderer::Submit`; no ECS or asset-registry mutation off the main thread | `Threading.md` |
| Ownership | `Ref<T>` or `Scope<T>`? Who frees GPU resources, and through `Renderer::SubmitResourceFree`? Does a mid-session teardown (scene switch, subsystem toggle) free anything an in-flight frame may still reference? Then the plan must drain the GPU first — there is no dedicated helper for this today, so name the mechanism | `Conventions.md`, `Rendering.md` |
| Renderer data flow | Does the renderer read it? Then it enters through `FrameRenderPacket`, not an ECS read at submission | `Rendering.md` |
| Renderer invariants | New `(set, binding)` grepped for collisions? Pipelines created once in `Init()`? New `PassDesc` / `TextureDesc` fields folded into `ComputeStructureHash()`? Accurate `Reads` / `Writes`? Feature-gated to zero cost when off? | `Rendering.md` |
| Serialization | Saved in the scene, the project, user preferences, or an asset? Do **existing files still load**? What is the migration for old `.luxproj` / `.luxscene`? | Architecture 2.13 |
| Editor | Panel, inspector UI, undo, and whether settings leak into `LuxSample.luxproj`. For every UI phase, plan the ImGui verification: scopes closed on every path, unique IDs per scope (stable `PushID` in loops, `###` for labels that change), matching popup/window/dock names, and each new UI state driven in the editor | Architecture 2.9, `Conventions.md § ImGui correctness` |
| Scripting | Does it need a C# API (`ScriptCore` + `ScriptGlue` internal calls)? | Architecture 2.7 |
| Runtime and Dist | Does it work in `Lux-Runtime`, in Dist (no shader compiler, no Tracy), and after runtime export (asset packs, `ShaderPack.lsp`)? | `Building.md` |
| Linux | Does a `Platform/Windows/` change have a `Platform/Linux/` counterpart? Remember Linux defaults to single-threaded | `Threading.md` |
| Build | New files → project regeneration. New dependency → `Dependencies.lua`. New toggle → `BuildOptions.OPTIONS` + `newoption` | `Building.md` |
| Docs | Which shared doc becomes stale, and must be updated in the same phase? | `CLAUDE.md` |

---

## Step 5 — Surface the decisions

List every choice with a real trade-off:

| Decision | Options | Recommendation | Consequence |
|---|---|---|---|

Ask the user about the ones that are theirs — product behaviour, scope, compatibility, quality vs
cost, what to delete. Ask at most four at a time, recommended option first. Decide the purely
technical ones yourself and record the reasoning in the table so it can be inspected.

---

## Step 6 — Design the phases

Order phases to retire risk early:

1. **Measure or spike first** when the plan rests on an unverified assumption — the smallest
   experiment that proves or kills it.
2. **Then the thinnest vertical slice** that works end to end in the running editor.
3. **Then breadth** — the remaining cases, UI, serialization, scripting, runtime parity.
4. **Then hardening** — edge cases, Linux, Dist, performance budget, docs.

Each phase uses this shape:

```markdown
### Phase N — <outcome, not activity>

**Goal:** what is true after this phase that wasn't before.

**Changes**
- `path/Existing.cpp` — `Symbol`: what changes and why.
- `path/NewFile.h` — **NEW**: what it holds.

**Playbook:** <Architecture Part 4 entry, with every step listed> or "none applies".

**Thread and lifetime:** which thread each new call runs on; who owns and frees what.

**Verification**
- Build: config(s), and project regeneration if files were added.
- Run: the exact steps in the editor (or runtime) and what must be observed.
- Log: what must appear, and what must not (validation errors, binding collisions).
- Numbers: for performance work, the `/profile` protocol and the target.

**Docs:** which of `.claude/docs/*` is updated in this phase.

**Exit criteria:** the checks that make this phase done.

**Rollback:** how to back it out if it fails.
```

"Update X" is never a complete step. Say what changes in X.

---

## Step 7 — Risks and open questions

- **Risks:** what could make a phase fail or force a redesign, how likely, and how the plan detects
  it early.
- **Open questions:** everything you could not verify, each with how to resolve it (a spike, a
  measurement, or a user decision).
- **What would invalidate the plan:** the assumption that, if wrong, sends you back to Step 3.

---

## Step 8 — Check the plan before presenting it

Go through this list and fix the plan, not the list:

- [ ] Every existing path and symbol was opened or grepped this session; new ones are marked **NEW**.
- [ ] The ledger says what was verified at runtime versus only read.
- [ ] Every phase compiles, runs, and verifies on its own, and leaves the engine shippable.
- [ ] Every phase that matches a Part 4 playbook lists all of its steps.
- [ ] Every new call has a thread, and is correct under both threading policies.
- [ ] GPU resource lifetime and scene-transition teardown are addressed where relevant.
- [ ] Existing projects, scenes, and assets still load, or a migration is planned.
- [ ] Runtime, Dist, export, and Linux were each considered.
- [ ] Every phase with ImGui code has a verification step for closed scopes, unique IDs, and every
      new UI state exercised in the editor.
- [ ] Doc updates are scheduled in the phase that makes them stale.
- [ ] No temporal techniques; no resumption of the deleted ray tracing, terrain, or GI work.
- [ ] Nothing outside the stated goal crept in. Anything worth doing but out of scope is listed as
      a follow-up, not smuggled into a phase.

---

## Step 9 — Deliver

**Where it goes**

- **Small plan** (one or two phases): in the chat.
- **Plan mode active:** present the plan through plan mode for approval.
- **Large or multi-session plan:** offer to write `docs/<AREA>_PLAN.md`, following the existing
  `docs/AUDIO_SYSTEM_PLAN.md` convention. It is a planning document, not architecture — the
  authority for what exists stays `.claude/docs/Architecture-LuxEngine.md`.

**Shape of a full plan document**

```markdown
# LuxEngine <Area> Plan

One paragraph: what this plan achieves and for whom. State that it is a planning document, and
point at the Architecture section that describes what is actually built.

**Decisions this plan is built on**
| Decision | Choice | Consequence |

## Part 0 — Where we are        (the ledger, with evidence)
## Part 1 — Goals and non-goals (success criteria, constraints)
## Part 2 — Design              (systems, data flow, threads, ownership — Step 4)
## Part 3 — Phases              (Step 6 shape, one section per phase)
## Part 4 — Verification        (how the whole thing is proven, beyond per-phase checks)
## Part 5 — Risks
## Part 6 — Open questions
```

**Close by** naming the first phase to implement and handing off: implement it with `/dev`, review
with `/cr`, and ship with `/send-pr`. Offer to start Phase 1; do not start it unasked.
