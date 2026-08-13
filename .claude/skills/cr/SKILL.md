---
name: cr
description: Local pre-commit review for LuxEngine. Scans working-tree changes against the shared rule list, applies must-fix and should-fix repairs, and reports consider-tier nits. Local only — never commits, pushes, or opens a PR. Use before committing.
---

# cr — LuxEngine local pre-commit review

Reviews **working-tree changes** against the shared rule list and fixes what should be fixed.

**Hard boundary: this skill is local-only.** It must not `git commit`, `git push`, `gh pr create`, or
otherwise publish anything. If the user wants a PR, that is `/send-pr`.

---

## Procedure

### 1. Establish the diff

```bash
git status --short
git diff
git diff --staged
```

Review **only** what changed, plus enough surrounding context to judge it. Untracked files that are
part of the change count — check `git status` for them, since `git diff` won't show them.

If the working tree is clean, say so and stop.

### 2. Load the rule list and the relevant context

The rule list is `.claude/skills/send-pr/SKILL.md § The rule list` — rules 1–19, tiered must-fix /
should-fix / consider.

Load the docs the diff actually implicates:

- Always: `.claude/docs/Conventions.md`, `.claude/docs/Threading.md`
- `Renderer/`, `Platform/Vulkan/`, `Editor/Resources/Shaders/` → `.claude/docs/Rendering.md`
  (its must-fix table applies in full)
- New/removed/renamed files, premake edits → `.claude/docs/Building.md`
- System boundary changes → `.claude/docs/Architecture-LuxEngine.md`

### 3. Verify each finding before reporting it

A finding you cannot demonstrate is noise. For each candidate, state the concrete failure: the
input, state, or thread that produces the wrong result. If you cannot construct one, drop it to
consider-tier or drop it entirely.

Do not flag:

- Pre-existing issues in lines the change didn't touch (that is rule 1 in reverse — reviewing
  outside the diff produces the same churn the rule forbids).
- The intentional DX11/DX12 scaffolding, the `#undef CreateDirectory` block in `FileSystem.h`, the
  vendored-submodule workarounds in `premake5.lua`, or `// NOTE(Name):` comments.
- Style in vendored code under `Core/vendor/`.

### 4. Apply fixes

- **must-fix** — fix it.
- **should-fix** — fix it, unless doing so would expand the change's scope; then report it.
- **consider** — report only, do not touch.

Keep the fixes inside the change's existing scope. `/cr` must not turn a three-line fix into a
refactor.

### 5. Check the silent-failure playbooks

These are the LuxEngine changes that compile, run, and are still wrong. If the diff matches one,
verify every step:

| If the diff… | Verify |
|---|---|
| adds/changes a component | `Components.h` + copy/duplicate/prefab + `SceneSerializer` both directions + `SceneHierarchyPanel` + `FrameRenderPacket` if renderer-visible |
| adds a render pass or graph resource | pipeline built once in `Init()`, shader dependency registered, accurate `Reads`/`Writes`, folded into `ComputeStructureHash()` |
| adds a shader buffer | `(set, binding)` doesn't collide with a differently-named buffer anywhere in `Editor/Resources/Shaders/` |
| edits a `UB*` / `CB*` struct | the matching GLSL block was edited in the same change |
| adds an asset type | enum + `Asset` subclass + serializer + `AssetImporter` + `AssetExtensions` |
| adds files | project regeneration is called out |
| adds a thread or job | `Lux::Thread`, `LUX_PROFILE_THREAD`, GPU work via `Renderer::Submit`, no ECS/registry mutation |
| touches `Platform/Windows/` | the `Platform/Linux/` counterpart exists or is explicitly deferred |

### 6. Report

Findings first, most severe first. For each: file:line, what is wrong, the concrete failure, and
whether you fixed it. Then a one-line summary of what you changed.

Close by telling the user what still needs doing before commit — typically a build
(`.claude/docs/Building.md`), and project regeneration if files were added or removed.

Do not commit.
