# LuxEngine Editor Remaster Plan

**Phase 4 delivery checkpoint (2026-11-04):** Profiler and Renderer Debugger now share a bounded
240-sample timing chart with explicit CPU sources, milliseconds, missing-GPU gaps, pause/resume,
follow, inspection, fit, legend toggles and budget lines. Per-pass GPU plots own frozen labels/values
and use readable wrapped rows in narrow panels. Workspace docking now resolves actual ImGui window
titles; legacy display-name Profiler placeholders migrate without changing the panel's window ID.

Verified: diagnostic plots, workspace lifecycle and panel/Beam tests passed under Single and Multi
at `bin/diagnostic-plot-smoke/1793842341946247900`,
`bin/workspace-smoke/1793842362141368700` and `bin/panel-controls-smoke/1793842393481602400`.
Fresh artifacts: Debug Editor `20:31:51`, Dist Editor `20:35:21`, Dist Runtime `20:39:02` local.
Native private-project verification at `bin/diagnostic-phase4-ui/1793842032382` confirmed the
repaired Profiler dock, real CPU/GPU samples and inspection tooltip at Windows 150% scaling.
Dist missing-GPU behavior has deterministic chart coverage and a successful Dist build, but no
native Dist session yet. Linux, mixed-monitor transitions and remaining full-matrix work remain
in Phase 6. Local review found no remaining blocking findings. Sample-project edits are excluded.

**Planning snapshot: 2026-11-04, `fac11e76`.** Remaster the existing editor's panels and interactions
so scripting, material authoring, node graphs, plots and everyday scene work feel coherent inside
the new workspace system. This is an implementation plan, not a record of completed work. The
current architecture remains `.claude/docs/Architecture-LuxEngine.md`, especially sections 2.9
and 2.3. Workspace intent and its implementation record are in `docs/EDITOR_WORKSPACES_PLAN.md`.

## Goal card

- **User's goal:** "the text editor, materials, graphs, plots, the rest of workspace, another remaster of the editor."
- **Outcome:** coherent, readable tools with clear actions, useful space allocation and consistent navigation.
- **Success:** existing workflows remain usable at narrow panel widths, on Full HD and 4K, across workspace changes.
- **Scope:** visual and interaction redesign, plus targeted fixes to existing tools; user confirmed.
- **Order:** shared panel controls, then text editor and material/graph tools; user confirmed.
- **Constraints:** preserve projects, assets, dirty documents, undo, existing workspaces and engine/runtime boundaries.
- **Non-goals:** new modeling/sculpting/UV/animation systems, a replacement GUI framework or a full external-style IDE.
- **Delivery:** independently usable phases, with concrete runtime verification before moving on.

## Decisions and working assumptions

| Decision | Choice and basis | Consequence |
|---|---|---|
| Remaster depth | User: visual/interaction redesign and targeted existing-tool fixes | Address information layout and behavior, not only colors. Broad new feature sets need separate scope. |
| Starting point | User: shared controls, then text editor/materials/graphs | Prove shared components in a real tool before spreading them across the editor. |
| Durable plan | User approved this file | Update phase completion with evidence; do not confuse proposals with built architecture. |
| Visual identity | Working assumption: refine the current `Colors::Theme` warm graphite/lime identity | No unrequested full palette or branding replacement. Establish typography, contrast and density before decoration. |
| Dependencies | Reuse ImGui, ImPlot, TextEditor and imgui-node-editor already present | No new mandatory software, web frontend or dependency upgrade in this plan. |
| Tool composition | Start with adaptive views inside existing panels | Independent graph/preview/inspector dock windows are deferred until a shared document/view design is approved. |
| Workspace semantics | Keep protected Defaults and autosaved Personal layouts | A visual update cannot reset saved arrangements or turn workspace switching into document switching. |
| Plot meaning | Start with existing diagnostic/time-series plots | Animation curves and other missing editor families stay in the original Part Two roadmap. |

## Part 0 — Current-state ledger

Source was inspected during this planning pass. These rows establish implementation presence and
specific layout risks; they do not claim every feature was driven in the editor. Earlier workspace
build/UI evidence is recorded in the workspace plan. This pass makes no new build or runtime claim.

| Area | Observed state | Evidence read this pass |
|---|---|---|
| Workspaces | Implemented in the preceding change; broad platform acceptance still open | `Editor/Source/Workspace/EditorWorkspaceManager.h`: `DrawTabs`, `DrawPanelsMenu`, `BeforeFrame`; Architecture 2.9 and workspace implementation record. |
| Panel lifecycle | Existing visibility, project/scene context and presentation-state contract | `Core/Source/Lux/Editor/PanelManager.h`: `CaptureState`, `ApplyState`, `SetOpen`; Architecture 2.9 describes `EditorPanel` hooks and main-thread dispatch. |
| Theme and scaling | Existing common tokens and dynamic DPI baseline | `Core/Source/Lux/ImGui/Colors.h`: `Theme`; `ImGuiScaling.h`: `DpiStyle`, `ScopedWindowScale`. Handwritten tool geometry still needs a panel-level audit. |
| Shared widgets | Reusable search, property sections and numeric controls exist | `ImGuiWidgets.h`: `Widgets::SearchWidget`; `ImGuiEx.h`: `PropertyGridHeader`; `ImGuiUtilities.h`: `NumericControlMenu`. Extend suitable helpers instead of duplicating them. |
| Beam text editor | Existing per-file documents, tabs, dirty-close confirmation, shortcuts, status bar and diff mode | `Editor/Source/Panels/TextEditorPanel.h/.cpp`: `Document`, `UI_Toolbar`, `UI_Tabs`, `UI_CloseConfirm`, `OnImGuiRender`. Toolbar sizes/gaps include fixed values; shell is largely local styling. |
| Text implementation | Vendored syntax editor and text diff already underpin Beam | `Core/Source/Lux/Vendor/TextEditor.h` and `TextEditorPanel.h`: `TextEditor`, `TextDiff`. This plan does not assume semantic C# completion/debugging exists. |
| Materials | Existing document tabs, explicit Save/Revert, dirty tracking, preview, instances and graph mode | `MaterialEditorPanel.h/.cpp`: `Document`, `MaterialState`, `UI_Properties`, `UI_Instance`, `TrackEdit`, `OnImGuiRender`. Preview width is clamped to a minimum/maximum inside the panel. |
| Material graph | Existing node canvas, palette, search, minimap, inspector, clipboard, undo snapshots and node preview | `MaterialGraphCanvas.h/.cpp`: `Draw`, `DrawToolbar`, `DrawCanvas`, `DrawInspector`, `TrackChanges`, `UpdatePreview`. `Draw` reserves a 280-unit inspector plus a minimum canvas width. |
| Graph data | Existing material-specific model independent of ImGui | `Core/Source/Lux/Renderer/MaterialGraph/MaterialGraph.h`: `MaterialGraphNode`, `MaterialGraphLink`, `MaterialGraphType`. Reuse this model; a visual remaster is not a new graph evaluator. |
| Preview lifetime | Existing dedicated preview scene/viewport | `MaterialPreview.h`: `Render`, `SetMaterial`, `Orbit`, `Zoom`; rendering is queued through existing renderer ownership. |
| Profiler plot | Hand-drawn frame history with its own legend, budget line and sizing | `ProfilerPanel.h/.cpp`: `UI_Graph`, `PushFrameSample`; `UI_Graph` builds local point vectors. This is a source observation, not a measured bottleneck. |
| Renderer charts | ImPlot frame history and per-pass GPU bars already exist | `RendererDebuggerPanel.h/.cpp`: `DrawFrameHistoryPlot`, `DrawPassGPUChart`; history axes currently follow frame/sample indices, and axis limits are set during drawing. |
| Audio tools | Recently refined tool-specific authoring flows should be retained | `SoundEditor/SoundEditorPanel.h`: documents and audition cleanup; `AudioMixerPanel.h`: live edits, Apply/restart rules, project/scene ownership. History: `ecd0fd1a` refined audio panels/shared controls. |
| Remaining editor | Existing browser, console and settings are integration targets, not presumed broken | `ContentBrowserPanel.h`: `OnImGuiRender`; `EditorConsolePanel.h`: `RenderMenu`, `RenderConsole`; `ApplicationSettingsPanel.h`: `EditorPreferencesBindings`. Their detailed defects require the runtime inventory below. |
| Vendored versions | Verified locally | `Core/vendor/imgui/imgui.h`: 1.92.6 WIP; `Core/vendor/imgui/implot/implot.h`: 0.18 WIP; `Core/vendor/imgui-node-editor/imgui_node_editor.h`: version macro 0.10.0. |

Recent history confirms existing investment in these tools: `ecd0fd1a` refined authoring controls;
`a796fdb2` added graph-node preview. Preserve those capabilities. The previous workspace plan already
separated optional material subviews from future creative editors; this remaster does not silently
expand its Part Two roadmap.

## Part 1 — Product and interaction design

Use a consistent anatomy where it helps: document tabs for document tools, a compact action area,
the main editing surface, and contextual status. Single-purpose utility panels need not acquire
empty toolbars or duplicate titles. Panel tabs already name the tool.

- Common text hierarchy, spacing, field alignment, focus/hover/disabled states and semantic status colors.
- Primary actions stay reachable. Secondary actions move into a labeled overflow menu as space shrinks.
- Long names truncate with full-name/path access; tooltips and shortcuts explain unfamiliar icons.
- Empty, loading, invalid and failed-save states explain the next useful action. A failed save leaves
  the document dirty and the user's work available.
- Tool sidebars can collapse or use an alternate arrangement in narrow regions; content width, not
  a guessed screen-resolution profile, determines the presentation. User choices survive resizing.
- Canvas navigation is local to the canvas; text shortcuts are local to the text editor. Typing,
  numeric editing, popups and dialogs must prevent accidental scene actions or graph deletion.
- Save, Revert and live preview keep their tool's real semantics. Sound Banks currently save edits
  immediately; a common visual language must not imply they use the material editor's save model.
- Do not use color alone for errors, selections or chart series; retain readable labels and focus cues.

The first implementation produces native, reviewable examples of shared panel controls and a Beam
toolbar at wide/narrow widths. Those examples establish the visual target for later panels. No
full application rebuild is needed to review a static image, but final acceptance uses the editor.

## Part 2 — Engine fit, ownership and persistence

| Concern | Design constraint |
|---|---|
| System boundary | Generic, model-independent UI primitives live with `Lux::ImGuiEx`. Editor-specific composition and document actions stay in `Editor/Source`. Core never includes editor-app headers. |
| Main/render threads | All ImGui, node-editor, ImPlot, focus and workspace changes run on main under both policies. Renderer statistics come from the existing published data; no UI reads live render-thread containers. |
| Ownership | Keep `Scope<Document>` in Beam and the existing material/sound document owners. Layout/view state does not own authored data. Use `Scope`/`Ref`; no extra panel/document copies just to show the same asset. |
| GPU preview lifetime | Reuse `MaterialPreview` and its viewport/renderer. Hiding/reflowing a preview changes view scheduling, not resource ownership. If teardown changes, audit in-flight references and use the established render submission/resource-free path; name and verify any required drain at that transition, not every frame. |
| Renderer data | No new renderer data flow is required for panel styling. If an approved targeted fix changes authored material/scene values, retain their existing propagation/packet path. No direct ECS read at render submission. |
| Rendering invariants | No new pass, shader buffer or pipeline is planned. If scope later requires one, apply the complete renderer playbook: global bindings, Init-time caches, reads/writes, structure hashes and lifetime rules. No temporal rendering. |
| Serialization | Presentation choices use existing panel `SaveState`/`LoadState` hooks and the workspace owner where appropriate; editor-wide appearance belongs in user preferences. Source text/material/sound data retain their file/asset owners. Missing presentation keys use safe defaults. |
| Workspace compatibility | Preserve existing panel IDs/window names, Default/Personal behavior, native docking and legacy imports. A hidden panel keeps dirty documents. Save positions/selection/zoom as view state only after deciding whether the scope is per-document, per-workspace or personal. |
| Undo | Document edits use existing text/material/sound undo. View-only actions and resizing do not dirty assets or create scene undo steps. |
| Scripting | No new ScriptCore API or mandatory language-server process. Semantic completion, debugger integration and project-wide refactoring require their own design. |
| Runtime/Dist/export | Shared widgets remain usable without the editor. Build Lux-Runtime; prove exported authored results where a phase changes asset behavior. Diagnostic UI must handle absent Dist instrumentation without fabricated zeros or unsupported controls. |
| Linux | Keep existing platform policy; do not enable detached platform viewports to copy Windows behavior. Verify text input, shortcuts, fonts and sizing on Linux. |
| Build/dependencies | New source files require project regeneration. Reuse current vendored versions. Any later dependency must go through `Dependencies.lua`, license/size/startup review and an explicit scope decision. |
| Documentation | Update Architecture 2.9 when interfaces/ownership change, Conventions when a helper becomes canonical, Threading/Rendering when those contracts change, and bundled help when user actions move. |

**NEW, deliberately small:** a reusable panel action/layout helper in the existing ImGui helper
area, driven by immediate-mode calls and caller-owned state. The exact file/API is settled during
the pilot after comparing the existing helpers. It owns no document, asset, workspace or global
selection. It must solve at least the Beam and material toolbar cases before being generalized.

**NEW:** a small shared plot presentation helper only after comparing the two existing chart paths.
It covers colors, readable axes/units, legends and interaction conventions; existing owners retain
their samples, history and instrumentation. Do not build a generic analytics subsystem.

## Part 3 — Implementation phases

### Phase 0 — Agree on a concrete visual target and baseline

**Goal:** an evidence-backed inventory and wide/narrow native panel examples define completion.

**Changes:** inspect the running Beam, material/graph, Profiler/Renderer Debugger, audio, browser,
Hierarchy/Properties, settings, console and workspace flows. Record the actual defect, reproduction,
intended behavior and priority for each; do not label every existing panel broken from source alone.
Use an isolated user directory and disposable project. Record present sizes, DPI and representative
content. Keep the current theme as the starting point and propose specific hierarchy/density changes.

**Thread/lifetime/playbook:** read-only main-thread UI observation; no new system or resource.
**Verification:** baseline Debug launch and representative workflows, logs free of validation errors;
capture current behavior at 100%/150% and wide/narrow widths. Performance claims require `$profile`
with a fixed scene/protocol before any optimization target is set.
**Docs:** evidence/acceptance inventory in this plan; no architecture change yet.
**Exit/rollback:** concrete examples and prioritized defects are reviewable; stopping changes no engine behavior.

### Phase 1 — Shared controls demonstrated in Beam

**Goal:** one polished, usable panel establishes the common visual and interaction language.

**Changes:** extend appropriate helpers in `ImGuiWidgets.h`, `ImGuiUtilities.h` or a **NEW** focused
ImGui helper file; reuse `Colors::Theme`, fonts, numeric menu and property-grid helpers. Migrate
`TextEditorPanel::UI_Toolbar` and its empty/action states first. Make actions adapt to width, preserve
stable widget IDs and keyboard access, and expose overflow actions rather than clipping them.
Do not migrate the whole editor in this phase.

**Thread/lifetime/playbook:** main-only UI; Beam keeps its existing document owners. New helper files
require project regeneration; no new panel registration, thread or GPU resource.
**Verification:** fresh Debug Editor and Lux-Runtime builds; create/edit/save a disposable file,
invoke the toolbar by mouse and keyboard, shrink/expand it and switch workspaces with a dirty file.
Check all Begin/End/Push/Pop paths and unique IDs, including overflow/popups. Compare against Phase 0.
**Docs:** Conventions for reusable controls; Architecture 2.9 if an interface is added; editor help.
**Exit/rollback:** Beam's pilot works without changing other panels; helper adoption can be reverted locally.

### Phase 2 — Beam feels complete as an existing text tool

**Goal:** document navigation, editing and save feedback remain clear under realistic content.

**Changes:** refine `TextEditorPanel::UI_Tabs`, `UI_StatusBar`, `UI_CloseConfirm`,
`UI_GoToLinePopup` and `OnImGuiRender`. Audit focus, diff/editor switching, find/replace, line-number
readability and text zoom using the vendored editor's actual APIs. Disambiguate same-name files;
keep active/modified state legible with many tabs. Targeted fixes come from Phase 0 reproductions,
including any pending close dialog obscured by mode changes. Reuse syntax/diff implementation;
semantic IDE features are not assumed or promised.

**Thread/lifetime/playbook:** main only; stable per-document editor addresses and existing callbacks;
no new panel/thread/asset. Preferences and view state stay separate from file contents.
**Verification:** Debug build/run; many files and duplicate basenames, Unicode paths, long lines,
read-only/unwritable destination, Save As cancellation, Save/Discard/Cancel, diff mode, search and
workspace hide/show with dirty text. Shortcuts must not trigger scene commands. Exercise every added
popup/ID scope at both DPIs. Add regression tests for reproducible data-loss/focus bugs, not cosmetic constants.
**Docs:** bundled text-editor help and Architecture 2.9 if document/view responsibilities change.
**Exit/rollback:** text workflows pass independently; revert the shell changes without altering source files.

### Phase 3 — Materials and node graphs use the available space

**Goal:** author a material with a useful preview, editing surface and contextual details at both widths.

**Changes:** adapt `MaterialEditorPanel::OnImGuiRender`, `UI_Toolbar`, `UI_Preview`, `UI_Properties`
and `MaterialGraphCanvas::Draw`. Replace competing fixed internal columns with resizable/collapsible
regions and a narrow alternative. Keep the graph canvas usable when preview/details are hidden.
Refine node/pin typography, selection and link contrast, palette/search, Fit/navigation, minimap
visibility and compiler-error presentation through existing graph/canvas APIs. Expose clear save,
dirty, inherited/overridden and compile-error states. Preserve node preview and instance propagation.
Do not split one material document into independent dock windows in this phase.

**Thread/lifetime/playbook:** main owns UI/graph edits; existing undo and compilation/preview paths
remain authoritative. Reflow must not recreate GPU resources each frame. Any changed preview teardown
requires an explicit in-flight resource audit and render-thread-safe disposal. No new graph asset type.
**Verification:** regenerate if new files; Debug Editor and runtime builds. Open ordinary, graph and
instance materials; edit/undo/redo/save/reload; add/link/delete/paste/find nodes; introduce and repair
an invalid link/compile error; preview a node; cancel dirty close. Shrink/re-expand, change workspace,
switch project and close during preview. Render the saved result in the standalone/exported runtime
if asset behavior changes. No shader errors, validation output, ID conflicts or asset changes from view-only actions.
**Docs:** material/graph help, Conventions helpers, Architecture 2.9 and 2.3 where integration changes.
**Exit/rollback:** complete material authoring works without Phase 4; UI can revert with asset formats intact.

### Phase 4 — Plots are readable and useful for inspection

**Goal:** consistent charts help inspect real measurements, including paused or missing data.

**Changes:** compare `ProfilerPanel::UI_Graph` with `RendererDebuggerPanel::DrawFrameHistoryPlot`
and `DrawPassGPUChart`, then reuse existing ImPlot for the common chart presentation where suitable.
Standardize CPU/GPU labels/colors, units, budget reference, tooltips, legend toggles and fit/reset.
Keep live-follow and paused inspection distinct: automatic range updates must not fight a user who
is inspecting history. Label the x-axis honestly as samples/frames unless real timestamps are added.
Keep buffers bounded/reused and unavailable timings explicit. Other audio/curve plots join only after
their ownership and units are inventoried; a waveform is not automatically a diagnostic time series.

**Thread/lifetime/playbook:** main reads existing published timings; no new telemetry thread, render
pass or synchronous GPU readback. New helper files require regeneration; existing panels retain IDs.
**Verification:** Debug and Dist build coverage; available/missing GPU timing, zero/one/full history,
pause/resume, toggled series, outlier spike, resize, narrow labels and DPI transition. Verify values
against underlying data. Measure UI overhead on the Phase 0 protocol before claiming a speedup.
Check every ImPlot Begin/End and style push/pop. No instrumentation cost is introduced when the tool is closed.
**Docs:** diagnostics help, Conventions plot helper; Threading/Rendering only if an actual contract changes.
**Exit/rollback:** both diagnostic panels have coherent charts independently of later panel migrations.

### Phase 5 — Complete the remaining editor panel families

**Goal:** the rest of the workspace follows the established controls without losing specialist behavior.

**Changes:** migrate in small, independently verified batches: (a) Content Browser and asset picking,
(b) Hierarchy/Properties, (c) console/history and diagnostics controls, (d) settings/help, and
(e) audio tools. Use the Phase 0 inventory to name the exact affected symbols before each batch.
Refine selection, search, empty/error states, long labels and narrow layouts; retain audio's live
Apply/restart/audition semantics. Review workspace menu/shortcuts, panel search and Default/Personal actions
through `EditorWorkspaceManager::DrawPopups`, `DrawMenu` and `DrawPanelsMenu`; keep native docking and
existing settings identities. No blanket rewrite and no placeholder replacement panels.

**Thread/lifetime/playbook:** existing main-thread panels and document/context ownership. A genuinely
new panel requires all four steps: derive/implement hooks, register with PanelManager, expose the
panel toggle/category, regenerate; also verify command-palette reachability. Existing migrations
need none of these identities changed. No new component, asset type or scripting API is planned.
**Verification:** each batch builds/runs before proceeding. Exercise its main workflow, dirty/error
state, keyboard navigation, hide/reopen, project context and narrow/DPI cases. Browser/inspector edits
retain undo, asset identity and persistence; audio close stops owned audition. Scopes and IDs checked
per batch. Run workspace lifecycle regressions after each batch that changes persistence/focus.
**Docs:** affected bundled help and Architecture 2.9; shared helper documentation alongside adoption.
**Exit/rollback:** every inventoried panel has a recorded outcome; revert any batch independently.

### Phase 6 — Prove the remaster across workspaces and platforms

**Goal:** verify the complete experience beyond the primary developer display.

**Changes:** repair observed integration defects from the matrix below, finish documentation, and
remove obsolete local styling/helpers only after their callers have migrated. Do not remove a
working feature because it has not been redesigned yet. Revisit the outstanding workspace platform
checks explicitly rather than declaring them completed by the new visuals.

**Thread/lifetime/playbook:** both policies; main-only UI and existing renderer lifetime contracts.
**Verification:** Debug Editor, Debug/Dist runtime and Linux execution; fresh artifact timestamps,
project regeneration where needed, and exported authored-result coverage for changed asset workflows.
Inspect Windows mixed-DPI main/detached windows and Linux's supported window mode. Run relevant
workspace/docking/YAML regression checks, treating historical fixture drift separately from new failures.
**Docs:** update affected shared docs/help and append evidence/remaining limitations here.
**Exit/rollback:** matrix completed or specific platform gaps recorded without claiming acceptance;
no validation errors or unsaved-work regressions. Roll back the failing migration batch, not user data.

## Part 4 — Whole-feature verification

| Scenario | Required observable result |
|---|---|
| Wide and narrow panels | Primary action reachable; secondary actions discoverable; canvas/editor remains useful; no overlapping controls. |
| 4K/150%, Full HD/100%, repeated resize/moves | Readable text and hit targets; stable user proportions; no accumulated scaling or lost internal sidebar choices. |
| Detached windows / monitor disconnect | Verify Windows reachability and mixed-DPI styling; use Linux's actual supported mode. Existing workspace gaps remain explicit until exercised. |
| Dirty script/material/sound + workspace change | Same document and edits, correct save semantics, no focus/capture/audition leaks. |
| Many tabs, duplicate filenames, long names | Active document identifiable, full path discoverable, close/overflow behavior predictable. |
| Invalid graph or failed save | Clear actionable error; user work retained; no stale-success indication. |
| Existing personal/default/legacy layouts | IDs preserved; no surprise reset or cross-project document leakage; recovery remains available. |
| Charts while running/paused/unavailable | Honest axes, units and source values; inspectable history; missing data clearly distinguished. |
| Keyboard + text/numeric editing | No cross-panel shortcuts, accidental scene commands or intercepted text input. |
| Single/Multi, project switch, shutdown | No races, lifetime regressions, validation errors or ImGui assertions. |
| Runtime and export | Core remains independent; changed authored results survive save/pack/load. Visual styling alone does not justify asset-format changes. |

Every implementation phase verifies ImGui scope balance on early exits, stable IDs for loop items,
matching popup/window names, and the new states in the running editor. Tests target data integrity,
navigation/focus and persistence behavior; do not freeze arbitrary cosmetic pixel values in unit tests.

## Part 5 — Risks and open questions

- **A second shell around working tools:** avoid replacing their document models. The pilot proves
  shared visual helpers with caller-owned actions before other panels migrate.
- **Too little canvas space:** the source shows competing fixed-width regions. Phase 0 reproduces
  the actual failure; Phase 3 checks nested layouts at measured widths and retains user sizing intent.
- **View state becomes authored data:** decide the owner of each new preference before serializing it.
  Per-document zoom/selection, per-workspace arrangement and global appearance are separate choices.
- **Preview teardown or hidden work:** inspect actual queue/resource ownership before changing
  preview scheduling. No per-frame rebuilds or blanket GPU waits.
- **Misleading plot data:** frame indices are not elapsed time, CPU and GPU are not interchangeable,
  and missing instrumentation is not zero cost. Keep labels and availability accurate.
- **Vendor behavior differs from current documentation:** local versions above control implementation.
  Upstream's newer API is prior art only; no unplanned submodule edits/upgrades.
- **Visual direction:** current warm graphite/lime remains the working assumption. A different palette,
  density mode or branding direction needs user feedback on Phase 0 examples.
- **New capabilities:** semantic code completion, reusable multi-document view hosts, independent
  graph/preview dock panels and new graph families require separate decisions/designs. Do not infer
  approval for them from "targeted fixes."
- **Unverified:** precise visual defects across every remaining panel, Linux UI behavior, simultaneous
  detached mixed-DPI behavior and performance costs. Resolve by the inventory/platform matrix;
  use `$profile` only when a performance claim is actually being investigated.

Revisit the plan if the pilot requires replacing the editor framework, a new document model,
asset-format migrations, mandatory external processes or independent tool instances. Those are
architecture/product decisions, not incidental remaster details.

## Part 6 — Research brief

**Goal:** usable existing authoring/diagnostic panels within Lux's adaptive workspaces.

- **Text-editor prior art:** Godot separates document navigation, editing surface and useful status
  information, with shortcuts and adjustable supporting panes. Apply that clarity to Beam's shell;
  do not infer Godot's language services exist in Lux. [Godot Script Editor](https://docs.godotengine.org/en/stable/tutorials/editor/script_editor.html)
- **Graph prior art:** Godot's visual shader editor exposes preview visibility, graph navigation,
  node addition and minimap controls. This supports optional supporting panes and canvas-first use
  in Phase 3, rather than always reserving every column. [Godot VisualShaders](https://docs.godotengine.org/en/stable/tutorials/shaders/visual_shaders.html)
- **Node-editor separation:** upstream imgui-node-editor handles graph interaction while applications
  provide node/pin content. Keep Lux's existing graph evaluator/undo separate from visual styling.
  Upstream documentation is newer than Lux's 0.10.0 macro; verify exact APIs locally.
  [imgui-node-editor documentation](https://github.com/thedmd/imgui-node-editor/blob/master/docs/README.md)
- **Plots:** ImPlot provides axes, legends, pan/zoom/fit and real-time plotting as an ImGui extension.
  Reuse the existing integration for consistent inspection instead of expanding hand-drawn chart
  infrastructure. Its UI primitives do not supply correct sampling/time semantics for the app.
  [ImPlot README](https://github.com/epezent/implot/blob/master/README.md)
- **Pitfall:** automatic axis limits and live data presentation can defeat user inspection; Lux's
  current frame-history path sets limits while drawing. Phase 4 defines follow/pause/reset behavior
  and checks it against the vendored 0.18 WIP APIs rather than copying current upstream examples.
  [ImPlot API](https://github.com/epezent/implot/blob/master/implot.h)
- **Rejected:** replacing ImGui, adding a mandatory external editor/server, copying a different
  engine's entire interface, or treating node graphs/time-series/animation curves as one data model.
  These expand scope or violate Lux's self-contained product/dependency rules.

These findings inform shared panel anatomy, adaptive graph composition and plot interaction choices.
They do not establish runtime correctness, visual acceptance or measured performance in Lux.

## Handoff

**Phases 1–4 are implemented (2026-11-04), ready for user review.**
Phase 5 is in progress; Phase 6 remains planned. The whole-editor Phase 0 inventory and Phase 6 platform matrix are not declared complete
by this pilot. Continue with `$dev` and review each useful slice with `$cr`. The user subsequently
requested a commit whenever a phase is finished; commit each verified phase and keep unrelated
working-tree changes out. Push only when requested.

### Phase 1 delivery and verification

- Added `Core/Source/Lux/ImGui/PanelControls.h/.cpp`: caller-owned actions with stable IDs,
  font/style-based fitting, primary action priority, icon compaction, labeled More overflow,
  selected/disabled states and tooltips. Shared empty/message states wrap within the panel.
  Existing RAII helpers close color/style/ID/disabled scopes; child and popup Begin/End paths
  are balanced, including clipped children and resize while a popup is open.
- Beam uses these controls for New, Save, Find, Editor/Diff and the secondary Save As, Go to
  Line and Close commands. Document ownership, dock identity, syntax editor and diff engine
  remain in their existing owners. File-edit commands are disabled in read-only Diff mode.
  The no-file state has a direct New file action; save failures remain visible until retry succeeds.
- Fixed a save/close correctness defect found while exercising the pilot: the vendored summary
  callback runs during a later Render. Beam now observes the editor's transaction change version
  synchronously before save/close and after editing. Undo followed by replacement of the redo
  branch is detected even when its undo index matches the saved index. No delayed callback can
  mark freshly saved text dirty. The close confirmation is submitted in either view mode.
- Updated Conventions, Architecture 2.9, Building and bundled Beam help. Regenerated projects
  with `scripts/Win-GenProjects.bat --last` for the added implementation/test files.

**Build evidence (Windows Debug):** `bin/remaster-build.txt`, fresh Editor artifact
`2026-11-04 16:10:03`; `bin/remaster-runtime-build.txt`, fresh Lux-Runtime artifact
`2026-11-04 16:10:33`. The existing optional `vaudionative.dll` copy message remains; the fresh
executables launch and complete the probes.

**Repeatable regressions:** `python tests/editor/run_panel_controls.py` passes Single and Multi
at `bin/panel-controls-smoke/1793826629384980000`. It exercises production controls at 100%,
150% and 200% style/font scale, narrow/wide/narrow transitions, hidden primary activation, exact
save bytes, immediate edit/save/close, undo/redo-branch replacement, failed save/retry and live
document retention. `python tests/editor/run_workspace_smoke.py` passes Single and Multi at
`bin/workspace-smoke/1793826648958480500`, covering existing lifecycle, persistence, recovery
and concurrent-save behavior. No Vulkan validation errors were reported in these runs.

**Native pilot check:** used an isolated user directory and disposable project, with session/log
and saved file under `bin/remaster-ui/1793826159869251000`. On the 4K display at 150%, verified
the no-file action, text input, Ctrl+S/native Save As, exact saved bytes, windowed/maximized
resizing (Beam narrowed to approximately 260 logical pixels), More access to Editor/Diff,
disabled Save/Find in Diff, return to the same document, unsaved text through Scripting → Shading
→ Scripting, dirty-close Cancel, toolbar Save clearing the marker, and Ctrl+F. The session shut
down cleanly with no validation errors or assertions. Source review checked keyboard-navigation
support through ordinary ImGui buttons/menu items and the existing keyboard-enabled IO config.

**Limits:** simulated control scaling is not a native mixed-monitor DPI test. Linux, detached
mixed-DPI windows and monitor-disconnect behavior remain in Phase 6; no acceptance or performance
claim is made for those scenarios. The later Beam tab/status refinements, materials/graphs,
plots and other panel migrations have not been included in this phase. No commit or push was made.

### Phase 2 delivery and verification

**Delivered:** Beam now caches UTF-8 labels on document/path changes, prefixes duplicate basenames
with enough parent-folder context to distinguish them, bounds tab widths and uses native unsaved
indicators. The tab-list arrow and searchable Open Documents popup (Ctrl+P) keep many files reachable;
Ctrl+PageDown/PageUp follows the visual order after dragging tabs. Ctrl+O opens existing text files.
Full paths remain available in tooltips, copy-path actions and the close dialog.

The status bar adapts to width and exposes complete details on hover. Session-only 50–250% text
zoom (Ctrl++/Ctrl+-/Ctrl+0), line-number and minimap controls use the existing monospaced font and
vendored editor APIs. Line-number contrast follows the editor theme. Find/Replace flows within its
editor, sizes itself from actual wrapped content and scrolls when height is constrained; an empty
replacement can remove matches. Diff keeps its read-only state and uses the same text zoom.

Go to Line selects its input, clamps the destination safely and returns focus to the editor. Fixed
the pre-existing unsupported InputInt Enter flag, pending editor focus hiding a popup/modal, and
navigation Enter reaching the editing surface after popup dismissal. Close decisions resolve after
the controls draw, so a successful save cannot leave later controls referring to a destroyed document.
Escape cancellation is consumed by its popup. A failed or cancelled close-save retains the decision
and text; Save As is available inside the dialog for recovery.

Save As commits the path/language after a successful atomic write. Existing open-file aliases cannot
create a second writer, failed reads cannot create a blank replacement document, and failed diff reads
retain the previous comparison. Unicode coverage exposed Windows code-page conversion in atomic
temporary names and native-dialog results; the shared FileSystem helpers now retain native paths and
decode NFD UTF-8 explicitly. Failure logging also preserves Unicode destinations.

**Builds:** Windows Debug Editor artifact `2026-11-04 17:25:27` (`bin/beam-phase2-build.txt`),
Debug Lux-Runtime artifact `2026-11-04 17:21:57` (`bin/beam-phase2-runtime.txt`). No new files were
added in Phase 2, so the Phase 1 project generation remains sufficient. The existing optional audio
DLL copy message remains; both fresh executables were verified.

**Regressions:** `python tests/editor/run_panel_controls.py` passes Single and Multi, with evidence
in `bin/panel-controls-smoke/1793831128079175200`. Coverage includes many tabs, visual-order cycling,
duplicate names/path aliases, Unicode paths, failed/cancelled Save As, Windows read-only save/retry,
failed close-save and discard preservation, popup focus/Enter handoff, and real Find/Replace layout
at 100%/150% scales with long lines. Existing workspace lifecycle/persistence/recovery/concurrent-save
tests pass both policies at `bin/workspace-smoke/1793830935465981800`. No Vulkan validation errors.

**Native checks:** disposable project/user data under `bin/beam-phase2-ui/1793829867155569800`.
Verified Open, Go to Line to line 60 without dirtying the file, zoom, filtered document selection
with Enter without inserting text, search, compact status and narrow search at about 260 logical
pixels, Save As cancellation, dirty-close Cancel, visible line-number toggling and all wide search
actions. The corrected close modal stayed open after cancelling native Save As; Save then wrote a
Unicode filename and closed the requested document. Saved bytes were checked. The final native
session log `ui-verified.log` has no assertion or Vulkan validation error; earlier failed probes are
retained separately. Shared docs and bundled help were updated.

**Limits:** native checks used the Windows 4K/150% display; automated scale tests do not replace
native 100%/mixed-monitor or Linux verification. Those remain in Phase 6. Materials, graphs, plots
and remaining panel migrations remain later phases. No commit or push was made.

### Delivery checkpoint

Local `$cr` review found no remaining must-fix or should-fix findings in the Phase 1/2 diff.
Debug Editor and Lux-Runtime artifact timestamps above were rechecked. The panel/Beam regressions
passed again under Single and Multi at `bin/panel-controls-smoke/1793837214579844300` before
committing the combined pilot. Native mixed-monitor and Linux checks remain open as recorded above.
The commit excludes the existing sample-project, audio and other unrelated working-tree changes.

### Phase 3 implementation and verification

Material documents now use the shared action bar, cached UTF-8 tab/path labels, checked atomic saves
and recoverable save errors. A failed save keeps the document dirty and its close decision open.
Graph creation/removal and full Revert participate in the existing undo history; instance creation
opens its document after the current draw/propagation finishes, avoiding a vector-invalidated pointer.

Preview/properties and graph/details use caller-owned split proportions and alternate narrow pages.
Preview, Details and minimap visibility are view choices, persisted through panel state rather than
material data. Hiding a preview keeps its viewport owner and skips rendering. Project changes clear
document-specific graph state, clipboard/selection and owned previews. New toolbar nodes use the
visible canvas center; right-click and dragged-link additions keep their chosen canvas coordinates.
The graph adds searchable Find, shared controls, bounded compiler errors with node navigation,
theme-based selection/link contrast and font-based sizing. Its create/delete/shortcut scopes now
match the vendored editor's conditional Begin/End contract.

**Renderer defects found by real preview frames:**

- Graph permutations can introduce SceneData/RendererData uniforms absent from the previous
  reflection. The relevant geometry/masked passes now retain these initialization-time binding
  intents, and descriptor rebaking restores them when the uniforms appear.
- In Single mode, a nested `Renderer::Submit` during shader reload took the main-thread producer
  path. A draw could therefore use a new descriptor layout before its dependent pipelines were
  rebuilt (first VUID-08600, then device loss). Queue execution now has a thread-local scope;
  nested work runs inline under both policies, including shutdown drains. Resource frees use the
  same execution context. No validation filtering, device-idle workaround or shader-slot change.

The initial failing runs are retained under `bin/material-panel-smoke/1793839334484593700` and
`1793839719014466800`. Both policies passed the repaired shader-transition/preview sequence at
`bin/material-panel-smoke/1793839866818976800`. The expanded run with pointer splitter input and
explicit narrow/wide assertions also passed both policies at `bin/material-panel-smoke/1793840483100222900`.
It covers save failure/retry/reload, property and graph undo, instance inheritance, material pack
round-trip, invalid graph repair, graph-only uniform removal/restoration, node previews, hidden
preview ownership and document/project teardown. Layout frames exercise 100%/150% scale settings;
these are not native monitor transitions.

Fresh Debug artifacts: Editor `2026-11-04 20:01:04`, Lux-Runtime `2026-11-04 20:03:03` (local time).
Logs: `bin/material-phase3-build.txt`, `bin/material-phase3-runtime-build.txt`. The new test source
was included by `scripts/Win-GenProjects.bat --last`. Production docking tests also passed at
`bin/workspace-tests/1793839925806120700` (20 size round trips and constrained hosts).

**Native evidence so far:** private project/user directory
`bin/material-phase3-ui/1793837411114`, Windows 4K/150%, maximized 2560 × 1396 logical window.
Opened the ordinary material, rendered its sphere, used overflow Make Graph, searched the palette
for Float, added with Enter and saved using Ctrl+S. The native run exposed the offscreen toolbar
addition fixed above. `final.log` has no Vulkan validation or ImGui assertion. The follow-up run
`center-verified.log` also has no validation/assertion output. It verified a toolbar-added Float
inside the visible canvas, filtered Find → Output → Enter navigation, dirty-close Cancel, maximized
to 1077 × 635 logical window reflow, the narrow graph Details page, and Shading → Audio → Shading
with the dirty three-node document retained. Save wrote all three nodes, including the new node's
visible-canvas position, and cleared the dirty marker. Pointer resizing also passes deterministic
frame-by-frame ImGui input, alongside repeated keyboard splitter movement. Native 100%, mixed-monitor,
detached-window and Linux execution remain in Phase 6; runtime rendering of an exported authored
scene is not claimed by the material pack round-trip or the successful runtime build.

**Phase 3 delivery checkpoint:** local `$cr` review found no remaining must-fix or should-fix
findings. The checked-save path, ImGui/node-editor scopes and IDs, caller-owned view state, undo
snapshots, optional uniform ownership and nested render-command ordering were reviewed together
with the successful runs above. No asset/pack schema changed. The commit excludes pre-existing
sample-project/audio changes and unrelated untracked files. Phase 4 begins with Profiler/Renderer
Debugger chart ownership and units; Phase 0's remaining inventory and Phase 6's platform/complete
workflow matrix remain open.

**Later-phase baseline:** in the same 1077 × 635 native window, Audio's Sound Bank empty-state text
clips in its narrow right region, and the Audio Mixer table compresses its right columns. Record
these as Phase 5(e) reproductions before migrating those controls. WSL Ubuntu 24.04 is present and
can read the checkout, but its current PATH has no Clang or .NET; Linux execution is unverified.

### Phase 5(a) — Content Browser and common asset pickers

**Baseline:** the disposable project at `bin/browser-remaster-ui/1793843814156` reproduced the
fixed-width header losing Search/New after long-folder navigation in a 1077 × 635 logical window.
The item child also consumed the footer's space, including in a maximized window. These are
browser layout defects, separate from the broader mixed-monitor acceptance matrix.

**Delivered:** `ContentBrowserPanel::RenderTopBar` uses the shared action bar with New retained,
secondary actions in More, a clipped location/parent selector and live search. Folder sizing uses
caller-owned proportions and an alternate popup when constrained; ShowFolders/FolderFraction use
workspace panel state. Existing application-level view/filter/sort/favorite settings retain their
owner. The item region reserves footer height; labels clip by measured text width with complete
UTF-8 paths on hover. Text editing consumes browser shortcuts before asset/scene operations.
Project changes clear copied handles and pending browser context. Back now preserves its forward
destination, and search rebuilding no longer rewrites navigation history.

`Widgets::AssetSearchPopup` shows name/path rows, searches paths, commits the first match with
Enter, and refreshes metadata when the main-thread registry revision changes. Owned cached strings
and handles avoid repeated registry sorting while open; the cache retains no project lifetime.
`AssetRegistry::GetRevision` and `Utils::PathToUTF8` are the supporting Core interfaces. No asset,
project or runtime-pack schema changes. Architecture, conventions, build guidance and bundled help
document the ownership and behavior.

**Verification:** project generation ran with `scripts/Win-GenProjects.bat --last`
(`bin/browser-remaster-generation.txt`). Fresh Debug artifacts: ordinary Editor `2026-11-04
22:27:05`, Lux-Runtime `21:28:43` local. Build logs are `bin/browser-remaster-normal-build.txt`
and `bin/browser-remaster-runtime-build.txt`. Earlier verification used the separately linked
`Editor-RemasterReview.exe` (`21:30:18`) because the regular executable was in use; runners now
accept optional `LUX_TEST_EDITOR` while retaining their ordinary Debug default. Unchanged managed
DLL copy operations reported sharing violations while the review editor was running; the native
executable was freshly linked and its timestamp/size checked. No user editor process was terminated.

Browser Single/Multi runs passed at `bin/browser-panel-smoke/1793845819151284000`, covering
wide/narrow/wide reflow at 100%/150% control scale, footer bounds, folder preferences, Back/Forward,
text shortcut isolation, duplicate-name picker identity, open-picker rename invalidation, rename
undo, Unicode path presentation and project teardown. Workspace Single/Multi passed at
`bin/workspace-smoke/1793845201179765400`; material Single/Multi passed at
`bin/material-panel-smoke/1793845478522558000`. Actual editor frames exposed a manual-item cursor
extent assertion; submitting the item extent through Dummy fixed the cause before those passing
material runs. The final browser rerun against the ordinary rebuilt Editor also passed both
policies at `bin/browser-panel-smoke/1793849352780225200`. Earlier failed runs remain in the local
evidence directories.

**Native retry:** Windows 4K/150%, maximized then 1077 × 635 logical window. Verified grid/list
switching, long-folder entry with Search/New/footer retained, full-path hover text, More → View
options, and New menu access. The private session closed cleanly; `retry.log` and `retry-stderr.log`
contain no Vulkan validation or ImGui assertion. Native 100%, mixed-monitor, detached/disconnected
monitor and Linux checks remain unverified. Local `$cr` reviewed scopes/IDs, metadata cache
invalidation, navigation aliasing and project ownership; unrelated sample/audio files are excluded.

**Remaining:** Phase 5(b–e) hierarchy/properties, console/history/diagnostics, settings/help and
audio tools, the remaining Phase 0 inventory, and Phase 6's full platform/workflow matrix. This
checkpoint completes the browser/picker batch, not the whole remaster.

### Phase 5(b) — Hierarchy and Properties

**Inventory and implementation:** `SceneHierarchyPanel::DrawComponents` reserved 55% for the
name, then placed a fixed 90-unit Add button on that same row alongside a selection count.
`DrawVec3Control` retained a fixed 110-unit label column and three numeric fields at all widths.
The empty Properties message visibly clipped in the 1077 × 635 logical native baseline at
`bin/browser-remaster-ui/1793843814156/hierarchy-baseline.log`; selected-state defects were
established by source and exercised by the new production-frame tests rather than claimed as
native baseline evidence.

The hierarchy now keeps Create/search above its scrolling tree, reports an empty search clearly,
reveals matching ancestors and searches beyond the former ten-level limit. Ctrl+F is scoped to
hierarchy focus; text/popup input cannot trigger F/Escape scene operations. Scene-context changes
clear search/focus and queued entity actions. Entity hover text includes the complete name/UUID.

Properties separates its dynamically sized name/active controls from the shared Add component
action bar. Component choices are searchable by name/category in a bounded scrolling region.
Long names use the existing std::string ImGui adapter instead of a truncating 256-byte buffer.
Empty, locked and multi-selection messages wrap. Section titles reserve space for Help/settings;
these controls participate in keyboard navigation. Transform labels/axes adapt to available font
metrics and retain the existing snapshot undo signal. Only edited axes propagate to a selection,
preserving every entity's other values. Numeric fields reuse Reset/Copy/Paste and keyboard entry.
`ImGuiEx::PropertyLabel` consolidates literal, clipped label rendering and Help reservation across
the existing property overloads; complete clipped text remains available on hover. No authored
schema, selection owner, undo model or runtime dependency changed.

**Verification:** new `SceneHierarchyPanelTests.cpp` is included by project generation
(`bin/hierarchy-generation.txt`). Single/Multi production-frame tests passed at
`bin/hierarchy-panel-smoke/1793855543655685900`: 100%/150% scaling with 800/280/190-wide reflow,
multi-selection retention, a 400-character rename, keyboard reset of one transform axis without
overwriting other axes, component-search focus/addition, scene snapshot persistence, locked/folder
and empty states, a match below twelve ancestors, and scene-switch cleanup. The runner is
`python tests/editor/run_hierarchy_panels.py`, using disposable project/user data and the non-Dist
`LUX_HIERARCHY_PANELS_SELFTEST` hook. Earlier failed probes remain under `bin/hierarchy-panel-smoke`;
the narrow summary overflow was repaired, and queued modifier events in the rename test now get
their own input frames.

Fresh Debug Editor and Lux-Runtime artifacts were checked after the final controls/badge work
(`bin/hierarchy-build.txt`, `bin/hierarchy-runtime-build.txt`): `2026-11-05 00:26:41` and
`00:26:42` local. The final Editor-only rebuild (`bin/hierarchy-final-build.txt`, artifact `00:31:47`) includes the
source review correction to compute badge width from screen-space viewport bounds, keeping
font and available-space units consistent on high-DPI displays.

Final hierarchy Single/Multi coverage passed at `bin/hierarchy-panel-smoke/1793856417530820200`,
including selection after a sustained tooltip hover. Final workspace coverage passed both policies
at `bin/workspace-smoke/1793856437448867700`. Beam/shared controls passed at
`bin/panel-controls-smoke/1793855613856497900`; material saves, graph scopes, reloads and actual
preview frames passed both policies at `bin/material-panel-smoke/1793855632433730700`.
No assertion or Vulkan validation output occurred in those passing runs.

**Native evidence:** disposable project/user data at `bin/hierarchy-remaster-ui/1793855605493067500`,
Windows 4K/150%, maximized and 1077 � 635 logical window. Verified a 400-character entity name,
wrapped hierarchy hover text, hover-to-select, compact name/Add controls, stacked transform axes,
readable component headings/settings, rename and scene undo. The long-name fixture exposed the
pre-existing unbounded viewport selection badge; its cached presentation now clips within its
viewport instead of covering adjacent panels. Final native checks verified that bound at wide and
compact sizes, the Add menu, category search (`audio`, including Music Director), and cancellation
without losing selection. `final.log`/`final-stderr.log` have no assertion or Vulkan validation
error. Earlier runs are retained separately. Compact external-popup typing did not retain focus
through Computer Use; maximized native filtering and deterministic compact picker focus/addition
passed, but native compact-popup typing is not claimed.

The new hover regression also caught an ImGui ordering hazard: tooltip submission replaces
LastItemData. Hierarchy tooltips now submit after row interactions and use wrapped text. Name and
property-label hover text wraps too. Local `$cr` reviewed scopes/IDs, font/viewport units, cached
label ownership, selection/edit targets and scene teardown; no remaining must-fix or should-fix
finding. Project generation was rerun; sample-project/audio edits and unrelated files are excluded.

**Remaining:** Phase 5(c�e), the remaining Phase 0 inventory and Phase 6 remain open. Native
100%, mixed-monitor/detached/disconnect behavior, Linux execution and full exported-workflow
acceptance are not established by this batch. This is the Hierarchy/Properties delivery checkpoint.
