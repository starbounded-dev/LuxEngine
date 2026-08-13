# LuxEngine Agent Instructions

This repository supports both Codex and Claude Code. `CLAUDE.md` and the files under `.claude/` are
shared project guidance despite their historical names. **Read `CLAUDE.md` completely before
substantive repository work and treat it as authoritative.**

LuxEngine is a C++20, Vulkan-only 3D game engine and editor, built with Premake5. It compiles as a
static library (`Core`), a C# scripting assembly (`ScriptCore`), an editor application (`Editor`),
and a standalone runtime player (`Lux-Runtime`). Everything lives in the `Lux` C++ namespace.

---

## Shared Guidance

- `.claude/docs/Conventions.md` — C++ style and the existing helper APIs. Read before writing or
  reviewing any C++.
- `.claude/docs/Threading.md` — thread contexts and the rules for each. Consult before changing
  threads, `Renderer::Submit`, the JobSystem, asset loading, events, or anything with an `RT_`
  prefix.
- `.claude/docs/Rendering.md` — renderer invariants. Required reading for anything under
  `Core/Source/Lux/Renderer/`, `Core/Source/Lux/Platform/Vulkan/`, or `Editor/Resources/Shaders/`.
- `.claude/docs/Building.md` — build commands, project-regeneration rules, and common failure
  modes.
- `.claude/docs/Architecture-LuxEngine.md` — system boundaries, ownership, lifecycle, extension
  playbooks. Consult the relevant section before changing an interface or an integration point, and
  update it when implementation changes make it stale, as `CLAUDE.md` requires.

### Three facts that catch agents out

LuxEngine descends from the same lineage as several other engines, and the reflexes carried over
from those are wrong here:

1. **Shaders are GLSL-first** (`Editor/Resources/Shaders/*.glsl`, `.glslh`), not HLSL-only.
2. **The editor runs a real render thread by default on Windows** (single-threaded on Linux). Main
   and render are *not* the same thread; code must be correct under both policies.
3. **Premake does not regenerate itself.** Adding a source file and building produces an
   unresolved-external linker error until `scripts\Win-GenProjects.bat` is re-run.

A fourth, specific to this engine: **`(set, binding)` is a global namespace shared by every
shader.** Reusing a slot with a differently-named buffer silently corrupts another pass's data.

---

## Workflow

- For substantive coding, use the repository `dev` skill once before editing.
- For a local pre-commit review, use the `cr` skill. It is local-only and must not commit, push, or
  create a pull request.
- Before creating a pull request, use `send-pr`. For the same checks *without* creating a PR, run
  its steps 1–4 and stop.
- Build and test in proportion to the change, using the commands and regeneration rules in
  `.claude/docs/Building.md`. Verify a build by artifact timestamp, never by exit code.
- Before editing files in a subtree, check for a closer `AGENTS.md`; its instructions augment or
  override this file.

---

## Engineering Posture

Write code that belongs in a serious long-term engine, not a demo.

- **Root cause over symptom.** A guard that hides a bad state, a widened timeout, or a `WaitIdle`
  that papers over a race is rejected. If the real fix is out of scope, say so rather than shipping
  the bandage silently.
- **No silent failure.** An operation that can fail either handles the failure or reports it via
  `LUX_CORE_ERROR_TAG` / `LUX_CORE_VERIFY`.
- **Validation errors are bugs.** Vulkan validation output is never noise and is never to be
  silenced, filtered, or `#ifdef`'d away — it is a real lifetime, layout, or synchronisation
  mismatch that will crash on some other driver.
- **Nothing expensive per frame.** Pipelines, shaders, descriptor layouts, and buffers are cached
  and invalidated, never rebuilt each frame.
- **Explicit ownership.** `Ref<T>` / `Scope<T>`; no raw `new`/`delete`, no `std::shared_ptr`.
- **`Core` stays usable without the editor.** `Lux-Runtime` building is the proof.
- **Complete features.** A feature that only works in one code path is not done: runtime behaviour,
  editor integration, serialization, and safe fallbacks all count.

The full, tiered rule list these expand into is `.claude/skills/send-pr/SKILL.md § The rule list`.

---

## Claude-to-Codex Translation

- A shared-document invocation such as `/dev` means the corresponding Codex repository skill,
  normally invoked as `$dev` or selected from the skill list.
- References to "Claude" as the acting coding agent apply to Codex too.
- Paths beginning with `.claude/`, `.agents/`, `docs/`, or `scripts/` in shared guidance are
  relative to the repository root unless stated otherwise.
- Claude-specific tool names describe the intended action. Use the Codex-native tool with equivalent
  behaviour and preserve all safety gates.
- `$ARGUMENTS` means the arguments supplied with the skill invocation.
- Build commands in shared docs are written for the PowerShell tool on Windows; use the Codex shell
  equivalent and keep the artifact-timestamp verification rule.

---

## Skill Maintenance

The complete skill bodies live under `.claude/skills/`. The Codex adapters under `.agents/skills/`
provide native discovery and translation **without duplicating the workflows** — they point at the
shared body so the two cannot drift. When adding, renaming, or removing a skill under
`.claude/skills/`, update the matching adapter in the same change.
