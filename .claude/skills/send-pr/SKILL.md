---
name: send-pr
description: PR-time gate for LuxEngine. Runs the full shared rule list against the branch diff, fixes what must be fixed, verifies the build, then creates the pull request. Use when the user asks to open a PR, send a PR, or says the work is ready for review.
---

# send-pr — LuxEngine pull-request gate

This skill owns **the shared rule list** used by `/dev` (preflight), `/cr` (local pre-commit), and
itself (PR time). The three differ only in when they run and what they are allowed to do:

| Skill | When | May commit? | May push / open PR? |
|---|---|---|---|
| `/dev` | before writing code | no | no |
| `/cr` | before committing | no | no |
| `/send-pr` | when the work is ready | yes | yes |

Shared context lives in `.claude/docs/`: `Conventions.md`, `Building.md`, `Threading.md`,
`Rendering.md`, `Architecture-LuxEngine.md`.

---

## Procedure

1. **Establish the diff.** `git status`, then `git diff master...HEAD` (or the merge-base with the
   branch this will target). Never review the whole tree — review what changed.
2. **Read the rule list below** and apply it to every changed hunk.
3. **Fix must-fix and should-fix findings** in place. Report consider-tier findings without fixing.
4. **Verify the build.** See "Verification" below. A PR that has not been compiled is not ready.
5. **Update the architecture doc** if the change altered a system boundary, interface, ownership
   rule, or integration point (`CLAUDE.md` requires this).
6. **Commit and open the PR** with a description covering what changed, why, and how it was
   verified.

Do **not** skip step 4 because the change "looks trivial". Premake does not regenerate
automatically; a new file that compiles on your machine may not even be in the generated project
(`.claude/docs/Building.md`).

---

## The rule list

Findings are tiered: **must-fix** (block the PR), **should-fix** (fix unless there is a stated
reason), **consider** (report only).

### 1. Scope discipline — must-fix

The diff contains the change that was asked for, and nothing else. No drive-by reformatting, no
renaming unrelated symbols, no "while I was here" cleanups, no reordering includes in files you
didn't otherwise touch. Bring the lines you *do* touch into conformance with
`.claude/docs/Conventions.md`; leave the rest.

If a file is uniformly wrong and worth fixing, that is its own change.

### 2. Root cause, not symptom — must-fix

A fix that suppresses a symptom (swallowing an error, adding a null guard where the null should be
impossible, widening a timeout, adding a `WaitIdle` to hide a race) is rejected. Find why the bad
state arises. If the real fix is out of scope, say so explicitly rather than shipping the bandage
silently.

### 3. No silent failure — must-fix

An operation that can fail must either handle the failure or report it. `catch (...)` that logs at
debug level and continues, a `bool` return that every caller ignores, or an early `return` on an
unexpected state with no log — all rejected. Use `LUX_CORE_ERROR_TAG` / `LUX_CORE_VERIFY`.

### 4. Threading contract — must-fix

Every new call must be legal on the thread it runs on. Check against `.claude/docs/Threading.md`:

- No ImGui outside the main thread (including inside a `Renderer::Submit` lambda).
- No nvrhi/Vulkan calls from the main thread outside `Renderer::Submit`.
- No ECS or asset-registry mutation off the main thread.
- `RT_`-prefixed functions only from the render thread.
- New threads use `Lux::Thread` and call `LUX_PROFILE_THREAD`.
- Code must be correct under **both** `ThreadingPolicy::MultiThreaded` and `SingleThreaded` — the
  editor defaults to Multi on Windows and Single on Linux.

### 5. Renderer invariants — must-fix in renderer paths

Apply the must-fix table at the end of `.claude/docs/Rendering.md` in full. The four that catch
people:

- A new `(set, binding)` colliding with a differently-named buffer in another shader.
- Pipeline / shader / descriptor-layout creation in a per-frame path.
- A GPU resource freed without `Renderer::SubmitResourceFree`.
- A new `PassDesc` / `TextureDesc` field not folded into `RenderGraph::ComputeStructureHash()`.

### 6. Memory and ownership — must-fix

No raw `new` / `delete`, no `std::shared_ptr` / `std::make_shared`. `Ref<T>` for `RefCounted`
engine objects, `Scope<T>` otherwise. Don't reorder or "simplify" the atomic decrement in
`Ref::DecRef`. Check for cycles: two `Ref`s pointing at each other never free — use `WeakRef`.

### 7. Component completeness — must-fix

A new or changed component must be handled in **all** of: `Components.h`, scene copy /
duplication / prefab instantiation, `SceneSerializer` (both directions), and the
`SceneHierarchyPanel` UI. Renderer-visible state must also reach `FrameRenderPacket`. See the
playbook in `.claude/docs/Architecture-LuxEngine.md § Part 4`.

### 8. Serialization compatibility — must-fix

A missing key must deserialize to the struct default. Do not rename or repurpose an existing key
without a migration path — old scenes and old `.luxproj` files must still load. Never silently drop
data on save.

### 9. Dependency direction — must-fix

`Core` must not include `Editor/Source/**`. Renderer must not reach into Physics or ScriptEngine.
Asset code must not assume the editor asset manager. If `Lux-Runtime` would no longer build, the
layering was violated. See the dependency table in `Architecture-LuxEngine.md § Part 1`.

### 10. Build-system hygiene — must-fix

New files: confirm project regeneration is required and say so in the PR body. New premake option:
one `newoption` + one `BuildOptions.OPTIONS` entry. New dependency: `Dependencies.lua` only. Don't
edit vendored submodules — reopen the project in `premake5.lua` the way Coral/NVRHI/Tracy are
handled.

### 11. Platform parity — should-fix

A behaviour added to `Core/Platform/Windows/` needs its `Core/Platform/Linux/` counterpart, or an
explicit note that Linux is unimplemented. Don't thread `#ifdef LUX_PLATFORM_*` through shared code
to avoid writing the second implementation.

### 12. Helper reuse — should-fix

Before adding a utility, check `Lux::FileSystem`, `Lux::Utils::String`, `Lux::ImGuiEx`,
`Colors::Theme`, `AssetManager`, `Project`. If an existing helper almost fits, extend it in its home
namespace rather than writing a variant at the call site.

### 13. Style conformance — should-fix

Per `.claude/docs/Conventions.md`: tabs, Allman braces, control-flow bodies on their own line,
unqualified names inside `namespace Lux`, `std::`-qualified C functions, named casts in new code,
`m_` / `s_` / `k_` prefixes, `RT_` only where the contract holds, file-scope declarations at the top,
`lpch.h` first in `Core` sources.

### 14. Logging quality — should-fix

Tagged macros (`LUX_CORE_*_TAG`) with an existing subsystem tag. No untagged logs in new code, no
log spam in per-frame paths, no logging of the same failure at two levels in two places.

### 15. Naming and magic values — should-fix

Names say what the thing is. Non-obvious literals get hoisted to a named `constexpr` at file scope
rather than sitting inline at the use site.

### 16. Dead code and comments — should-fix

No commented-out code blocks left behind. No comments restating the code. Comments explain *why*.
Existing `// NOTE(Name):` comments and the intentional DX11/DX12 scaffolding are **not** dead code —
leave them.

### 17. Performance in hot paths — should-fix

Per-frame allocations, `std::string` / `std::format` per draw or per entity, unreserved vectors
regrown every frame, `unordered_map` lookups in inner loops, copies of large structs by value. Add
`LUX_PROFILE_*` around meaningful new work.

### 18. Error messages — consider

Failure messages should name the thing that failed and, where possible, what to do about it.
`"Failed to load"` is not useful; `"Failed to load mesh {0} ({1}): file missing"` is.

### 19. Test / verification story — consider

State how the change was verified. For rendering work, that means "ran the editor, checked pass X in
the Renderer Debugger", not "it compiles". For `RenderGraph` compile/alias changes, run
`RenderGraph::RunValidationSelfTests`.

---

## Verification

Build the configuration the change plausibly affects — at minimum `Debug`.

```powershell
scripts\Win-GenProjects.bat --last
```

Then build from Visual Studio or MSBuild, and confirm by artifact timestamp, never by exit code:

```powershell
Get-Item 'bin\Debug-windows-x86_64\Editor\Editor.exe' | Select-Object Name, LastWriteTime
```

Invoke `.bat` / `.ps1` through the PowerShell tool, not Bash — MSYS mangles `.bat` arguments and a
no-op build can still report success. Full details in `.claude/docs/Building.md`.

If the change touches shaders, delete `Resources/Cache/Shader/` before testing; a stale cache looks
exactly like a broken shader.

---

## PR body

Include:

- **What** changed, in one or two sentences.
- **Why** — the underlying problem, not the diff restated.
- **Verification** — configs built, what was exercised at runtime.
- **Regeneration** — whether `Win-GenProjects` is required (new/removed/renamed files, premake
  changes).
- **Doc updates** — which `.claude/docs/` files were touched, or why none were needed.
- Any consider-tier findings deliberately left unfixed.
