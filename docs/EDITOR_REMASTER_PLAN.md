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

**Phases 1-5 are implemented; Phase 6 acceptance is active (2026-11-05).**
The delivery records below reconcile all 21 registered panels and their verification. The
planning ledger is historical; it is not the current completion ledger. Remaining physical
platform checks and failed presentation paths stay explicit in the latest Phase 6 records.
Continue with `$dev` and review each useful slice with `$cr`. The user requested a commit
whenever a batch is finished; commit verified changes and keep unrelated working-tree changes
out. Push only when requested.

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
Windows 4K/150%, maximized and 1077 × 635 logical window. Verified a 400-character entity name,
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

**Remaining:** Phase 5(c–e), the remaining Phase 0 inventory and Phase 6 remain open. Native
100%, mixed-monitor/detached/disconnect behavior, Linux execution and full exported-workflow
acceptance are not established by this batch. This is the Hierarchy/Properties delivery checkpoint.


### Phase 5(c) — Log, History and asset diagnostics

**Inventory and baseline:** `EditorConsolePanel::{RenderMenu, RenderConsole}`, its existing
pending-message queue, `UndoHistoryPanel::OnImGuiRender`, and
`AssetManagerPanel::OnImGuiRender`. In the private native project, the old Log toolbar overlapped
search/tag controls even at 2560 logical pixels; its detail dialog clipped Copy Full Message.
The compact editor reproduced the overlap. History already had a bounded 256-step / 128 MB
owner; no claim of an actual row-ID collision is made. Audio Debugger remains in Phase 5(e).

**Delivered:** Log uses the shared action bar and a full-width search, with complete-message/tag
filtering, severity/tag/recent-search controls and explicit Follow latest. Manual inspection
stops following; new messages preserve that position. Retention stays bounded at 10,000.
Producers retain the existing mutex queue; main formats stable row identities, timestamps and
captions after swapping outside the lock. Narrow rows combine metadata and preview; wide tables
keep their original identity/settings. Details own their text independently of Clear/retention,
with accessible Copy/Close above the scrolling text and Escape dismissal. Owned popup teardown
runs on hide/project change through the shared `ClosePopupByID` helper.

History adds shared Undo/Redo/Current state controls, clipped rows and full-label tooltips, keeps
the existing before-edit/through-future-edit jump semantics, and defers callbacks until its ImGui
scopes close. Asset Manager caches owned metadata/search rows by registry revision, supports
path/type/status/handle search and live Only loaded filtering, compact rows and complete metadata
details. Existing Sync/Reload/Save commands remain explicit. No new asset formats, sink, threads
or runtime dependency were added. Bundled help and current shared documentation were updated.

**Verification:** projects regenerated for the two new C++ test files. Fresh Debug Editor
`2026-11-05 01:20:56` and Debug Lux-Runtime `01:20:59`; logs
`bin/diagnostic-controls-build.txt` and `bin/diagnostic-controls-runtime-build.txt`.
`tests/editor/run_diagnostic_controls.py` passed Single/Multi in
`bin/diagnostic-controls-smoke/1793859676746879800`: real text search/clear, full-message matching,
severity/tag filters, follow/manual inspection/new arrivals, bounded retention, copied detail
lifetime and popup isolation, Unicode/duplicate-path metadata refresh, compact detail selection,
and History undo/redo callbacks. Wide/narrow/wide coverage includes 190-pixel panels at 100%/150%.
The first test drafts omitted explicitly selecting their new ImGui contexts; those failed runs
are not accepted evidence. The final isolated tests clear search through keyboard input rather
than overwriting an active text field's backing state.

Workspace lifecycle/persistence/recovery/concurrent-save passed Single/Multi in
`bin/workspace-smoke/1793859701209283100`. Native checks used only the private project/user under
`bin/hierarchy-remaster-ui/1793855605493067500`, with `diagnostics-native.log` and
`diagnostics-native-stderr.log`: wide and 1077×635 Log reflow, actual search, detail dialog/Escape,
More actions, compact Asset Manager/detail popup, command-palette reachability, History empty
state and a real rename → row undo → row redo → toolbar undo. No ImGui assertion or Vulkan
validation error was found in these logs. Native Clear was covered synthetically; clipboard
transmission was not needed. Mixed-monitor/disconnect and Linux execution remain unverified.

**Remaining:** Phase 5(d–e), the remaining Phase 0 inventory and Phase 6 remain open. This batch
does not declare the complete editor remaster finished.


### Phase 5(d) — Settings, Help and Light Settings

**Verified inventory:** `ApplicationSettingsPanel::{OnImGuiRender, DrawPageList, DrawEditorPage,
DrawViewportPage, DrawContentBrowserPage, DrawDiscordPage}`, `HelpPanel::{OnImGuiRender,
UI_Toolbar, UI_TopicList, Navigate, SaveState, LoadState}`, `ProjectSettingsWindow::OnImGuiRender`
and its seven existing section renderers, plus `LightSettingsPanel` and the shared
`Widgets::EditVec3` path it uses. Native baseline used the private hierarchy-review project/user:
Application Settings reserved sidebar space at compact widths; Help used a fixed topic width
and clipped titles. Source review also found Application Settings returning from a clipped
page-list child without `EndChild`, and Project Settings placing Save after all its sections.
The narrow synthetic light fixture reproduced 216 pixels of submitted content in a 190-pixel
panel. No new editor/panel identity or asset format was introduced.

**Delivered:** Application Settings uses proportional, keyboard-resizable sidebar navigation and
a page selector when the sidebar/content minimums cannot fit. Page/fraction state is optional
and survives reflow; property controls are bounded in font units, descriptions and recent paths
wrap, and startup/recent/grid actions use shared controls. The clipped-child scope is balanced.
Help keeps its offline documents, links and history, adds shared Back/Forward/Start/Topics
controls and full-width search, clipped titles with complete hover text, proportional topics,
and a full-width topics/page choice at narrow widths. Selecting a search result returns to its
page. Restore retains the topic and sidebar preference without taking focus. Project Settings
keeps Save/status/section selection above scrolling content, remembers its selected section,
opens the chosen section, preserves existing immediate edits and save-on-close behavior, and
retains dirty state on failed saves. Build actions remain explicit; leaving Audio or changing
project stops owned dialogue audition, and project changes clear old cleanup scan/report state.

Light Settings has wrapped entity names, independent component/UUID ID scopes, bounded grids,
a shared environment-map asset picker and a wrapped missing-scene state. Its project hook respects
`OpenProject` installing the new scene before the project notification; project closure releases
it. Shared `BoundedCollapsingHeader` clips long titles while retaining their normal IDs and arrow
behavior. `Widgets::EditVec3` stacks axes rather than computing negative input widths. Focused
settings/manual text entry consumes scene shortcuts; hide/context teardown clears focus. All
changes run on main and add no required dependency, asset schema or renderer resource.

**Verification:** new C++ tests required project regeneration (`bin/settings-panels-generation.txt`).
Final Debug Editor `2026-11-05 02:02:26`; Debug Lux-Runtime `01:53:04`, newer than the changed Core
library (`01:47:25`). Build logs: `bin/settings-panels-build.txt` and
`bin/settings-panels-runtime-build.txt`. Final settings runner passed Single/Multi in
`bin/settings-panels-smoke/1793862147249792000`, including 100%/150% wide/narrow/wide settings and
Help, 190-pixel controls, a genuinely clipped child, optional state/invalid fractions, actual narrow
page-selector keyboard navigation, real manual search/result selection, relative links, text
isolation, viewing without edits/build launch, project save/failure/retry, cleanup-context teardown,
light scene ordering/release and stacked-vector axis reset. The acoustic-control startup probe
also passed for both policies in that fixture. Light's long-name/four-component fixture was
checked at 150%; physical monitor/DPI transitions remain in Phase 6.

Workspace lifecycle/persistence/recovery/concurrent-save passed Single/Multi in
`bin/workspace-smoke/1793861639679527500`; hierarchy/inspector reflow, multi-edit, search and
persistence passed Single/Multi in `bin/hierarchy-panel-smoke/1793861658833952100`. Native checks
used only `bin/hierarchy-remaster-ui/1793855605493067500` (`settings-baseline.log`,
`settings-final.log`, corresponding stderr logs): manual search/open-result/Back, maximized to
1077×635 reflow, Application Settings page switching, and Project Settings pinned Save plus section
selection/opening. No ImGui assertion or Vulkan validation error was found. Final keyboard-default
focus polish was verified by the final synthetic selector test. Local `$cr` checked scopes/IDs,
main-thread ownership, optional state compatibility and project/scene notification order; no
remaining must-fix or should-fix issue in this batch. Bundled help/shared docs were updated;
unrelated sample/audio files remain excluded from delivery.

**Remaining:** Phase 5(e) audio tools, the remaining whole-editor inventory and Phase 6's full
platform/workflow matrix. This checkpoint does not declare the complete editor remaster finished.

### Phase 5(e)(1) — Sound Banks, Mixer and checked audio saves

**Verified inventory:** `SoundBankEditorPanel` and `AudioMixerPanel` render/edit/lifecycle paths,
`SoundEditorPanel::SaveDocument`, the shared disabled-scope wrappers, and Sound/Bank source and
pack serializers. The recorded native baseline showed clipped Bank empty-state text and
compressed Mixer columns at compact sizes. Sound Editor's full UI and Audio Debugger remain
in the next audio slice.

**Delivered:** Mixer keeps Save/Apply above scrolling Buses, Reverb return, Snapshots, VCAs and
Scene Sound Banks pages. Buses use a proportional split when both regions fit and a selected
bus details view at compact widths; volume remains linear with a separate dB reading. Effects,
naming, snapshot overrides and bank controls use bounded or wrapped controls. Optional Page and
BusFraction state preserves view choices without changing authored data. Snapshot rows use UUIDs,
render literal names and reacquire selection after structural edits. Hiding finishes the current
undo transaction. Structural edits, page changes, hide and project changes stop owned snapshot
preview. Failed Save/Apply retains pending state, and Apply remains unavailable during Play.

Bank membership/residency changes atomically save a candidate before publishing to the loaded
bank and undo stack. Rejected operations visibly explain failure and can be retried. Member
pickers and Remove actions stack when narrow; paths/status wrap. Memory estimates and unbanked
inventory cache results, invalidate on registry/data generations and membership changes, and
provide Refresh for edits made elsewhere. Unreadable banks report an incomplete inventory.
Sound and Bank serializers expose checked saves, validate the existing size limit and atomically
write exact YAML bytes before updating dependencies. Sound documents retain dirty work if writing
fails. Source and pack formats are unchanged.

**Root fixes exposed by verification:** a disabled Scene Sound Bank row reproduced an ImGui
stack assertion because `ScopedDisable(false)` could consume its caller's disabled scope. The
shared wrappers now always own a balanced Begin/End, including false conditions. A later run
crashed before the first Bank edit (`bin/audio-panels-smoke/1793865689594816600`): the matching
Windows fault offset resolved through the executable/PDB to `MaterialAsset::GetParent`.
`EditorAssetManager::RegisterAssetDependencies` had used unchecked `Ref::As` casts as type tests,
so loading a bank inspected a material layout. It now dispatches by actual asset type and leaves
serializer-owned dependencies intact. The regression reloads a real bank and checks its exact
Sound dependency set. These failed runs are not accepted verification evidence.

**Verification:** project regeneration for the new C++ tests is recorded in
`bin/audio-panels-generation.txt`. Final Debug Editor `2026-11-05 03:17:55`; Debug Runtime
`03:10:59`, newer than changed Core `03:10:05`. Build logs are `bin/audio-panels-build.txt` and
`bin/audio-panels-runtime-build.txt`. Final audio runner passed Single/Multi in
`bin/audio-panels-smoke/1793866689230434400`: 100%/150% wide/narrow/wide reflow down to 190 pixels,
all Mixer pages and compact selected details, actual snapshot creation, optional state, text
isolation, save failure/retry and unchanged original bytes, undo/redo, bank reload dependencies,
real Sound/Bank pack round trips, saved mixer decode, Apply, snapshot preview stop, disabled
bank actions during Play and context teardown. The fixture's Sounds have unassigned clips;
these pack tests do not establish exported-game audio playback.

Shared Single/Multi regressions passed: Beam/action controls
`bin/panel-controls-smoke/1793866296000258400`, materials/graph lifecycle
`bin/material-panel-smoke/1793866314498221400`, settings/Help
`bin/settings-panels-smoke/1793866539259624200`, hierarchy/inspector
`bin/hierarchy-panel-smoke/1793866577589823800`, workspace persistence/recovery
`bin/workspace-smoke/1793866597513299000`, diagnostic controls
`bin/diagnostic-controls-smoke/1793866615915489500`, and plots
`bin/diagnostic-plot-smoke/1793866639030328300`. The last build only corrected the Master row's
hidden label and formatted changed lines; the final audio suite exercised it.

Native review used only `bin/audio-panels-ui/1793865742372562700` (final.log and final-stderr.log):
Ctrl+3 Audio switching without a strip, Unicode/literal Bank paths, actual unbanked inventory,
2560-wide to 1077x635 restored reflow and back, compact bus selection/details, wide effect chain,
page selection, literal long snapshot names and pinned toolbar while scrolling. No assertion or
Vulkan validation error was found; the private editor closed normally. Attempts to drag the dock
separator did not change its position, so this session does not establish dock-drag acceptance.
Local `$cr` checked scopes/IDs, main-thread ownership, state compatibility, checked writes and
preview lifetime. No remaining must-fix or should-fix finding in this batch. Shared documentation
and bundled audio help are updated; unrelated sample/audio changes are excluded.

**Remaining:** Phase 5(e)(2) Sound Editor UI and Audio Debugger, the remaining whole-editor
inventory, and Phase 6 platform/workflow acceptance, including dock-drag, mixed-monitor/disconnect,
Linux execution and exported authored playback. The full remaster remains in progress.

### Phase 5(e)(2a) — Sound Editor

**Verified inventory:** the Sound Editor's document/tab/save/close lifecycle, tree actions and
selection, Sound/Spatial/Music controls, parameters, bank membership, clip waveforms, music
markers, parameter curves and owned audition. The previous shell used fixed tree widths,
stacked regions at narrow widths, independent toolbar buttons and inline marker/parameter
commands. Source review found that hide/switch/save could leave an edit outside undo history,
and the Banks section published membership before observing write success. The narrow 150%
test reproduced an overflowing Parameter Curves caption; it now wraps.

**Delivered:** pinned shared Save/Revert/Play/Stop/Loop/Tree actions and a section selector,
proportional tree/details regions, optional tree visibility, and full-width alternate tree/details
when both minimums cannot fit. Optional Section/TreeFraction/ShowTree state preserves the user's
wide preference. Tree rows retain arrows/selection and literal bounded names with wrapped hover
text; deep indentation leaves room for labels. Headers no longer add hidden indentation, and
marker, parameter and curve commands use shared bounded actions. Canvas drawing stays clipped
to its own rectangle, retaining seconds, BPM, linear values and logarithmic filter mapping.
Waveform caches invalidate after asset data changes. The Banks catalog caches by sound and
registry/data generation and supports explicit Refresh; its membership commands save candidates
before publishing and pushing undo, with visible failure/retry.

Save, hide, close and document changes finish current edit transactions. Close decisions retain
documents on failed saves, survive hide/reopen, and tear down their owned modal on hide/project
change. Save and audition failures have visible errors. Project changes clear old document,
focus, modal, waveform and bank state without resolving old handles through the new manager.
Switching documents and hiding stop owned audition immediately. Existing Sound/Bank source and
pack schemas, editing algorithms and global history remain in their owners. This batch changes
only editor code, tests and help/documentation; it adds no mandatory runtime dependency.

**Verification:** no new C++ file, so no new project regeneration was needed. Final Debug Editor
`2026-11-05 03:52:39`, build log `bin/sound-editor-build.txt`; shared Core/Runtime were unchanged
from the preceding verified batch. Audio runner passed Single/Multi in
`bin/audio-panels-smoke/1793868759777005700`. It includes all earlier Bank/Mixer/save regressions,
all Sound sections at 100%/150% wide/narrow/wide down to 190 pixels, compact tree and Music
controls, unchanged authored data while viewing, optional state/invalid fractions, retained
hide-edit undo/redo, bank save failure/retry/undo/redo, actual PCM waveform decoding and clip-byte
reload, waveform clicks, marker drags and curve double-click insertion with real ImGui input and
one undo transaction each. Real Sound audition starts through the native engine, stops on document
switch, reports failed start, and close/modal/project teardown is checked. The fixture Sound's
volume is zero to avoid audible test output. This is not an exported-game playback check.
Workspace lifecycle/persistence/recovery passed Single/Multi in
`bin/workspace-smoke/1793868781550989500`.

Native review used the private project/preferences under
`bin/sound-editor-ui/1793868488727799500` (final.log, final-stderr.log): open a multi-container
Sound, select a long literal node name, inspect loop/waveform/curve controls, change start offset
by clicking the waveform, save with Ctrl+S, restore to 1077x635, hide the tree, choose Parameters
and close the saved document with Ctrl+W. The resulting source records StartOffset 0.11560365;
no assertion or Vulkan validation error was found, and the review editor closed normally. The
final build adds visible audition-error handling, covered by the final synthetic failed-start
probe. Local `$cr` checked scopes/IDs, candidate writes, main-thread ownership, optional layout
state, undo grouping, canvas clip balance and project/preview teardown. No remaining must-fix or
should-fix issue in this batch. Bundled audio help/shared docs were updated. Unrelated sample
assets and project changes remain excluded.

**Remaining:** Audio Debugger (Phase 5(e)(2b)), remaining whole-editor inventory and Phase 6's
platform/workflow acceptance. The full remaster remains in progress.

### Phase 5(e)(2b) — Audio Debugger

**Verified inventory:** `AudioDebugPanel::{OnImGuiRender, UI_Backends, UI_Playback, UI_Performance,
UI_SoundBanks, UI_Acoustics, UI_Sources, UI_Validation, UI_Visualisation, SetSeeTheSoundEnabled}`,
its scene/project lifecycle and new captured-reading ownership. The previous view drew every
section together, used fixed bank/clip/source column widths, traversed scene sources and metadata
while drawing, and had unwrapped captions and error text. Its visualization properties also
used scene-undo defaults despite being panel-owned view settings.

**Delivered:** pinned Pause/Resume and Refresh readings, a page selector for Overview, Sound Banks
and clips, Acoustics, Sources, Validation and Visualisation, and optional Page layout state.
Readings capture together on main at 4 Hz while visible: engine/performance/budgets/meters,
bank/clip lists, acoustic/reverb values, and source UUID/name/Sound/measurement rows. Paused
readings own values and strings and survive entity removal; they retain no entity, voice or solve
pointers. Refresh explicitly captures once while paused. The caption reports age and makes clear
that audio continues. Context/project changes clear stale readings/search/validation; project
notifications preserve an already-installed new scene. Closed panels do not capture.

Wide tables retain their IDs. Narrow source/bank/clip lists use clipped rows and wrapped selected
details; source filtering matches name, Sound path or UUID and clears an excluded panel selection.
Long literal names and UTF-8 paths remain inspectable. Measurements preserve distance in metres,
linear gain, relative HF loss, occlusion percent, path diagnostics, seconds, percentages and dBFS;
missing listener/acoustic/mixer data stays unavailable. Meter graphics remain, compact captions
wrap, and zero signal is shown as -inf dBFS. Acoustic counters retain 64-bit values. Validation
remains a separate explicit project snapshot. Visualization properties use panel-owned undo-free
controls, bounded emitter radius and the existing reversible See the Sound preset. No new audio
instrumentation, worker, renderer resource, asset schema or required dependency was introduced.

**Verification:** no new source file, so no project regeneration needed. Final Debug Editor
`2026-11-05 04:33:47`, log `bin/audio-debug-build.txt`; Core/Runtime were unchanged from the preceding
verified batch. Final audio runner passed Single/Multi in
`bin/audio-panels-smoke/1793871227997730100`, including all earlier authoring/save/pack/audition
checks and the diagnostic probes: 180 real scene sources and a runtime listener, 5-metre captured
distance, entity removal while readings are paused, available/unavailable presentation fixtures,
all six pages at 100%/150% wide/narrow/wide down to 190 pixels, list clipping, full details,
Sound/UUID/no-match filtering and excluded selection, explicit refresh and resumed cadence,
first-open sizing without forced dimensions, hidden sampling, text isolation, optional state,
reversible visualization and project/scene teardown. Serialized scene data remains identical
through view-only interactions. Representative acoustic/meter rows in the width probes are
owned test presentation values; they do not establish a new live acoustic solve or performance
benchmark. Workspace lifecycle/persistence/recovery passed Single/Multi in
`bin/workspace-smoke/1793871249811953900`.

The stricter error-state probes caught the unwrapped acoustic failure message; it was replaced
with the shared wrapped message. Native review additionally reproduced a first-open auto-fit
collapse caused by exclusively wrapped content. A FirstUseEver size now supplies usable initial
geometry without restricting user resizing, and the regression opens the panel without a test
size override. Earlier failed runs are not accepted evidence.

Native review used only `bin/audio-debug-ui/1793870532552252400`, with the serialized 180-source
Diagnostics.luxscene and fresh user-sized preferences (`sized.log`, `sized-stderr.log`). It covered
command-palette opening, corrected first-open size, compact source selection and full literal name
hover/details, actual Pause/Refresh with an advancing age caption, and typed Source 17 filtering.
That filtering exposed an excluded selection still showing details; the final build clears it,
verified by the final regression. Earlier logs retain the initial fixture-extension mistake and
first-open collapse. Attempts to resize the floating window through computer use did not change
its geometry, so native wide-table/dock-drag acceptance is not inferred from them; wide table
coverage here is the production ImGui test. No assertion or Vulkan validation error was found in
the final native log, and the private editor closed normally.

Local `$cr` checked scopes/IDs, owned snapshots, capture cadence/hidden work, main-thread access,
optional-state compatibility, literal text, panel-owned visualization and context ordering. No
remaining must-fix or should-fix finding in this batch. Shared documentation and bundled audio
help are updated; unrelated sample assets/project changes remain excluded.

**Remaining:** the whole-editor inventory/rollout audit and Phase 6 platform/workflow acceptance,
including native dock/splitter dragging, physical mixed-monitor/detached/disconnect checks, Linux
execution, Debug/Dist runtime gates and exported authored-result coverage. The complete remaster
is not yet declared finished.

### Phase 5(f) — Scene Renderer and Game View inventory

**Verified inventory:** `SceneRendererPanel::{OnImGuiRender, SaveProjectRendererSettings,
SaveState, LoadState}` and the Scene/Game View panel adapters and viewport sizing/ownership.
Scene View keeps its specialized picking, gizmos and overlays in `EditorLayer::UI_DrawSceneView`;
this batch does not replace that renderer integration. The Scene Renderer header previously
forced status chips and five quality buttons into rows; the Game View no-camera hint used one
unwrapped draw-list line.

**Delivered:** Scene Renderer now pins shared Save/More, an overall quality selector and full-width
search above a scrolling body. Reset Debug Views remains in More, disabled during Play. Status
and error messages wrap, card/category headings and property labels are bounded with complete
hover text, controls use font-based widths, and fixed-resolution presets use the shared overflow
bar. Common toggles retain their authored behavior and gain the shared keyboard activation.
Search temporarily expands matching cards without rewriting saved collapse preferences. Existing
card-state keys and panel identities remain intact. Failed saves keep dirty state and show the
project path and retry guidance; project changes clear old transient error/search/focus state.
Text/numeric editing consumes keyboard events while focused. Game View wraps and centers its
no-camera guidance within the viewport. Initial FirstUseEver geometry preserves later user sizing.
No shader, renderer algorithm/resource ownership, asset format or mandatory dependency changed.

**Verification:** final Debug Editor `2026-11-05 05:06:32`, log `bin/viewport-panels-build.txt`.
Dist Editor also compiled (`05:05:47`, `bin/remaster-dist-editor-build.txt`); the subsequent test-only
fix corrects a YAML assertion's existing Rendering map and is excluded from Dist. Dist Runtime
was freshly built at `04:40:03` (`bin/remaster-dist-runtime-build.txt`). No new source file or project
regeneration was needed. Final Single/Multi settings/viewport runner passed in
`bin/settings-panels-smoke/1793873206626321100`: 100%/150%, wide/narrow/wide down to 190 pixels,
native/fixed/dynamic resolution branches, all card families via search, current visible child
bounds, unchanged scene/renderer data while viewing, optional card state, hidden/text focus,
failed project save/retry and an authored culling value decoded from saved YAML. Game View checks
actual caption vertices remain within narrow bounds and no renderer is allocated by empty drawing.

The width probe caught an unbounded category heading, fixed with the shared bounded control.
A separate nested-child failure was retained previous-width content from a clipped child
(`SkipItems=true`, zero submitted width), not live overflow; the probe now checks submitted bounds
of visible children and brings fixed-resolution controls into view. A later failed run read the
wrong YAML nesting in the assertion; the fixture's actual saved value was correct. Failed runs
are not accepted evidence. Workspace lifecycle/persistence/recovery/concurrent-save tests passed
Single/Multi in `bin/workspace-smoke/1793873125658871100`; production docking tests passed in
`bin/workspace-tests/1793873076754436100`, including twenty size round trips and constrained hosts.

Native Debug review used only `bin/viewport-panels-ui/1793872536991667400`: command-palette search
and opening Scene Renderer showed the pinned actions, selector, status and bounded cards. Its
`native.log` session closed normally. Final native review was started (`final.log`) but Computer
Use was stopped with Escape before further inspection; no final native acceptance is inferred.
No user sample assets/preferences were used. Local `$cr` reviewed the batch's scopes/IDs,
main-thread context access, existing persistence keys, bounded text, explicit save errors and
ownership. No remaining must-fix/should-fix finding in this batch. Shared architecture/build docs
and bundled interface/Game View help are updated; sample-project changes remain excluded.

**Platform evidence and remaining work:** WSL Ubuntu 24.04's Vulkan software device is llvmpipe
25.2.8; the required core-feature list from the repository CI probe passes against
`bin/remaster-linux-vulkan-capabilities.txt`. This is capability evidence, not Linux execution.
The actual `Linux-Build.sh debug` preflight stops for missing `dotnet` and `clang`
(`bin/remaster-linux-build-preflight.txt`). No build prerequisite was installed in this batch.
An additional native Dist Editor attempt failed before panel UI: Dist disables source shader
compilation, while `LuxEditorApp.cpp` leaves ShaderPackPath unset. Its private report is
`dist-user/CrashReports/Editor-2026-11-05_05-06-37/report.txt` under the native fixture above.
Shader-debug configuration triage confirmed this source path; no shader or startup workaround was
introduced. Dist compilation does not establish native Dist panel/chart acceptance. The whole-editor
inventory/rollout audit, native drag/mixed-monitor/detached/disconnect matrix, Linux execution,
Debug Runtime freshness review and exported authored-result coverage remain open. The full goal
is still in progress.

### Phase 5(g) — First floating-open inventory

The remaining initial-geometry audit exercised Asset Manager, Light Settings, Audio Mixer and
Profiler in their production ImGui render methods for three frames with no requested test size
or saved settings. All four baseline probes reproduced an initial width below the usability bound
(200 pixels); prior forced-size probes had not exercised this state. These are recorded runtime
reproductions, not assumptions from the absence of SetNextWindowSize. The four panels now supply
FirstUseEver sizes, preserving saved docking/settings and subsequent user resizes. There is no
new persistent key, panel identity, minimum-width restriction or data mutation.

Tests resize the existing windows as a dock/OS resize would before exercising narrow layouts:
otherwise a panel's FirstUseEver call can replace a test's pending NextWindowData even when its
condition no longer applies. The fix does not weaken the narrow-width cases. Final Debug Editor
is `2026-11-05 10:19:55` (`bin/panel-first-open-build.txt`). No new C++ file or regeneration needed.
All four new initial-open probes passed with their existing complete Single/Multi suites:
settings/Light/Scene Renderer `bin/settings-panels-smoke/1793892076501515200`,
Log/History/Asset Manager `bin/diagnostic-controls-smoke/1793892075672971700`,
audio authoring/diagnostics `bin/audio-panels-smoke/1793892178882929700`, and
plots/Profiler `bin/diagnostic-plot-smoke/1793892182478172300`. These are production ImGui probes;
no additional native desktop acceptance is claimed. Local cr found no must-fix/should-fix issue
in the scoped size/probe changes; shared architecture now explains the initial-geometry contract.
Unrelated sample-project files remain excluded. Platform/export/inventory acceptance remains
in progress, including Scene View overlay/viewport context review.

Linux prerequisite preparation has progressed beyond the earlier missing-tools result: Ubuntu's
stale package index initially returned 404s, then refreshed successfully. The repository CI's
compiler/GTK/window/audio development prerequisites are installed, with clang 20 and g++ 14.
Microsoft's .NET 9.0.320 SDK is local to ignored `bin/linux-toolchain/dotnet`; workspace-only
compiler aliases are in `bin/linux-toolchain/bin`. Logs are `bin/remaster-linux-apt-update.txt`,
`bin/remaster-linux-prerequisites.txt`, `bin/remaster-linux-dotnet-install.txt` and
`bin/remaster-linux-dotnet-info.txt`. This establishes prerequisites, not a successful Linux
engine build or execution. No new mandatory dependency was added for game makers.

### Phase 6 — Windows exported runtime acceptance

The existing scene/acoustics runner passed a normal Debug Play -> editor export -> packaged Dist
player check under Multi with isolated user storage. It used the owned Sponza corner scene and
copied Audio/model inputs, exported 104 assets plus ShaderPack.lsp, then launched only the package's
player. Source project, scene, registry and model hashes were unchanged. Play and packaged runtime
matched direct-ray bits, raw/occlusion gain, fourteen opening paths and listener reverb within
the runner's tolerance. Both native runs reported no Vulkan validation messages. The output is
correctness evidence; the single captured timing sample is not a performance benchmark.

Command: `python tests/audio/run_scene_acoustics.py --config debug --threading multi
--export-runtime --runtime-config dist --cases corner --timeout 600` with a private
`LUX_USER_DATA_DIR`. Wrapper evidence: `bin/remaster-runtime-acceptance/1793892383080933200`.
Fixture/package: `bin/soundwave-scene-tests/sponza-acoustics-1793892383578203400`.
Native capture/export/player reports:
`bin/golden/sponza-acoustics-1793892383578203400-corner`, especially AudioDemo.status,
AudioDemo.export.yaml, AudioDemo.runtime-audio.yaml and its .status. This exercises the normal
exported source/pack/scene/renderer/audio path with the current binaries. Earlier panel-specific
round trips continue to prove their authored edits, undo and checked saves separately; this run
does not claim a new material graph authoring interaction or physical monitor acceptance.

The Linux Vulkan SDK 1.4.335.0 was fetched into the verified, previously absent
`Core/vendor/VulkanSDK/x86_64` path; glslc executes successfully. Evidence:
`bin/remaster-linux-vulkan-fetch.txt`. The first inline WSL build launcher failed while parsing
an inherited Windows PATH containing spaces, before any build/generation. An ignored script now
quotes that PATH and invokes the unchanged repository Linux build workflow. The real Linux
build is still being evaluated; no Linux execution acceptance is claimed yet.

### Phase 6 — Linux build and initial execution evidence

The actual Linux Debug Core, Editor and Runtime compiled and linked with clang 20. Fresh binaries:
Editor `2026-11-05 10:44:44`, 406385432 bytes; Lux-Runtime `10:46:31`, 321040032 bytes. The first
build stopped in post-build deployment because Stage-LinuxRuntimeLibraries.sh had CRLF in this
Windows checkout (`set -euo pipefail` included a carriage return). HEAD's script blob was already
LF. `.gitattributes` now gives every tracked .sh LF checkout, including the runtime deployer,
Linux launch helpers, packaging helper and sample-script generator. Seven current checkouts were
normalized without changing their script contents. Fresh core.autocrlf=true checkout probes pass
for deployment, launch and sample generation in `bin/shell-eol-check/1793893881729369300`.

Runtime staging was rerun with the build's actual Linux VULKAN_SDK and now resolves Assimp,
shaderc and Vulkan from the staged lib directory without build-tree paths
(`bin/remaster-linux-runtime-staging.txt`). The first standalone staging probe lacked that SDK
variable and correctly rejected a missing shaderc library; it is not accepted staging evidence.
The full unchanged Linux-Build.sh workflow then completed, including sample script compilation:
`bin/remaster-linux-build-final.txt`. Initial failed build evidence remains in
`bin/remaster-linux-build.txt`. Remembered Windows project generation was restored with
Win-GenProjects.bat --last (`bin/remaster-windows-regeneration.txt`); no Windows source was
replaced by the Linux generation.

The real Linux editor ran workspace lifecycle/persistence/recovery/concurrent-save tests under
Single/Multi in `bin/workspace-smoke/1793894351618872635`. All eight panel runners also reported
PASS under both policies through `bin/remaster-linux-panel-matrix.txt`: Beam/shared controls
1793894584155407467, material panels 1793894618629807233, diagnostic plots 1793894918675894025,
browser 1793894940397316104, hierarchy 1793894995589715604, diagnostic controls
1793895035396551803, settings/viewport 1793895078490086416, audio panels 1793895119059419269
(the corresponding bin/*-smoke directories). These exercise Linux builds, authored data, text
and shortcut logic, fonts, 100%/150% test scaling, width transitions and lifecycle.

**Presentation limit:** manual inspection caught vkGetPhysicalDeviceSurfaceFormats2KHR returning
VK_ERROR_SURFACE_LOST_KHR and repeated NRI swapchain creation failures in the material runner's
WSLg Wayland render-startup frames. Its PASS marker therefore does not establish successful
native preview presentation. This is retained as failed presentation evidence, not filtered out
or accepted as harmless. The existing X11/XWayland path is being checked separately with Vulkan
validation still enabled. No renderer guard, retry timeout or validation setting was changed.
Physical monitor/drag/detached/disconnect acceptance remains open.

### Whole-editor inventory reconciliation

All 21 current EditorLayer AddPanel registrations have a named owner and rollout outcome:

| Panel | Outcome/evidence location in this plan |
|---|---|
| Scene Hierarchy | Phase 5(b), search/create/multi-edit/undo and bounded rows. |
| Properties | Phase 5(b), independent visibility/focus and adaptive component/axis controls. |
| Content Browser | Phase 5(a), navigation/actions, asset identity/pickers and undo. |
| Beam | Phases 1–2, document tabs/actions, narrow search/diff and dirty-close recovery. |
| Log | Phase 5(c), retained messages/filter/follow and complete details. |
| Game View | Phase 5(f), retained viewport renderer ownership and wrapped no-camera guidance. |
| Scene Renderer | Phase 5(f), bounded/pinned settings controls and visible checked-save status. |
| Renderer Debugger | Phase 4, retained diagnostic ownership and inspectable/missing measurements. |
| Profiler | Phase 4 plus 5(g), shared histories and usable first floating open. |
| Help | Phase 5(d), offline navigation/search and responsive topics/page view. |
| Material Editor/graph | Phase 3, adaptive composition, authored saves/undo/pack and preview lifetime. |
| Sound Editor | Phase 5(e)(2a), adaptive tree/sections and checked edit/save/audition lifecycle. |
| Sound Bank Editor | Phase 5(e)(1), bounded inventory and candidate-save publication. |
| Audio Mixer | Phase 5(e)(1) plus 5(g), adaptive pages/Apply/preview and first floating open. |
| Audio Debugger | Phase 5(e)(2b), owned captured readings and bounded inspectable pages. |
| History | Phase 5(c), explicit undo/redo/current-state navigation. |
| Application Settings | Phase 5(d), bounded properties and adaptive page list/selector. |
| Asset Manager | Phase 5(c) plus 5(g), owned inventory/filter/details and first floating open. |
| Project Settings | Phase 5(d), pinned save/pages and retained project/audio semantics. |
| Light Settings | Phase 5(d) plus 5(g), bounded authored-light controls and first floating open. |
| Scene View | Retains SceneViewPanel/EditorLayer/Viewport's specialist image, picking and gizmos; normal native Play/export is recorded above. Overlay/detached narrow context acceptance is still open. |

Workspace DrawMenu/DrawPopups/DrawPanelsMenu and EditorLayer shortcut dispatch were reviewed again.
Default/Personal ownership, deferred requests, Ctrl+1–9 ordering, modal lifetime, category search
and recovery continue to use existing owners. The source audit identifies a remaining menu case
to reproduce: personal names reach MenuItem as labels (so ## can hide text) and the active-name
caption is unbounded. This is not yet declared accepted. Scene View overlay geometry/context and
the remaining physical platform matrix must also be resolved or specifically recorded before
closing the full goal.

### Phase 6 — Export actual panel-authored fixtures

`python tests/editor/run_remaster_export.py` now reuses passed Multi material/audio panel fixtures,
creates a fresh owned Sponza corner project, and exports/runs it through the normal editor and
Dist player path. It copies the panel-saved graph material (roughness 0.19, metalness 0.66 and its
link), the silent real-PCM Sound tree/curves/markers and Tone.wav, and saved Mixer settings. Only
clip/asset addresses are rebound to the owned fixture; every rendered mesh material slot uses the
copied graph material and the source uses its copied Sound/Bank. It hashes original model,
project, scene, registry and authored inputs and checks them after execution. PyYAML and the
existing golden/scene-acoustics helpers are reused; this is developer verification, not a new
product dependency or asset schema.

Final native Windows run passed: `bin/remaster-export/remaster-export-1793895895488521300`, with
capture/export/player evidence in `bin/golden/remaster-export-1793895895488521300` and runner log
`bin/remaster-authored-export.txt`. The packaged player ran with matching direct-ray bits,
raw/occlusion gain 0.23425025, fourteen opening paths and listener reverb within tolerance. Normal
export includes the copied graph/Sound/Mixer data and the generated shader pack; material/source
pack value assertions remain in their owning panel suites. This run is native exported-result
coverage, not a new pixel-reference comparison or performance benchmark.

The new log gate rejects NRI swapchain creation failures, null resources, descriptor-bake failures,
shader load failures, VUIDs and assertions, in addition to the existing report checks. It accepted
this run's Play and packaged-player logs and rejected the actual failing Wayland material log.
The final gate was checked against these exact positive/negative artifacts after the native run;
no source/runtime behavior changed afterward. The runner's imported helpers generate Python
bytecode, now ignored by the associated __pycache__ rule.

An initial fixture-read failure exposed EditorAssetManager's existing Windows generic_string()
registry encoding. The verifier explicitly reads the current Windows ANSI format when UTF-8
fails on Windows; it never rewrites original registries, and Linux users are told to recreate a
native fixture instead of guessing a legacy encoding. The exported owned registry is UTF-8.
A project-registry encoding migration is outside this visual remaster and has not been smuggled
into the asset format. That portability limitation remains explicit.

The additional Linux X11 material presentation attempt timed out at the existing 180-second
runner bound while pipeline creation was still logged. Evidence: `bin/remaster-linux-x11-material.txt`
and `bin/material-panel-smoke/1793895340364476788`. It is not accepted native preview presentation.
No test/engine timeout was widened, renderer failure hidden or validation setting disabled.
Both Linux presentation findings remain explicit platform gaps. Windows generation remains
restored, and unrelated CI/build-doc/sample changes from concurrent work are excluded from this
batch. Local cr reviewed fixture isolation, source hashes, address rebinding, explicit failure
paths and log gates; no remaining must-fix/should-fix finding in this test-only change.

**Next remaining acceptance work:** literal/long personal-layout menus and empty panel search,
combined dirty Beam/material/Sound workspace transitions, Scene View overlay/context review,
physical mixed-monitor/detached/disconnect/drag checks, and the specifically unaccepted Linux
presentation path. The goal remains active; no complete-remaster claim is made.

### Workspace menus - bounded literal names and search recovery

Reproduced an unbounded workspace menu with a long personal name in the real editor's ImGui
self-tests (`bin/workspace-smoke/1793896916312511200`). Workspace and deleted-layout rows now
use the shared bounded literal-text control, keyed by stable record IDs. Names containing `##`
or `%` remain literal, rename does not change widget identity, and full names/shortcuts remain
available on hover. The active-name caption uses the shared bounded property label. The menu's
preferred width follows font size and is capped to the main viewport work area. Default/personal
ownership, Ctrl+1-9 ordering, deferred requests and persistence are unchanged.

View > Panels now shows wrapped feedback and a Clear search button for unmatched queries;
clearing restores the category list without changing visibility. Tests exercise actual mouse
press/release selection and restore actions, literal names, stable IDs and bounded rows at
100%/150%, plus empty-search recovery. Final Debug Editor artifact: 2026-11-05 11:51:54,
53,531,136 bytes (`bin/workspace-menu-build-final.txt`). Single/Multi workspace lifecycle,
persistence, recovery and concurrent-save suites passed:
`bin/workspace-smoke/1793897524154723800`. Local cr found no remaining must-fix/should-fix
issues in this batch. No new source files or project regeneration were needed. Bundled Help
and Architecture section 2.9 describe the result. Physical monitor/docking and the previously
listed integration/platform acceptance work remain open.

### Combined dirty document workspace acceptance

`python tests/editor/run_workspace_documents.py` now creates isolated projects and exercises
real Beam, Material Editor and Sound Editor instances through the production PanelManager and
EditorWorkspaceManager. It keeps dirty text, material roughness, an authored graph link and Sound
pitch together while switching Shading/Audio/Scripting/Debugging/Layout and returning to them.
It verifies stable panel/Beam-document/loaded-asset identities, retained authored graph/data, and
byte-for-byte unchanged original files before any explicit save. The Sound owner finishes one
undo transaction on hide; undo/redo still works after the transitions. Space auditions a looping
silent PCM fixture in the restored Sound editor, then switching to Scripting stops that voice.
Finally, focused Ctrl+S saves from each restored real panel are reloaded/compared to the edits.

Single/Multi passed: `bin/workspace-documents-smoke/1793897927464911100`. The test hook is
non-Dist and explicitly opted in; it validates the disposable active project path, isolates
window/docking state, shares only the registered application font atlas, and introduces no
product schema, dependency, renderer wait or alternate save path. Material preview is disabled:
this does not claim native preview presentation or physical multi-monitor/docking acceptance.
Local cr reviewed main-thread ownership, balanced ImGui scopes, instance/asset lifetimes, save
isolation, silent real audio, explicit failure reports and both threading policies. No remaining
must-fix/should-fix findings in this verification batch; no new C++ file or regeneration needed.

Final formatted Debug Editor build verified at 2026-11-05 12:00:34, 53,531,136 bytes
(`bin/workspace-documents-build-final.txt`). Runtime/Core product code is unchanged.

### Scene View overlay containment and platform association

Reproduced the HUD escaping a 180x140 Scene View before the fix:
`bin/settings-panels-smoke/1793898406034712700` (Single/project.status). Scene View now captures
its current ImGui platform-viewport ID and font size at BeginImGui. PrepareOverlay reuses its
content bounds to anchor/limit corner windows and explicitly target that same platform viewport;
Hide clears the transient platform ID. HUD/legend/room readouts wrap. Simultaneous bottom HUD and
legend have separate width budgets. Settings, orientation and selection use Scene View's detached
style metrics; gear/gizmo dimensions and hit radii scale together, the gizmo fits below the gear,
and selection reads the font size captured in Scene View. The room readout reserves badge height.
The image, picking, camera math, render algorithms/resources and authored data are unchanged.

The existing settings runner now checks actual production overlay windows at wide and 180x140
sizes, 100%/150%, including a real acoustic collider and a 300-character selected name. It checks
containment, HUD/legend separation, platform-ID agreement and hidden overlay state in an isolated
ImGui context. Temporary scene/hierarchy/selection/cache/settings are restored. Final Single/Multi
pass: `bin/settings-panels-smoke/1793898965923748100`. Final Debug Editor artifact: 2026-11-05
12:15:03, 53,591,040 bytes (`bin/viewport-overlay-final-build.txt`). Local cr checked scopes/IDs,
next-window-data consumption on early returns, detached metrics, main-only access and ownership;
no remaining must-fix/should-fix finding. Architecture and bundled Help describe the behavior.

These checks establish UI geometry/data behavior in the supported main-window test context.
They do not establish physical mixed-monitor drag/disconnect or detached-window presentation,
and do not create synthetic platform backends to claim those tests. Published room-reverb data
is still covered by the audio suites; this probe does not fabricate a live reverb measurement.
Very small views can clip passive diagnostic readout content inside their bounded windows; the
main diagnostic panels retain full readings. Linux native presentation gaps remain as recorded.
No C++ file was added, so project regeneration was unnecessary.

### Follow-up - prevent vertical HUD text

The user's native screenshot exposed a gap in the preceding containment-only checks: ImGui's
AlwaysAutoResize combined with TextWrapped can shrink the width used for wrapping until the
readout becomes a one-character column, even in a large Scene View. Bounds alone do not prove
readability. HUD, acoustic legend and room readout now set an explicit preferred width in the
Scene View's font units, capped by their available panel/paired-overlay budget, before wrapping.
Only height remains content-driven. Existing positions, values and platform association remain.
The wide-view regression now requires space for a natural FPS readout, alongside the existing
containment/scale/hide/separation checks.

Single/Multi passed: `bin/settings-panels-smoke/1793899397481338500`. Final formatted Debug
Editor artifact: 2026-11-05 12:24:14, 53,591,040 bytes
(`bin/viewport-overlay-width-final-build.txt`). Native review used the disposable copied project
and private user root in `bin/viewport-overlays-native/1793899093357`. Computer Use visibly
confirmed the horizontal multi-line HUD in maximized Scene View and after restoring the editor
to a 1077x635 logical window. The settings gear popup opened and closed normally. The owned
review editor was closed through Alt+F4; `width-review.log` records normal shutdown, with no
validation/assert/null-resource/swapchain failure in that log or stderr. Earlier hidden review
launches were not accepted as visual evidence. This is main-window resize acceptance, not a
physical monitor-move/disconnect or detached-window claim. Local cr reviewed explicit-width
ordering, bounds, font scale and scope balance; no outstanding finding in this fix.

### Phase 6 - current Linux X11 execution and native failure gates

The final workspace/menu/document/overlay code rebuilt incrementally with the existing Linux
Makefiles (no generation change): Debug Editor 2026-11-05 12:32:04.495770100 -0500, 406,846,096
bytes; `bin/remaster-linux-editor-final-build.txt`. Core and Runtime product code did not change.
Windows project generation was not touched by this incremental make build.

Normal X11/XWayland execution on WSLg/lavapipe rendered an isolated copied cube/material/sky
scene through the existing golden runner, with its unchanged default 900-second capture timeout.
Evidence: `bin/golden/remaster-linux-x11-1793900290105321700` (summary/status/log/lximg/PNG),
`bin/remaster-linux-x11-native-capture.txt` and the owned source project in
`bin/remaster-linux-native/1793900290105321700`. The 320x180 image visibly contains the green cube
and sky; capture and RenderGraph self-tests passed, with normal shutdown and zero graphics/UI
failure markers. Original model-input hashes remained unchanged. One timing sample was retained
by the existing runner; no performance benchmark or hardware-GPU claim is made.

The current Linux X11 integration matrix passed Single/Multi with full native-log checks:

- Workspace lifecycle/persistence/recovery/concurrent writers: `bin/workspace-smoke/1793900457975272699`.
- Real dirty Beam/material/Sound workspace transitions, audition cleanup and restored saves:
  `bin/workspace-documents-smoke/1793900486579336786`.
- Settings/Help/Game View and final readable Scene View overlay geometry at 100%/150%:
  `bin/settings-panels-smoke/1793900530724886619`.
- Material saves/retry/reload, undo, adaptive graph and real preview frames:
  `bin/material-panel-smoke/1793900568967692881`.
Full orchestration: `bin/remaster-linux-x11-final-matrix.txt`. No native runner timeout or engine
validation behavior was changed. This successful X11 material run supersedes the earlier X11
timeout for that supported backend; the recorded Wayland surface failure remains unaccepted.

`tests/editor/native_checks.py` now supplies the native graphics/UI log gate to eleven editor
runners, including authored export. It rejects the observed NRI swapchain failure even when a
self-test writes PASS, as well as surface loss, null resources, descriptor bake errors, shader
load failures, Vulkan validation and assertions. It reports the failing file/line, while expected
authoring save failures and unavailable timings remain testable. The stdlib regression tests
passed on Windows and Linux. All runner imports were checked; 26 actual clean X11/Windows logs
were accepted, and both console/file copies of the recorded failing Wayland Single log were
rejected. Initial scratch verifier input classification was corrected to use only logs that
actually contain a failure; a clean historical Multi log is not labeled failing by assumption.
The Windows unit run required the ordinary host temporary directory after sandbox temp I/O
was denied. No application behavior was changed to bypass that environment restriction.

The updated Windows workspace runner also passed Single/Multi with the shared checker:
`bin/workspace-smoke/1793900923618682700`. Local cr reviewed imports, expected-error handling,
log I/O failures and exact rejection evidence; no remaining must-fix/should-fix finding in this
Python-only verification batch. Existing fresh native builds above remain authoritative.


### Phase 6 - focused floating Scene View overlays

Native detached-window review exposed a second overlay issue after the wrapped-HUD width fix:
focusing the floating Scene View raised its image window above the separate corner overlay roots.
The platform IDs and bounds were correct; ImGui display ordering hid the overlays.

`Viewport::BeginOverlay` now places an obscured overlay immediately above its scene dock-tree root.
It relocates the overlay beside the owner before swapping their display positions, preserving the
owner's ordering relative to other windows, keyboard focus and later popups. All six Scene View
corner overlay paths use this helper and retain their matching End scopes. No renderer resources,
waits, workspace schema or authored data changed.

The production overlay regression now focuses the existing floating scene on subsequent frames,
checks every submitted corner window's display order and verifies scene keyboard focus. It still
checks wide/narrow containment, platform association, readable HUD width and 100%/150% metrics.
Before the fix it failed with "Focused Scene View covered its corner overlays" at
`bin/settings-panels-smoke/1793901762798836900/Single`. After the fix:

- Windows Single/Multi PASS: `bin/settings-panels-smoke/1793901880469831200`.
- Linux X11 Single/Multi PASS: `bin/settings-panels-smoke/1793902166175681223`.
- Windows Debug Editor artifact: 2026-11-05 13:04:10, 53,591,040 bytes,
  `bin/viewport-overlay-order-final-build.txt`.
- Linux Debug Editor artifact: 2026-11-05 13:09:13.843253600 -0500, 406,847,168 bytes,
  `bin/viewport-overlay-order-linux-build.txt`.

Computer Use in the disposable personal layout under
`bin/workspace-platform-native/1793900647090929900` verified native Scene View creation after host
restore, a readable horizontal HUD, visible gear/orientation controls after activation and scene
clicks, and settings popup above the overlays. The final fully visible review is
`order-final-native.log`; normal shutdown and the shared graphics-failure gate are clean. A prior
cropped-window review is not used to claim full corner-control visibility. Local cr found no
blocking findings; no source files were added, so project regeneration was unnecessary.

Phase 6 remains active: the remaining detached mixed-DPI/monitor acceptance and known failed
Wayland path retain their prior limits. A separately identified PanelManager scale lookup still
uses the Profiler's menu label instead of its actual window title and needs its own verified fix.


### Phase 6 - actual-title detached panel scaling

PanelManager's pre-Begin `ScopedWindowScale` used the menu label. Profiler retains its stable
icon-prefixed ImGui title, so the lookup missed its detached window and left main-display metrics
in place on another DPI. The manager now resolves `PanelData::GetWindowName()`, matching docking,
restore and tab selection; panels without a custom title retain the existing fallback.

The workspace integration test registers a real icon-titled panel, creates its ImGui window and
observes the manager's pre-Begin metrics across main/detached 100%/150%/200% combinations, including
a repeated return. The synthetic metric target is restored before Render; it is explicitly not a
native platform backend. Padding/spacing must match the target and the main style must be restored.
Before the fix the new regression failed at `bin/workspace-smoke/1793902366353777300/Single.log`.
After the fix, all lifecycle/persistence/recovery checks and the scaling regression pass:

- Windows Single/Multi: `bin/workspace-smoke/1793902479280597000`.
- Linux X11 Single/Multi: `bin/workspace-smoke/1793902913017610957`.

Fresh artifacts (2026-11-05 local): Windows Debug Core 13:13:39 and Editor 13:13:46
(`bin/profiler-window-scale-final-build.txt`), Debug Runtime 13:14:54
(`bin/profiler-window-scale-debug-runtime-build.txt`), Dist Core 13:15:17 and Runtime 13:16:07
(`bin/profiler-window-scale-dist-runtime-build.txt`). Linux Editor 13:21:13.508770100 -0500,
406,854,816 bytes (`bin/profiler-window-scale-linux-recovery-build.txt`); Linux Debug Runtime
13:23:39.598162400 -0500, 321,040,032 bytes (`bin/profiler-window-scale-linux-runtime-build.txt`).
The first Linux build's postbuild copy met a Windows review process holding Coral.Managed.dll.
After closing that private process, the exact failed managed deployment commands completed and
the Editor/runtime builds finished. No timeout, validation filter or engine workaround was added.

Native Computer Use used private project/user state in
`bin/profiler-dpi-native/1793902557413689100`. ImGui Metrics showed the real monitors:
4K main/work bounds (0,0)-(3840,2160/2088), 150%; Full HD bounds (3840,0)-(5760,1080/1032), 100%.
`mixed-native.log` records the accepted run. A real native 900x900 Profiler at (4100,150) showed
readable charts, labels and hit targets at 100% alongside the main editor at 150%. Pause worked;
resume/follow was verified after moving/maximizing the main editor on Full HD. The main workspace
retained its sidebar and bottom-panel proportions through the physical 4K-to-Full-HD move and
Full HD maximize/restore. Native viewports were created/merged by ImGui as expected. Normal
shutdown and the shared graphics-failure gate are clean. Local cr found no blocking findings.
No files were added; no project regeneration was needed.

Remaining: the return move was not accepted because seam-cropped captures do not supply reliable
input coordinates. The normal/half-screen Full HD window also exposed project breadcrumb and
transport overlap in the titlebar; fix and verify that next. Repeated mixed-monitor/disconnect
acceptance and the previously recorded Wayland presentation failure remain explicit. Phase 6 is
still active; this batch proves the actual-title scaling repair and the stated native cases only.


### Phase 6 - bounded titlebar identification and scene controls

The native Full HD normal/half-screen case exposed project/scene text beneath the centered scene
controls. The breadcrumb clipped only at the native window buttons. The titlebar now budgets the
menus and window buttons before placing DPI-scaled scene controls, clips identification before the
control hit rectangle and keeps full project/scene labels available by hovering LUX. If the inline
group cannot fit, a labeled Controls popup retains Select/Move/Rotate/Scale and Play/Simulate/Stop.
Returning to inline mode closes that owned popup. Placement clamps remaining space, avoiding
absolute clamp-bound inversion at fractional DPI. No workspace or asset schema changed.

Local review also reproduced a keyboard leak: R in the open popup reached scene tool selection.
Popup navigation consumes an unmodified key by matching a menu item's first letter, so scene
shortcuts yield only for unmodified keys while an ImGui popup is open. Local review then
reproduced a second, wider leak from the first guard shape: swallowing every key made Ctrl+S
(Save Scene), Ctrl+Z/Ctrl+Y (Undo/Redo) and Ctrl+O (Open Project) dead keys whenever any popup
was open, with no feedback. Ctrl/Shift chords therefore stay live. Normal scene shortcuts resume
after dismissal; menu actions retain their existing scene lifecycle methods.

The settings runner now renders the actual titlebar with long literal project/scene names at
640/958/1077/1920 logical-width cases and 100%/130%/150% metrics, with and without scene gizmos.
It checks real draw-command clip bounds, menu/window-control separation, native caption hit-test
exclusion, keyboard popup opening, Move selection, popup teardown on widening, key isolation and
shortcut restoration. The chord case pairs with the unmodified-key case inside one build: the popup
guard returns true for an unmodified key and false for a Ctrl chord, so a guard that swallows every
key fails the second assertion. Key::A stands in for the chord because no handler acts on it, which
keeps the probe from mutating the fixture scene. Baselines failed as intended:

- `bin/settings-panels-smoke/1793903802294431500/Single/editor.log`:
  "Titlebar breadcrumb can draw over scene controls".
- `bin/settings-panels-smoke/1793905326890844600/Single/editor.log`:
  "A titlebar popup key also changed the scene tool".

Final Single/Multi PASS:

- Windows: `bin/settings-panels-smoke/1793905580588789500`.
- Windows after the chord refinement: `bin/settings-panels-smoke/1793919488370319700`.
- Linux X11: `bin/settings-panels-smoke/1793905730157477983`.
- Linux X11 after the chord refinement, Single and Multi:
  `bin/settings-panels-smoke/1793922233655377200`.

Fresh Editor artifacts on 2026-11-05: Windows Debug 14:03:21, 53,612,544 bytes
(`bin/titlebar-overlap-final-build.txt`); Windows Dist 14:08:57, 11,202,048 bytes
(`bin/titlebar-overlap-dist-final-build.txt`); Linux Debug 14:05:39.487930800 -0500,
406,876,736 bytes (`bin/titlebar-overlap-linux-final-build.txt`). Windows Debug was relinked after the chord
refinement at 17:57:36, 53,613,056 bytes (`bin/titlebar-chord-build-evidence.txt`). Linux Debug
was rebuilt with the refinement through `make config=debug Editor` after a `clang++` symlink into
the Ubuntu user bin (the distribution ships only versioned compiler aliases) and with the loader
paths `scripts/Linux-Run.sh` exports: the first attempt stopped at
`libassimp.so.5: cannot open shared object file`, the staged path then passed. Fresh Linux Editor
18:40, 406,876,984 bytes. Core/runtime code is unchanged
from the preceding verified actual-title DPI batch. No files were added; no regeneration was needed.

Native Debug Computer Use used the disposable project/user directory
`bin/profiler-dpi-native/1793902557413689100`. `titlebar-final-native.log` covers 1077 to 523
logical-pixel narrowing, readable Controls access, selected Rotate feedback, Play/Stop and
Simulate/Stop with correct active/disabled feedback, the complete-name LUX tooltip, and maximize
back to a 2560x1396 logical client with inline controls and readable identification. The last input
repair was then verified in `titlebar-keyboard-native.log`: Move remained selected after R in the
popup, and R selected Scale after Escape. The chord refinement was verified in the C++ suite
only; no native session was re-run for it, so native Ctrl+S behavior is unproven. Both sessions
shut down normally with clean graphics failure gates. Local cr reported one should-fix, the chord
narrowing above, applied in this batch. Bundled interface help and Architecture
2.9 describe the new behavior. Original project data and unrelated staged changes are excluded.

Dist native acceptance remains unproven. A launch in
`bin/titlebar-dist-native/1793904929631969800` terminated before showing a window. Its console
files are empty because Dist has no console sink; the authoritative file
`user/logs/LUX_2026-11-05-13-55-37_88816.log` reports shader load failures beginning at line 40,
and the shared native checker correctly rejects it. Shader-debug traced the configuration:
`ShaderLibrary::Load` compiles only with `LUX_HAS_SHADER_COMPILER` (`Renderer/Shader.h:16`,
`!LUX_DIST`), while the editor entry point leaves `RenderConfig.ShaderPackPath` unset
(`Editor/Source/LuxEditorApp.cpp:88`, commented as `Resources/ShaderPack.lsp`). The only pack
writer is `WriteRuntimeShaderPack` (`Editor/Source/EditorLayer.cpp:381-407`), reached from
runtime export into the exported package, so a standalone Dist Editor has neither a compiler nor a
pack to load: the failure is the missing editor-side pack, not a titlebar regression. No shader source, compiler flag or renderer fallback was changed here.
The user chose the editor-side pack route. The recorded plan: a documented regeneration step
in a compiler-enabled editor writes `Editor/Resources/ShaderPack.lsp`, which is checked in; the
Dist Editor sets `RenderConfig.ShaderPackPath` to it and fails loudly (log plus non-startup) when
it is absent or stale; a stamp manifest carries the shader-source set hash and the generating
device vendor/driver, validated on load before any frame. No format migration of `LXSP` v1 was
planned at this point: the manifest is a sidecar, so existing exported packages keep reading the
same binary. Implementation is the next batch, not this one. Phase 6 remains
active, including the previously recorded physical-monitor/reachability and Wayland limits.

### Phase 6 - editor-side shader pack for the Dist Editor

Implements the route recorded above. The Dist Editor sets `RenderConfig.ShaderPackPath` to
`Resources/ShaderPack.lsp` (`LuxEditorApp.cpp`, `LUX_DIST` only). Application Settings gains an
Editor Shader Pack row: Refresh shader pack (compiler-enabled builds) writes the pack through a temp
file and then `<pack>.stamp`, re-reading both; Re-check pack state recomputes the cached verdict.
`Renderer::Init` evaluates the pair before loading: changed sources or an absent, truncated or
empty pack stop the launch with `LUX_CORE_VERIFY`; another GPU and an unstamped (exported) pack only
warn. LXSP v1 is unchanged, so existing packages read the same bytes. `VulkanDevice` keeps the
vendor and driver it already reads. Architecture 2.3 describes shader packs.

Verified: `scripts/Lux-BuildTest.ps1 -Configurations Debug,Dist` PASS
(`bin/soundwave-tests/build-20261110-000351-057`); Dist Editor built from `Editor.vcxproj` with
`SolutionDir` set (`bin/shaderpack-dist-editor-build.txt`, 11,219,456 bytes).

Refreshed from the Debug editor through Application Settings, driven with Computer Use
(`bin/shaderpack-refresh-editor.txt`): 56 programs, source set 2206879063, RTX 4070 Ti. The stamp
recorded `__HZ_AO_METHOD=2;__HZ_GTAO_COMPUTE_BENT_NORMALS=1`, set by SceneRenderer from project
settings, while `Renderer::Init` starts with bent normals `0`: comparing macros would have refused
every refreshed pack in Dist. Macros are now recorded but not compared. After deleting build
outputs that were dated in the future by a system clock change and relinking both configs, the
Dist Editor started and opened its window with the pack
(`%APPDATA%/LuxEngine/logs/LUX_2026-11-09-14-36-47_15124.log`): no shader or pack errors; the
only errors are the existing Dist refusal to import source meshes (Sponza.gltf). Nobody has yet
looked at the rendered Dist viewport.
Phase 6 remains active.
