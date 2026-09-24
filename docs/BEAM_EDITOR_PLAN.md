# LuxEngine Beam Editor Plan

The phased design for reworking **Beam**, LuxEngine's in-editor text and code editor, into a fast,
well-made editor that brings the best ideas from Vim, VS Code, Visual Studio, and JetBrains IDEs into
the engine — without asking game makers to install anything else. This is a planning document, not a
description of what exists: `.claude/docs/Architecture-LuxEngine.md § 2.9 Editor` and
`docs/Editor/Panels.md § Beam` describe what is actually built.

*Written 2026-09-17 against `sound-fmod-va` (commit `a4c2bfe4` and later), Dear ImGui 1.92.6 WIP,
.NET SDK 9.0.316 / 10.0.x, vendored goossens `TextEditor` (pre-April-2026 API).*

---

## Goal card

- **Goal:** rework Beam into a complete, fast, refined code editor inside LuxEngine, with the features
  people rely on in Vim, VS Code, Visual Studio, and JetBrains IDEs.
- **User's words:** "keep it within the engine, I don't want people to have to install 500 software
  to make a game … I want to keep it small and refined."
- **Success:** a game maker can write, navigate, build, and fix C# scripts and shaders entirely in
  Beam — errors inline, jump to definition, find across the project, reload into the running editor
  — and never loses work. Typing stays instant.
- **Non-goals:** a general-purpose IDE; debugging (breakpoints, stepping); plugin/extension APIs;
  VCS UI beyond diffs; Vim script; editing engine C++ source as a first-class workflow.
- **Constraints:** ImGui stays (it is the UI toolkit). No new mandatory installs
  (`CLAUDE.md § Product Principle`). Editor-only — `Lux-Runtime` and Dist games are unaffected. Works
  on Windows and Linux, under both threading policies.

## Decisions this plan is built on

| Decision | Choice | Consequence |
|---|---|---|
| Editor core | **Update the vendored goossens `TextEditor` to upstream, then build on it** (user, 2026-09-17) | Folding, word wrap, and squiggles arrive with the update; Beam's code moves to the new API once, in Phase 0. Upstream fixes keep flowing. |
| C# intelligence | **Build errors first, then Roslyn in-process** (user) | Phase 5 needs no new dependency. Roslyn is gated behind a measured spike (Phase 6). |
| Vim | **Optional Vim mode, off by default** (user) | A modal input layer (Phase 9); needs a keyboard hook in the editor core. |
| Where the rule lives | **`CLAUDE.md` + `AGENTS.md`** (user) | Done alongside this plan: `CLAUDE.md § Product Principle`, `send-pr` rule 10, `plan-le` standing rules. |
| Language intelligence transport | In-process services, **no LSP servers** | LSP would require users to install servers ([research](#part-7--research-notes)). |
| Syntax engine | Keep the editor's hand-written, regex-free tokenizers; add YAML; structure from folding/indentation, Roslyn for C# | Tree-sitter rejected for now: generated parsers are multi-MB C per language, against "small". |
| Background work | A dedicated `Lux::Thread` per service, not `JobSystem::Submit` | `JobSystem::Submit` runs **inline** when there are no workers — i.e. on Linux's default single-threaded policy (`Threading.md § JobSystem`). |
| Multi-cursor vs go-to-definition click | Keep **Ctrl+click = add cursor**; **F12 / Alt+click** go to definition | Matches the editor's existing binding; avoids a regression for current users. |

---

## Part 0 — Where we are

Everything here was read in source this session; "verified" means seen working at runtime.

| Capability | State | Evidence |
|---|---|---|
| Multi-tab documents, find/replace, go to line, minimap, bracket matching, multi-cursor | ✅ Working (used daily) | `Editor/Source/Panels/TextEditorPanel.cpp` — `ConfigureEditor`, `UI_Tabs`, `UI_GoToLinePopup` |
| Monospace layout | ✅ Fixed 2026-09-16 | `a4c2bfe4` pushes the `Mono` font around `TextEditor::Render` |
| Ctrl+click on the current cursor | ✅ Fixed 2026-09-16 | `a4c2bfe4` — `TextEditor::Cursors::update` main/current index |
| Read-only diff view | ⚠️ Built, lightly used | `Core/Source/Lux/Vendor/TextDiff.{h,cpp}`, `TextEditorPanel::OpenDiff` |
| Dirty flag | ⚠️ Wrong after undo | Set by `SetChangeCallback` on any change; undoing back to the saved text stays dirty |
| Save | ⚠️ Not atomic | `SaveDocument` truncates with `std::ofstream`; a crash mid-write loses the file. `FileSystem::ReplaceFileAtomically` exists but is unused here |
| Save As / untitled | ❌ Missing | `SaveDocument` returns early for an empty path; `FileSystem::SaveFileDialog` exists |
| External change detection | ❌ Missing | No watcher in `Core/Source/Lux/Utilities/FileSystem.h`; `FileSystem::GetLastWriteTime` exists |
| Session restore (tabs, cursors) | ❌ Missing | Nothing persisted; editor prefs use `Application::GetSettings()` (`App.lsettings`) |
| Languages | ⚠️ Partial | `GetLanguageFromPath`: C/C++/C#/Lua/Python/GLSL/HLSL/JSON/Markdown/SQL. `.glslh`, `.slh`, `.hlslh` fall back to C++. **No YAML** for `.luxscene`/`.lmat`/`.luxproj` |
| Folding, word wrap, squiggles | ❌ Not in our copy | Vendored `TextEditor.h` predates upstream's April 2026 rewrite, which has `SetLineFoldingEnabled`, `SetWordWrapEnabled`, `AddSquiggle` |
| Autocomplete, markers, decorators, context menus | ⚠️ API present, unused by Beam | `TextEditor::SetAutoCompleteConfig`, `AddMarker`, `SetLineDecorator`, `Set*ContextMenuCallback` |
| Vim-style input hook | ❌ Missing | `TextEditor::render` always calls `handleKeyboardInputs()` (`TextEditor.cpp:200`) |
| Open from Content Browser | ⚠️ `.cs` only | `EditorLayer.cpp` — `RegisterItemActivateCallbackForType(AssetType::ScriptFile, …)` |
| Click a console error to open source | ❌ Missing | `ConsoleMessage` holds only `ShortMessage`/`LongMessage`/`Flags`/`Time` |
| C# build from the editor | ❌ Missing | `Project::ReloadScriptEngine` → `ScriptEngine::ReloadAppAssembly` loads a **prebuilt** DLL; `ScriptBuilder::BuildProject` (only called by runtime export) uses `std::system`, so compiler output is not captured |
| Process spawn with output capture | ❌ No helper | Only an inline `posix_spawn` in `VulkanShaderCompiler.cpp` (Linux `dxc` path) |
| In-process shader compile | ✅ Working (engine path) | `VulkanShaderCompiler` via shaderc; includes `Resources/Shaders/Include/{GLSL,Common}/`, global macros from `Renderer::GetGlobalShaderMacros()` |
| Shader stage splitting | ⚠️ Crashes on bad input | `ShaderPreprocessor::PreprocessShader` uses `LUX_CORE_VERIFY` on a malformed `#version`/stage pragma — unusable on half-typed text |
| Fuzzy matching | ⚠️ Exists, file-local | `FuzzyMatch` in `Editor/Source/CommandPalette.cpp` (subsequence + contiguity/word-start bonus) |
| Command palette | ✅ Working | `Editor/Source/CommandPalette.h` — `Register(Command)` |
| Roslyn available without installs | ⚠️ Present, untested | .NET SDK ships `Roslyn/bincore/Microsoft.CodeAnalysis.dll` (8.1 MB) + `…CSharp.dll` (18.4 MB); no `Workspaces`/`Features` assemblies. Coral exposes `HostInstance::CreateAssemblyLoadContext(name, dllPath)` |
| Bundled monospace fonts | ✅ | `Editor/Resources/Fonts/JetBrainsMono`, `SourceCodePro` |
| Global shortcuts while typing in Beam | ⚠️ Likely conflict (read, not verified) | `EditorLayer::OnKeyPressed` handles Ctrl+S/N/O/D/B/R and plain Q/W/E with no focus or `WantTextInput` guard (only Z/Y have one), and nothing marks key events handled for ImGui — so Ctrl+S in Beam probably also saves the scene, and W/E change the gizmo |

---

## Part 1 — Goals and non-goals

**Feature targets, by source of inspiration**

| From | Brought into Beam |
|---|---|
| **VS Code** | Quick Open (Ctrl+P), Go to Symbol (Ctrl+Shift+O), command palette integration, multi-cursor + add next occurrence, sticky scroll, breadcrumbs, Problems list, find in files with replace preview, snippets, folding, minimap, zoom |
| **Visual Studio** | F12 go to definition, Peek definition (Alt+F12), Ctrl+, "Go to All", Build (Ctrl+Shift+B) with an error list, signature help, quick info |
| **JetBrains** | Search Everywhere (Shift, Shift), Recent Files (Ctrl+E), Local History, Extend/Shrink Selection (Ctrl+W / Ctrl+Shift+W in JetBrains keymap), rename symbol, navigation back/forward |
| **Vim** | Modal editing: operators × motions × text objects, counts, `.` repeat, registers, marks, `q` macros, `/` search, an `:` command subset |
| **Lux only** | Save → reload that shader; save → build & reload C#; live shader errors from the engine's own compiler; click a console error to jump; asset UUID → name in YAML; engine snippets |

**Not planned:** debugger integration, plugins, Git UI, Vim script, remote editing, AI completion,
an embedded terminal.

**Performance budget** (verified with `/profile`, Release, focused window):

- Keystroke to redraw: no measurable frame-time increase on a 5 000-line file.
- Beam idle with 20 open tabs: < 0.2 ms main-thread CPU per frame when not focused.
- Find in files over the sample project: first results < 100 ms, UI never blocks.
- Background services never run on the main thread; the main thread only swaps in finished results.

---

## Part 2 — Design

### Layers

```
Editor/Source/Panels/TextEditorPanel.*     Beam panel: tabs, toolbar, status bar, popups (ImGui, main thread)
Editor/Source/Beam/                        NEW — editor-only services (no Core dependency on Editor)
    BeamDocument.*                         file I/O, encoding/EOL, dirty tracking, session state
    BeamWorkspace.*                        open documents, MRU, navigation history, session persistence
    BeamSearch.*                           project file index + find in files (worker thread)
    BeamDiagnostics.*                      diagnostic model shared by shaders and C#
    BeamShaderService.*                    in-process GLSL checking via shaderc (worker thread)
    BeamCSharpService.*                    build diagnostics; later Roslyn bridge (worker thread)
    BeamVim.*                              optional modal input layer
    BeamKeymap.*                           keybinding table + presets
Core/Source/Lux/Vendor/TextEditor.*        updated upstream editor core (+ documented Lux patches)
Core/Source/Lux/Utilities/FuzzyMatch.*     NEW — extracted from CommandPalette.cpp, shared
Core/Platform/{Windows,Linux}/…Process.cpp NEW — Lux::Process: spawn with captured stdout/stderr
```

Everything Beam-specific lives in `Editor/`, so `Core` never includes `Editor/Source/**`
(`send-pr` rule 9). The two `Core` additions (`FuzzyMatch`, `Process`) are general utilities with
engine uses (`CommandPalette`, `ScriptBuilder`, `AudioBankBuilder`-style tool launches).

### Threads

| Work | Thread | How results return |
|---|---|---|
| Rendering, input, tab state, Vim state | Main (ImGui) | — |
| File index, find in files | `BeamSearch` `Lux::Thread` | Mutex-guarded result batches with a generation number; main drains once per frame and discards stale generations |
| Shader checking | `BeamShaderService` `Lux::Thread` | Same mailbox pattern; input is an immutable text snapshot + include dirs + a copy of global macros taken on main |
| `dotnet build` | `BeamCSharpService` thread via `Lux::Process` | Parsed diagnostics posted to main; `ReloadScriptsWithFeedback` runs on main after success |
| Roslyn (Phase 7) | Same service thread, dedicated Coral ALC | Same; spike decides whether managed calls off-main are safe here |

No service touches ImGui, the ECS, the asset registry, or nvrhi. Correct under `SingleThreaded`
because services use their own `Lux::Thread`s rather than `JobSystem`.

### Data

- **Session** (open tabs, active tab, cursor, scroll, folds) — per project, in the project's editor
  user settings, not in `.luxproj` (avoids the settings-leak trap).
- **Hot exit / backups** — unsaved buffers mirrored to `FileSystem::GetPersistentStoragePath()` /
  `Beam/Backups/`, restored on next launch.
- **Local History** — per project under its cache folder, bounded by bytes.
- **Preferences** (tab size, wrap, minimap, keymap preset, Vim on/off, font size) —
  `Application::GetSettings()` keys `Beam.*`.

---

## Part 3 — Phases

Every phase: build Release, open the editor, run the listed checks, then `/cr`. Every phase with UI
also runs the **ImGui check** from `Conventions.md § ImGui correctness` — every `Begin*`/`Push*`/
`TreeNode*` closed on every path, no duplicate IDs (tabs keyed by a stable document ID, not the file
name; `###` for labels that change like `name *`), popup/window/dock names matched, and every new
UI state driven with no ID-conflict popup. Every phase that adds files regenerates projects.

### Phase 0 — Beam runs on the current upstream editor core

**Goal:** the vendored editor is upstream's current version; Beam behaves exactly as before, plus
folding and word wrap toggles.

**Changes**
- `Core/Source/Lux/Vendor/TextEditor.{h,cpp}`, `TextDiff.{h,cpp}` — replace with upstream
  `goossens/ImGuiColorTextEdit` at a pinned commit; add any new upstream files it needs. Record the
  commit and every local patch in a header comment block.
- Re-apply local patches only where upstream still needs them: the `Cursors::update` main/current
  index fix (check upstream first), `#include "lpch.h"`.
- `TextEditorPanel.cpp` — port to the new API (`size_t` lines, `DocPos`), keep the `Mono` font push,
  add View toggles for folding and word wrap.
- `docs/Editor/Panels.md` — note the upstream commit and new toggles.

**Playbook:** none. **Thread and lifetime:** unchanged (main thread).

**Verification**
- Build Release; regenerate projects if the file set changed.
- Open `DeferredLighting.glsl` and a `.cs` script: typing, undo/redo, multi-cursor, Ctrl+click on the
  cursor (the old crash), find/replace, go to line, diff view, minimap, fold/unfold, wrap.
- `/profile`: frame time with Beam focused on `DeferredLighting.glsl` and on a generated 20 000-line
  file — recorded as the baseline for the performance budget.

**Exit criteria:** feature parity with today plus folding/wrap; baseline numbers recorded.
**Rollback:** revert the vendor commit; the panel port is in the same commit.

### Phase 1 — Beam never loses work

**Goal:** saving is safe, dirty state is truthful, external edits are noticed, and a session
survives a restart or crash.

**Changes**
- **NEW** `Editor/Source/Beam/BeamDocument.{h,cpp}` — load with BOM/encoding and line-ending
  detection (preserve on save); save via temp file + `FileSystem::ReplaceFileAtomically`; dirty =
  undo index ≠ saved undo index (`TextEditor::GetUndoIndex`); last-write-time snapshot.
- **NEW** `Editor/Source/Beam/BeamWorkspace.{h,cpp}` — open documents keyed by a stable ID, MRU,
  session save/restore per project, hot-exit backups of dirty buffers every few seconds (off the
  frame path: only when changed, throttled).
- `TextEditorPanel` — **Save As** (`FileSystem::SaveFileDialog`), Save All, Close Others / Close
  Saved, "file changed on disk" bar (reload / keep mine / diff) checked on window focus and at a low
  polling rate via `FileSystem::GetLastWriteTime`, deleted-file state.
- `EditorLayer::OnKeyPressed` — skip scene shortcuts (Ctrl+S/N/O/D/B/R, Q/W/E) while text input
  owns the keyboard, so Beam's keys never also act on the scene. Start with the guard Ctrl+Z/Y already
  use (`ImGui::GetIO().WantTextInput`, which the editor core sets in `handleKeyboardInputs`); key
  events arrive between ImGui frames, so if that flag proves unreliable, the panel exposes its own
  "editor focused last frame" flag instead.
- `docs/Editor/Keybindings.md` — Ctrl+Shift+S, Ctrl+K S (save all), and the focus rule.

**Playbook:** Add a new editor panel — not applicable (panel exists); settings keys added to
`App.lsettings` only.

**Thread and lifetime:** main thread; backup writes are small and throttled. Documents owned by
`BeamWorkspace` (`Scope`), panel holds non-owning IDs.

**Verification**
- Kill the editor process mid-edit → relaunch restores the unsaved buffer.
- Edit a file in Notepad while open in Beam → the bar appears; each choice behaves.
- Undo to the saved state → tab loses `*`.
- Ctrl+S in Beam saves only the file (scene stays unmodified); typing `w`/`e` does not change the
  gizmo. First confirm the conflict exists before the fix, and note the result in Part 0.
- CRLF file stays CRLF after save; UTF-8 BOM preserved.
- Crash during save: repeatedly terminate `Editor.exe` while it saves a large file — the file on disk
  is always the complete old or new version, never truncated.
- ImGui check.

**Exit criteria:** all of the above. **Rollback:** the panel falls back to its current save path.

### Phase 2 — Every engine file reads well

**Goal:** correct highlighting and structure for every text format a game maker touches, with
sticky scroll and breadcrumbs.

**Changes**
- `TextEditorPanel::GetLanguageFromPath` — map `.glslh`, `.slh` → GLSL; `.hlslh` → HLSL;
  `.luxscene`, `.lmat`, `.luxproj`, `.lsettings`, `.yaml`, `.yml` → **NEW** YAML language; unknown →
  plain text (not C++).
- **NEW** YAML language definition (tokenizer in the editor's `Language` format; no regex).
- GLSL: engine macros (`__GLSL__`, stage macros, `Renderer::SetGlobalMacroInShaders` names) and
  shader built-ins as known identifiers. C#: `ScriptCore` public API names as known identifiers,
  extracted at editor start from `Resources/Scripts/ScriptCore.dll` metadata or from
  `ScriptCore/Source/Lux/*.cs` (decide in phase; both local, no install).
- Palette built from `Colors::Theme` (dark/light follow the editor theme).
- **Sticky scroll** (folding/indentation model) and **breadcrumbs** (file path › enclosing
  fold-region headers) drawn above the text.
- Indent guides, whitespace toggle, font zoom (Ctrl+wheel, Ctrl+0), persisted.
- Content Browser: add an "Open in Beam" action for text-based asset files (`.luxscene`, `.lmat`,
  and other YAML assets). Scenes and prefabs open **read-only by default** with an "Edit anyway"
  action, since the editor rewrites them on save. Engine shaders live in `Editor/Resources/Shaders/`,
  outside the project's Content Browser, so they are reached through **File → Open** (added here) and
  Phase 3's Quick Open.

**Verification:** open one file of each type, compare highlighting; scroll through a long shader and
check sticky scroll shows the enclosing function; open the currently loaded scene file and confirm
it is read-only; ImGui check.

**Rollback:** language mapping is one function; revert.

### Phase 3 — Find anything in two keystrokes

**Goal:** VS Code / JetBrains navigation over the whole project, never blocking a frame.

**Changes**
- **NEW** `Core/Source/Lux/Utilities/FuzzyMatch.{h,cpp}` — extract `FuzzyMatch` from
  `CommandPalette.cpp`; upgrade scoring with fzf-style bonuses (path separator, camelCase, `_`) and a
  gap penalty; return match positions for highlighting. `CommandPalette` uses the shared helper.
- **NEW** `Editor/Source/Beam/BeamSearch.{h,cpp}` — `Lux::Thread` with a project file index
  (respects an ignore list: `bin/`, `Cache/`, binaries, files > 5 MB); literal and regex find in
  files with case/word options; replace-in-files with a preview diff and a single undoable apply per
  file.
- Panel popups: **Quick Open** (Ctrl+P), **Recent Files** (Ctrl+E), **Go to Symbol in File**
  (Ctrl+Shift+O, from fold regions / known declarations), **Search Everywhere** (Shift, Shift: files +
  symbols + commands through `CommandPalette`), **Find in Files** results panel (Ctrl+Shift+F).
- **Navigation history** (Alt+Left / Alt+Right) and **bookmarks** (Visual Studio chords: Ctrl+K
  Ctrl+K toggle, Ctrl+K Ctrl+N next, Ctrl+K Ctrl+P previous).

**Thread and lifetime:** index built on the search thread from a directory walk; results posted with a
generation counter; the main thread drains at most N results per frame. Cancelling a query bumps
the generation.

**Verification**
- Quick Open ranks `PlayerController.cs` first for `plctl`.
- Find in files for a common word over the sample project: typing stays smooth, results stream in;
  record timings with `/profile` (Tracy zones around indexing/search).
- Replace across 3 files, then undo each.
- Switch threading policy to Single, restart, repeat — still non-blocking.
- ImGui check (result rows keyed by file + line, not index).

**Rollback:** popups are additive; remove registrations.

### Phase 4 — The engine talks to Beam

**Goal:** shader work never leaves Beam: live errors while typing, save reloads the shader, console
errors jump to the source.

**Changes**
- **NEW** `Editor/Source/Beam/BeamDiagnostics.{h,cpp}` — `{file, range, severity, code, message,
  source}`; renders squiggles (`TextEditor::AddSquiggle`), gutter markers, hover text, and a
  **Problems** list (click → jump).
- **NEW** `Editor/Source/Beam/BeamShaderService.{h,cpp}` — on edit (debounced ~300 ms), snapshot the
  text, split stages with a **non-asserting** splitter (never `ShaderPreprocessor`, which
  `VERIFY`-crashes on half-typed input), and compile each stage with shaderc using the engine's
  options (Vulkan 1.2, warnings as errors, include dirs, a main-thread copy of
  `Renderer::GetGlobalShaderMacros()`); parse `file:line: error:` output into diagnostics.
- Save of a shader → reload that shader only (`ShaderLibrary::Get` by the shader's name →
  `Shader::Reload`),
  with the result in the status bar.
- `EditorConsolePanel` — detect source locations when drawing messages (shader compile block
  `Shader:` + `Exact line:`, MSBuild `path(line,col)`, generic `path:line`) and make them clickable →
  Beam opens at the location. No change to `ConsoleMessage`'s layout.
- Go to include: F12 / Alt+click on an `#include "…"` resolves through the same include dirs.
- YAML: hover a 64-bit asset handle → asset name/type from the editor asset manager (main thread).

**Playbook:** none. **Thread and lifetime:** shader service owns its shaderc compiler instance; only
immutable snapshots cross threads. Live shaders are touched only by the save → reload action, which
goes through the existing `Renderer::Submit` path.

**Verification**
- Type a GLSL error → squiggle within ~0.5 s; fix → it clears; no crash while typing `#version` or
  `#pragma stage` partially.
- Save `DeferredLighting.glsl` → only that shader recompiles (log), viewport updates.
- Break a shader, click the console error → Beam opens at the line.
- No new validation errors; `/profile` shows no main-thread cost from the service.
- ImGui check.

**Rollback:** services are optional; disable registration.

### Phase 5 — Build C# from Beam, errors inline

**Goal:** Ctrl+Shift+B builds the project's scripts, shows compiler errors in place, and reloads the
assembly on success — no external IDE needed.

**Changes**
- **NEW** `Core/Source/Lux/Utilities/Process.h` + `Core/Platform/Windows/WindowsProcess.cpp`,
  `Core/Platform/Linux/LinuxProcess.cpp` — spawn with argument array, working directory, captured
  stdout/stderr (non-blocking reads), exit code, cancel.
- `Core/Source/Lux/Scripting/ScriptBuilder.{h,cpp}` — use `Lux::Process`; add a result struct with
  parsed MSBuild diagnostics (`path(line,col): error|warning CODE: message [project]`); keep the
  existing `bool BuildProject` for runtime export.
- **NEW** `Editor/Source/Beam/BeamCSharpService.{h,cpp}` — builds on its thread; on success posts to
  main, which calls `EditorLayer::ReloadScriptsWithFeedback` through a callback given to the panel at
  registration (the panel does not include `EditorLayer`).
- Optional "build on save" preference (off by default).
- Autocomplete baseline: `TextEditor::SetAutoCompleteConfig` with identifiers from the open document
  plus `ScriptCore` API names (Phase 2 list) via `TextEditor::Trie`.
- `docs/Editor/Keybindings.md`, `.claude/docs/Architecture-LuxEngine.md § 2.7 / 2.9` (new build
  entry point), `.claude/docs/Conventions.md § Helper reuse` (`Lux::Process`).

**Playbook:** Add a new thread or background job — `Lux::Thread`, `LUX_PROFILE_THREAD`, no GPU/ECS
work. Platform parity: Windows and Linux implementations land together.

**Verification**
- Introduce a C# error → build → squiggle at the right line, Problems list entry, toast shows
  failure; fix → build → toast shows success and the script runs in Play.
- Build while typing: UI stays responsive; cancel works.
- Same on Linux.
- ImGui check.

**Rollback:** `ScriptBuilder` keeps its old path behind the new function until verified.

### Phase 6 — Spike: Roslyn inside the editor, measured

**Goal:** a go/no-go with numbers for in-process C# intelligence, without installing anything.

**Changes (spike branch, not merged unless go)**
- **NEW** managed project `Editor/Beam.CodeAnalysis` (C#, net9.0) referencing
  `Microsoft.CodeAnalysis` + `Microsoft.CodeAnalysis.CSharp`.
- Load it through Coral in its own context: `HostInstance::CreateAssemblyLoadContext("BeamAnalysis",
  <dll path>)`, separate from `LuxScriptRuntime` so script reloads never unload Roslyn.
- Build a `CSharpCompilation` from the project's `.cs` files, `ScriptCore.dll`, and the
  `Microsoft.NETCore.App.Ref` reference assemblies found under the installed `dotnet/packs`.
- Measure: load time, working-set increase, full-project diagnostics time, incremental re-parse after
  one keystroke, `SemanticModel.LookupSymbols` completion latency.

**Questions the spike must answer**
1. Does Roslyn from the **installed SDK** load into Coral's .NET 9 runtime? The SDK Roslyn follows
   the SDK version (this machine has SDK 9.0.316 and 10.0.x); a machine with only SDK 10 may ship a
   Roslyn built for .NET 10. If it cannot load reliably, the fallback is shipping the
   `netstandard2.0` Roslyn assemblies with the editor (~26 MB judging by the SDK copies, MIT) — still no install, but a size
   cost that needs the user's approval.
2. May Coral calls into that context run on the service thread?
3. Are the numbers inside budget: completion < 50 ms warm, memory < 300 MB extra?

**Exit criteria:** a short results table appended to Part 7 and a user decision. **Rollback:** delete
the spike branch.

### Phase 7 — C# language service (if Phase 6 is go)

**Goal:** VS/JetBrains-grade C# editing for scripts.

**Changes**
- `BeamCSharpService` — Roslyn bridge on its thread: diagnostics as you type (debounced), completion
  (`LookupSymbols` at the caret, member access aware), **signature help**, **quick info** hover,
  **go to definition** (F12; into project source, or a generated read-only view of `ScriptCore`
  signatures), **peek definition** (Alt+F12, inline read-only view), **find references**
  (Shift+F12), **rename symbol** (F2, project-wide, previewed), document outline feeding
  Go to Symbol, breadcrumbs, and sticky scroll with real declarations.
- **Extend/Shrink Selection** from the syntax tree.
- Versioned snapshots: every request carries the document version; stale results are dropped.
- Not included: Roslyn formatting and code fixes (they live in `Workspaces`/`Features`, which the SDK
  does not ship) — formatting stays with Phase 8's lightweight rules.

**Verification:** completion on `Entity.`, `this.`, and inside lambdas; F12 into another script;
rename a component field across files and build; `/profile` confirms no main-thread spikes while
typing; ImGui check (completion list rows keyed by symbol ID).

**Rollback:** the service falls back to the Phase 5 baseline when Roslyn is unavailable.

### Phase 8 — Editing power

**Goal:** the everyday editing commands people miss when they switch editors.

**Changes**
- Duplicate line/selection, delete line, join lines, sort lines, transpose, move line (exists), box
  selection (Alt+Shift+drag) if the updated core supports it, add cursors to line ends, select all
  occurrences (exists).
- **Snippets** with tab stops and placeholders: engine-provided (new script component class,
  `OnCreate`/`OnUpdate`, GLSL stage template with `#version` + `#pragma stage`, compute shader
  skeleton), plus per-project user snippets in a small YAML file.
- **Format on save** (optional): trim trailing whitespace, final newline, indentation normalisation;
  honour a subset of `.editorconfig` (`indent_style`, `indent_size`, `end_of_line`,
  `trim_trailing_whitespace`, `insert_final_newline`).
- Context menus via `SetTextContextMenuCallback` / `SetLineNumberContextMenuCallback`.

**Verification:** each command on multi-cursor input; undo restores in one step; snippets tab through
placeholders; `.editorconfig` respected; ImGui check (context menus keyed per document).

**Rollback:** commands are independent; remove individually.

### Phase 9 — Vim mode

**Goal:** an optional, faithful core of Vim for people who think in it.

**Changes**
- `Core/Source/Lux/Vendor/TextEditor` — a minimal input hook so an outer layer can consume key events
  before `handleKeyboardInputs()` (upstream API if it exists after Phase 0; otherwise a documented,
  small local patch proposed upstream).
- **NEW** `Editor/Source/Beam/BeamVim.{h,cpp}` — state machine for normal, insert, replace, visual,
  visual-line, visual-block modes:
  - operators `d c y > < = gu gU g~`, doubled forms (`dd`, `cc`, `yy`);
  - motions `h j k l w W b B e E 0 ^ $ gg G f F t T ; , % { } H M L`, counts;
  - text objects `iw aw iW aW i" a" i' a' i( a( i[ a[ i{ a{ i< a< it at ip ap`;
  - `.` repeat, `u` / Ctrl+R, registers (`"a`–`"z`, `"0`, `"+` system clipboard), marks (`m` / `` ` ``
    / `'`), macros `q` / `@`, search `/ ? n N * #`;
  - `:` subset — `:w :q :wq :e <file> :%s/a/b/g :<line> :noh :set wrap/nowrap`.
- Status bar shows the mode and pending command; block cursor in normal mode.
- Preference `Beam.VimMode` (off by default), toggle in the command palette.

**Verification:** a scripted checklist of ~40 commands (e.g. `ci(`, `dap`, `3dw.`, `qa…q@a`,
`"+yy`) on a sample file, each compared with real Vim behaviour; Vim off → Beam behaves exactly as
before; ImGui check.

**Rollback:** preference off; the hook is inert when no layer is registered.

### Phase 10 — Local History

**Goal:** JetBrains-style safety net independent of Git.

**Changes**
- **NEW** `Editor/Source/Beam/BeamLocalHistory.{h,cpp}` — snapshot on save, before an external
  reload, and before replace-in-files; stored compressed per project, bounded by total bytes and age;
  files over 1 MB record only the fact of change.
- History panel for the active file: list by time and reason, diff against current with `TextDiff`,
  restore whole file or copy a hunk.

**Verification:** save 20 times → list shows entries; restore an old version; delete a file and
restore it from history; storage stays under its cap; ImGui check.

**Rollback:** feature is additive; disable snapshots.

### Phase 11 — Settings, keymaps, and hardening

**Goal:** Beam feels finished: configurable, fast, documented, and equal on both platforms.

**Changes**
- **NEW** `Editor/Source/Beam/BeamKeymap.{h,cpp}` — one command table; presets **Default**,
  **VS Code**, **Visual Studio**, **JetBrains**; user overrides persisted; conflicts reported in the
  settings UI; the command palette shows the live binding. Known clash to resolve per preset: Beam's
  Ctrl+W (close tab) is JetBrains' Extend Selection, and Ctrl+Tab stays reserved by ImGui window
  navigation.
- Beam section in Application Settings: tab size, spaces/tabs, wrap, minimap, sticky scroll, font
  and size, line spacing, format on save, build on save, Vim mode, keymap preset.
- Performance pass with `/profile` against the Part 1 budget (typing latency, 20 tabs idle, find in
  files, Roslyn warm/cold).
- Linux pass under the single-threaded policy.
- Docs: `docs/Editor/Panels.md`, `docs/Editor/Keybindings.md`,
  `.claude/docs/Architecture-LuxEngine.md § 2.9`.

**Verification:** switch each preset and exercise its signature bindings; budget table filled with
medians; Linux checklist; ImGui check.

**Rollback:** presets fall back to Default.

---

## Part 4 — Verification

- **Per phase:** the checks above, `/cr`, Release build, and the ImGui check.
- **End to end (after Phase 7, repeated after 11):** on a clean machine with only the documented
  editor prerequisites installed, create a script from a snippet, write code with completion, make an
  error, see it inline, fix it, build, press Play, break a shader, click the console error, fix it,
  save, see the viewport update, kill the editor, relaunch, and find every tab restored.
- **No-install audit:** list every binary Beam loads; each is vendored, part of the editor, or part
  of the already-required .NET SDK.

## Part 5 — Risks

| Risk | Likelihood | Detection | Mitigation |
|---|---|---|---|
| Upstream editor API churn breaks Beam in Phase 0 | Medium | Phase 0 build + checklist | Pin a commit; port once; keep patches listed |
| SDK Roslyn will not load into Coral's .NET 9 runtime | Medium | Phase 6 spike, first test | Ship `netstandard2.0` Roslyn (user approval for ~26 MB) or stay on Phase 5 |
| Coral calls off the main thread unsafe | Low–Medium | Phase 6 spike | Marshal Roslyn requests through a main-thread pump with time slicing |
| Background services starve the frame on low-core machines | Medium | `/profile` in Phases 3–5 | Throttle, cap per-frame drain, lower thread priority |
| Editing scene/prefab YAML while the editor owns it | Medium | Phase 2 test | Read-only by default; warn and reload scene on explicit edit |
| Vim hook requires a vendor patch that upstream rejects | Low | Phase 9 | Keep the patch tiny and documented |
| Large generated files slow the line-based buffer | Low for scripts/shaders | Phase 0 baseline | Warn and open read-only above a size threshold; revisit a piece-tree buffer only if measured |

## Part 6 — Open questions

1. Session state location: project user settings file vs `App.lsettings` keyed by project path?
   (Decide in Phase 1; must not touch `.luxproj`.)
2. `ScriptCore` API names for completion: read `ScriptCore.dll` metadata at start, or scan the C#
   sources? (Phase 2.)
3. If Phase 6 needs bundled Roslyn assemblies, is ~26 MB acceptable? (User, after the spike.)
4. Should "build on save" become the default once builds are fast? (After Phase 5 numbers.)
5. Split view of the **same** document needs a shared-document mode in the editor core; is it wanted
   enough to justify a vendor change? (Deferred; different files side by side can come with Phase 3.)

## Part 7 — Research notes

### Research brief — in-engine code editor
**Goal (from the Goal card):** a complete, fast, self-contained editor inside LuxEngine.

- **Prior art — buffer:** VS Code replaced its line array with a piece tree after a 35 MB file used
  ~600 MB; the trade-off is slower line lookup after many edits. Beam's files are small scripts and
  shaders, so the line-based core stays unless Phase 0's baseline shows a problem.
  [VS Code blog](https://code.visualstudio.com/blogs/2018/03/23/text-buffer-reimplementation)
- **Prior art — editor core:** the upstream editor Beam vendors now supports word wrap, VS Code-style
  folding, squiggles, and decorators (April 2026 rewrite), MIT licensed.
  [goossens/ImGuiColorTextEdit](https://github.com/goossens/ImGuiColorTextEdit)
- **Concept — C# without an install:** `Compilation.GetSemanticModel` + `SemanticModel.LookupSymbols`
  provide scope-aware completion using only the compiler assemblies, no `Workspaces` layer.
  [LookupSymbols](https://learn.microsoft.com/en-us/dotnet/api/microsoft.codeanalysis.semanticmodel.lookupsymbols?view=roslyn-dotnet-4.7.0)
  · [Semantic model](https://joshvarty.com/2014/10/30/learn-roslyn-now-part-7-introducing-the-semantic-model/)
- **Concept — shader errors in process:** glslang's `TShader::parse` + `getInfoLog` produce
  diagnostics without spawning a tool; the engine already links shaderc (which wraps glslang).
  [glslang](https://github.com/KhronosGroup/glslang)
- **Concept — fuzzy finding:** fzf scores with a modified Smith-Waterman and bonuses for word
  boundaries, path separators, and camelCase, tuned so ~8 characters of gap cancel a boundary bonus.
  [fzf algorithm](https://deepwiki.com/junegunn/fzf/2.2-fuzzy-matching-algorithm)
  · [fzy notes](https://github.com/jhawthorn/fzy/blob/master/ALGORITHM.md)
- **Concept — fast search:** ripgrep's speed comes from extracting literals to run a fast substring
  search before any regex, plus parallel directory walking with ignore rules.
  [ripgrep](https://burntsushi.net/ripgrep/)
- **Prior art — navigation:** VS Code sticky scroll can use outline, folding, or indentation models;
  breadcrumbs show the path and symbol chain. Beam starts with folding/indentation, upgrades to
  Roslyn declarations in Phase 7.
  [roboleary](https://www.roboleary.net/vscode/2023/11/19/vscode-sticky)
- **Prior art — safety:** JetBrains Local History records revisions independently of VCS and only
  notes changes for files over 1 MB.
  [JetBrains docs](https://www.jetbrains.com/help/idea/local-history.html)
- **Concept — Vim core:** the value is composition — operators × motions × text objects, with `.`
  repeat and registers; emulation layers diverge mostly in macros, registers, and Ex commands, so
  Phase 9 keeps Ex to a small subset. [Zed Vim docs](https://zed.dev/docs/vim)
- **Pitfall — file watching:** `ReadDirectoryChangesW` silently drops all events on buffer overflow
  and can lock directories; Phase 1 uses last-write-time checks instead of a watcher.
  [Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw)
  · [Jim Beveridge](https://qualapps.blogspot.com/2010/05/understanding-readdirectorychangesw_19.html)
- **Rejected — LSP servers** (OmniSharp, glsl_analyzer): best-in-class features, but each is a
  separate install. Conflicts with the Product Principle.
- **Rejected — tree-sitter:** excellent incremental parsing, but each grammar is a generated C parser
  of several MB (the C grammar alone is ~3.7 MB of source). Conflicts with "small".
  [tree-sitter](https://github.com/tree-sitter/tree-sitter)

**Informs decisions:** editor core, C# intelligence, syntax engine, file watching, search design.
**Still unknown:** SDK Roslyn load compatibility with Coral's runtime (Phase 6); upstream Vim input
hook (Phase 9).

---

**First phase to implement:** Phase 0 — update the editor core and port Beam. Implement with `/dev`,
review with `/cr`, ship with `/send-pr`.
