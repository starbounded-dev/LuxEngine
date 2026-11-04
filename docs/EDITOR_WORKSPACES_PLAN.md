# LuxEngine Editor Workspaces Plan

Planning document, 2026-11-04. Part One makes the editor's existing tools easier to arrange and keeps those arrangements usable across displays. Part Two preserves the creative-tool ideas for later planning. This document describes proposed work; the authority for the implementation remains `.claude/docs/Architecture-LuxEngine.md` §§2.1, 2.2, 2.9 and 2.13.

## Goal card

- **Goal:** Blender-inspired workspace tabs for LuxEngine, with protected Default layouts, editable Personal layouts, and reliable resizing and monitor changes.
- **Success:** move the editor between 4K and Full HD, restore/maximize it, switch workspaces and restart without having to repair panel arrangements.
- **Success:** start from a task-focused default, rearrange its panels, and choose **Save as Personal Layout** to keep the result.
- **Part One:** existing panels, layout management, display scaling, persistence and recovery.
- **Part Two:** retain modeling, sculpting, UV, painting and animation ideas as a future roadmap, with prerequisites identified.
- **Constraints:** keep ImGui and Vulkan; no mandatory external software; preserve existing user layouts and project/scene/asset compatibility; Windows and Linux; both rendering thread policies.
- **User emphasis:** “I have to rearrange everything every single time”; “default layouts and ... personal layouts.”

## Decisions and proposed behavior

| Decision | Choice | Consequence |
|---|---|---|
| Initial scope — user confirmed | Workspace management using existing panels | New creative editors do not delay Part One. |
| Future scope — user requested | A separate Part Two roadmap | Ideas are retained without treating their underlying systems as already working. |
| Storage scope — user confirmed | Personal workspaces across projects | Machine-local preferences; project files do not impose somebody else's monitor arrangement. |
| Display behavior — user confirmed | One adaptive arrangement | No separate monitor profiles in the first version. |
| Layout types — user specified | **Default layouts** and **Personal layouts** | Built-in defaults cannot be overwritten; keeping a customized default creates a personal copy. |
| Personal persistence — proposed detail | Automatically remember changes to Personal layouts | No repetitive Save action after every splitter drag. Errors remain visible and do not claim success. |
| Modified defaults — proposed detail | Retain edits in memory during this session, including tab switches; restart restores the factory default | Show a clear modified indicator and **Save as Personal Layout**. Never present a default as durably saved. |
| Workspace contents — technical choice | Arrangement, visibility, selected dock tabs and explicitly approved presentation state | Scene, selection, loaded documents, dirty asset data and undo history remain session/document state. |
| Instances — technical choice | One live instance of each existing panel | Switching workspaces does not clone renderers or create competing document models. Multiple instances can be planned separately. |
| Implementation — technical choice | Keep ImGui docking behind a small adapter | No new UI toolkit or dependency; isolate version-sensitive internal APIs. |

The last two persistence details are the proposed interpretation of the user's Default/Personal model, not a claim that the user explicitly specified autosave timing or default-session lifetime.

## Current-state ledger

Source inspected at HEAD `ecd0fd1a`, including the working tree. Existing staged changes touch `PanelManager.cpp`, `ImGuiLayer.cpp`, panel code and shared docs; implementation must preserve and reconcile that work. Nothing here has been verified in a running editor during this planning session. The reported display problem is user-observed; its complete causal chain is still unproven.

| Capability | State | Evidence read in this session |
|---|---|---|
| Docking and detached OS panel windows | Built, runtime unproven here | `Core/Source/Lux/ImGui/ImGuiLayer.cpp` — `OnAttach`: docking enabled; platform viewports enabled on Windows, disabled on Linux. |
| Built-in Simple/Advanced arrangements | Built | `Editor/Source/EditorLayer.cpp` — `ResetDefaultDockLayout`, `SetEditorLayoutMode`; `EditorLayer.h` — `m_SimpleLayout`. |
| Named saved layouts | Built | `EditorLayer.cpp` — `GetSavedLayouts`, `SaveLayout`, `LoadLayout`, `UI_SaveLayoutPopup`: per-user `Layouts/<name>/imgui.ini` plus `Panels.yaml`. |
| Panel state persistence | Built | `Core/Source/Lux/Editor/PanelManager.{h,cpp}` — `Serialize`, `Deserialize`, `OnProjectChanged`, `SavedIsOpen`; `EditorPanel.h` — `SaveState` / `LoadState`. |
| Automatic initialization detection | Defect in the condition; contribution to this report unproven | `EditorLayer::OnImGuiRender` treats `!DockBuilderGetNode(...)->IsSplitNode()` as needing defaults. An intentionally unsplit layout is valid and will be reset by this rule. |
| Initial UI scaling | Built, calculated at startup | `ImGuiLayer::OnAttach` computes content/framebuffer scale, calls `Fonts::SetScale`, and scales the style. `ImGuiLayer::Begin` does not repeat that style/font policy on monitor changes. |
| Main-window content-scale callback | Built; incomplete UI propagation | `Core/Source/Lux/Core/Window.cpp` — `Window::Init`: callback updates the device manager's DPI scale, not the startup font/style policy. |
| Windows process DPI setup | Needs investigation, not a proven absence of DPI awareness | `Window::Init` makes a process-awareness call after GLFW initialization; vendored `Core/vendor/GLFW/src/win32_init.c` — `_glfwInitWin32` already requests per-monitor V2 where supported. Inspect actual process/thread awareness before changing it. |
| Version-specific scaling APIs | Available, experimental parts need a spike | `Core/vendor/imgui/imgui.h`: **1.92.6 WIP**, `IMGUI_VERSION_NUM=19255`, `ConfigDpiScaleFonts`, `ConfigDpiScaleViewports`, `FontScaleDpi`; GLFW header reports **3.5.0** in the vendored fork. |
| Per-monitor information | Already available | `Core/vendor/imgui/backends/imgui_impl_glfw.cpp` — `ImGui_ImplGlfw_NewFrame`, `ImGui_ImplGlfw_UpdateMonitors`: refresh display/framebuffer sizes, work areas and DPI scales. Reuse this information. |
| Deferred ini load | Built; not a complete workspace transaction | `ImGuiLayer::QueueIniSettingsLoad` / `Begin`: apply ini before `NewFrame`; `EditorLayer::LoadLayout` changes panel states immediately. Vendored `imgui.cpp` — `LoadIniSettingsFromMemory` merges window settings; absence from an ini does not guarantee removal of previous window placement. |
| Independent Scene View visibility | Missing from the panel registry | `EditorLayer::OnImGuiRender` draws `m_EditorViewport` directly; `Viewport::BeginImGui(bool*)` supports an open flag but is not managed like registered panels. |
| Independent Hierarchy and Properties visibility | Coupled today | `Core/Source/Lux/Editor/SceneHierarchyPanel.cpp` — `OnImGuiRender` draws both windows through the same `isOpen`; a collapsed hierarchy also returns before Properties is drawn. |
| Task-specific existing tools | Built; runtime unproven here | `EditorLayer::OnAttach` registers Material Editor, Sound Editor, Sound Bank Editor, Audio Mixer, Beam, Game View, renderer/debug panels, History and Help. |
| Hidden-panel lifecycle | Requires explicit integration | `TextEditorPanel.h` — `OnClose` clears keyboard focus; `SoundEditorPanel.h` — `OnClose` clears focus and stops audition. Bulk visibility changes currently do not offer a workspace-specific transition contract. |
| Atomic persistence helpers | Available | `Core/Source/Lux/Utilities/FileSystem.h` — `WriteFileAtomically`, `ReplaceFileAtomically`, `GetPersistentStoragePath`; replacement implementations in `Core/Platform/{Windows,Linux}/*FileSystem.cpp`. |
| Material graph editor | Built, not a missing feature | `Editor/Source/Panels/MaterialEditor/MaterialGraphCanvas.h` — `Draw`; `MaterialEditorPanel.h` — `m_GraphCanvas`; `Core/Source/Lux/Renderer/MaterialGraph/MaterialGraph.h` — graph model, compilation and serialization. |
| Mesh/UV data | Built, not an authoring document | `Core/Source/Lux/Renderer/Mesh.h` — `Vertex::Texcoord`, `VertexAttributes::UV1`, `MeshSource::GetVertices`, `GetIndices`, `BuildRenderGeometry`. A render/import representation is not proof of editable topology. |
| Skeletal animation end to end | Not established; do not promise “just add a timeline” | `Asset/AssetTypes.h` lists Skeleton/Animation/AnimationGraph, but `Mesh.h` has commented skeleton accessors; `SceneRenderer.cpp` marks bone-transform fields unused/no animation. No animation implementation files appeared in the inspected source inventory. |

Historical context: commit `f6c22ea7` introduced remembered panel state and named layouts. `docs/Editor/Architecture.md`, `Panels.md` and `Theme-Fonts-Icons.md` explain earlier intent but contain stale paths/claims; source and shared architecture guidance take precedence. No existing dedicated workspace plan was found.

## Part One — Workspaces and display reliability

### 1. User experience

A dedicated row below the editor title/menu/transport bar presents workspace tabs. This avoids stealing the titlebar drag region or crowding Play/Stop. At narrower widths, tabs scroll or enter an overflow menu; window controls remain reachable.

**Proposed factory defaults:**

| Default layout | Arrangement using existing tools |
|---|---|
| Layout | Scene View central, Hierarchy left, Properties right, Content Browser and Log below; Game View available as a tab. |
| Shading | Material Editor dominant, Scene View beside it, Content Browser below, Properties available. The existing material editor keeps its preview and graph UI. |
| Audio | Sound Editor dominant, Audio Mixer and Sound Bank Editor alongside/below, Content Browser available; Audio Debugger accessible. |
| Scripting | Beam dominant, Content Browser left, Log below; Scene/Game View available as secondary tabs. |
| Debugging | Scene/Game View central, Profiler, Renderer Debugger, Scene Renderer, Light Settings and Log in supporting docks. |

These names and arrangements are recommendations. Do not create nonfunctional Modeling/Sculpting/Animation tabs in Part One.

Workspace controls:

- **Add:** choose a Default layout or duplicate an existing Personal layout.
- **Save as Personal Layout:** copy the current arrangement, including temporary changes made to a Default layout, under a new personal name.
- **Personal layout menu:** Rename, Duplicate, Reset to its recorded starting default when applicable, and Delete. Reset/delete offer recovery; never discard scene or document edits.
- **Default layout menu:** Restore Default and Save as Personal Layout. Factory names and definitions remain protected.
- **Panels:** a searchable, categorized menu and matching command-palette actions. Initial categories: Scene, Assets & Authoring, Scripting, Diagnostics, Settings & Help. Do not copy Blender's menu categories where Lux has no corresponding tool.
- Panel close, redock and reopen work consistently for Scene View, Game View, Hierarchy, Properties and the other registered panels.
- Keep at least one reachable workspace. Deleting the active personal layout selects Layout; deleting the final personal layout never removes factory defaults.

Opening an asset reveals/focuses its existing editor in the active workspace; it does not unexpectedly switch workspaces. Workspace changes do not alter the scene, selected entities, playback mode or renderer quality. Play can reveal Game View as it does today without replacing the chosen workspace. Preserve the pre-Play visibility/focus state for Stop rather than permanently rewriting the personal arrangement with a transient Play adjustment.

### 2. Display contract

Treat three distinct things correctly: available window/work-area dimensions, OS content scale, and framebuffer pixels per ImGui unit. A 4K resolution does not itself imply a particular scaling percentage.

- A resize or monitor crossing keeps the same dock topology and panel membership. It must not invoke the default-layout builder.
- Maintain intended splitter proportions; enforce readable minimum sizes in UI units. When constrained, support scrolling/overflow rather than hiding controls or expanding docks beyond the host.
- Preserve the user's unconstrained arrangement when a temporary small window forces clamping. Returning to the larger size restores the intended proportions; resizing alone must not become a destructive user-layout edit.
- Detached windows keep usable titles/controls on an available monitor. On disconnect, recover only windows outside all work areas, using the nearest available area/main window. Handle negative desktop coordinates and taskbars.
- Prevent scale accumulation: apply scaling from an unscaled style baseline, never repeatedly multiply the already scaled style. Account for the current font bake scale to avoid scaling text twice.
- Support a main editor on one DPI and a detached panel on another simultaneously. A secondary window must not change the global style for every other window. The vendored ImGui style limitation makes this an explicit spike/exit gate, not a one-flag promise.
- Leave scene render sizes in physical pixels through `Viewport::GetOutputSize` / `SyncSize`. Verify mouse picking and gizmos against the image bounds after a DPI change.

Prefer the existing ImGui dock representation and native ini snapshots. The first spike must prove reversible resizing and workspace isolation with this version. If ini snapshots plus bounded adapter metadata cannot preserve the required proportions, stop and revise the adapter design before building a second full docking model. No vendored-library edits or UI-framework replacement are planned.

### 3. Ownership, state and persistence

**NEW** `Editor/Source/Workspace/EditorWorkspaceManager.{h,cpp}`: owned by `EditorLayer` as a `Scope`; owns factory definitions, personal records, tab order, active workspace and temporary default edits. UI commands only request transitions. It uses Core's panel framework; Core never includes this editor-application header.

**NEW** `Editor/Source/Workspace/WorkspaceDocking.{h,cpp}`: small adapter for capture/restore, default construction, geometry metadata and version-sensitive ImGui internals. Use stable workspace IDs independent of names. Keep existing panel/window identities unless a deliberate migration is supplied. Renaming a workspace cannot break docking.

**Extend** `PanelManager` with in-memory capture/apply and visibility-change notifications; separate that from writing the legacy global panel file. Supply window identity/menu metadata, not another copy of the actual panel model. An explicit panel hide clears input/focus/capture and performs existing hide cleanup without unloading dirty documents. Add a visibility hook if `OnClose` cannot safely express both meanings.

**NEW** `Editor/Source/Panels/PropertiesPanel.{h,cpp}`: a thin independently registered window delegating inspector drawing to the existing scene-hierarchy/controller code. Refactor the hierarchy's draw entry points so Properties does not depend on Hierarchy being visible. Reuse selection, callbacks, component UI and undo; do not duplicate them. Preserve the existing embedded `isWindow=false` use contract.

Scene View uses an explicit workspace visibility flag and window descriptor connected to its existing `EditorLayer` draw/update path; no renderer-owning replacement panel is required. Check all call sites that assume Scene View is drawn, especially focus/capture, overlays, camera sizing and Play/Stop.

**Proposed record:** schema version, stable ID, display name, origin/default ID, opaque ImGui ini text, window visibility, approved presentation state and display-restoration metadata. Personal layout names are data, never directory names. Factory definitions are compiled in; no resource deployment step or new dependency is needed.

Keep per-user data under `FileSystem::GetPersistentStoragePath()`. A single versioned **NEW on-disk** `Workspaces.yaml` holds personal layouts, tab order and active workspace, so geometry and panel visibility commit together. Temporary modifications to factory defaults are excluded. Missing optional keys retain defaults; unknown window records survive round trips for forward compatibility. Future unsupported schemas are reported and preserved, not overwritten.

Capture ImGui and panel state on the main thread. Debounce after completed layout gestures; switch requests capture the outgoing workspace in memory. Do not serialize/copy every workspace every frame. Respect `io.WantSaveIniSettings` if taking over ini persistence with `io.IniFilename=nullptr`.

**NEW** `Editor/Source/Workspace/WorkspaceStore.{h,cpp}` handles checked atomic writes and immutable snapshots. A single named `Lux::Thread` writer, owned through `Scope`, coalesces pending revisions and writes outside the UI thread under both threading policies. It uses `FileSystem::WriteFileAtomically`; never give it ImGui objects, panels, live YAML node views, scenes, assets or `EditorLayer` pointers. Flush/join before destruction; report errors on main and retain the dirty revision for retry. Load/parse the small bounded preference file at startup, before the first splash frame can mutate saved geometry. Preserve a validated last-good record for recovery.

**Switch transaction:** request → wait for active edits/drag/modal document decisions to finish → capture outgoing arrangement → validate incoming snapshot → restore dock/window state and visibility together before the next `NewFrame` → draw the incoming workspace → persist the active ID and personal changes. Coalesce rapid switches; default initialization and legacy migration happen exactly once. Scope the restoration to workspace windows; preserve unrelated table/preferences state. Failed loads retain the outgoing arrangement and report the failing layout. The existing queued ini-load hook is an integration point, not proof that this transaction already exists.

**Legacy migration:** import current `imgui.ini` + `EditorLayout.yaml` as **Personal Layout (Imported)**, import each valid `Layouts/<name>/` as another personal layout, retain names with stable unique IDs, and preserve the old Viewport→Scene/Game View migration. Old coupled Hierarchy visibility initializes both new windows. Keep the original files intact; write migration completion only after the new store commits. If current files are absent, use the old Simple/Advanced preference to choose Layout/Debugging. Once migrated, retire competing automatic writes from the old global panel/ini paths and route project changes through the new owner. A project switch updates panel context without reloading an unrelated global layout.

### 4. Engine fit

| Concern | Contract |
|---|---|
| Main thread | Workspace commands, panel draw/visibility, ImGui capture/restore, GLFW access and project transitions; identical contract under Single and Multi. |
| Writer thread | Only owned immutable persistence data and filesystem I/O; named `Lux::Thread`, `LUX_PROFILE_THREAD`; no JobSystem I/O that would run inline under Single. |
| GPU lifetime | Switching layouts hides/reuses live panel instances; it does not tear down their renderers, scenes or assets. Preserve `ImGuiLayer::End` weak viewport-data captures and existing deferred resource frees. No `WaitIdle` workaround. |
| Renderer/packet | Layout metadata does not enter the renderer or `FrameRenderPacket`; existing viewport-size paths remain responsible for render dimensions. No shaders, binding slots, pipelines or render-graph changes planned. |
| Ownership | Manager/adapter/store use `Scope`; existing panels remain `Ref<EditorPanel>`. Serialized snapshots contain values, not pointers. |
| Serialization | User preferences only; no `.luxproj`, `.luxscene`, asset or ScriptCore format changes. Presentation-state allowlist excludes project/renderer quality and asset edits. |
| Runtime/Dist/export | No workspace dependency in `Lux-Runtime` or exported packs. Shared ImGui changes must preserve runtime overlay startup, font display and shutdown in Debug and Dist. |
| Linux | Keep detached platform windows disabled; verify docked workspaces on X11 and Wayland and fractional framebuffer scaling. Do not enable unsupported platform viewports as part of this work. |
| Build/dependencies | New source files require `scripts\Win-GenProjects.bat --last`; Linux uses `scripts/Linux-Build.sh`. No new dependency, mandatory install or build toggle. |
| Documentation | Update shared Architecture §§2.1/2.2/2.9/2.13 with each owning phase, Threading for the writer, Conventions for any new canonical helpers, and the bundled editor interface manual. Root `docs/` plans remain point-in-time records. |

### 5. Implementation phases

Every phase below leaves a usable editor. Use `$dev` before coding and `$cr` for local review. Build evidence means fresh artifacts, never exit status alone.

#### Phase 1 — Explain the monitor and resize failure

**Goal:** a repeatable case and a verified scaling/restore strategy before committing to a workspace architecture.

**Changes:** investigate `Window::Init`, `ImGuiLayer::OnAttach/Begin`, `EditorLayer::OnImGuiRender/ResetDefaultDockLayout`, and vendored settings/scaling behavior. Use existing ImGui metrics and narrowly scoped opt-in diagnostics to record content/framebuffer scales, work areas, dock IDs/sizes, layout-reset calls and window state. Compare equal-DPI resize against mixed-DPI monitor movement. Prototype isolated snapshot restore and reversible constraints without replacing production layout management.

**Playbook:** none unless a probe adds a source file, in which case regenerate. No new render feature or panel.

**Thread/lifetime:** diagnostics and docking probes on main; retain existing GPU ownership. No worker/render mutations.

**Verify:** build Debug Editor if probes change code; verify its timestamp. With an isolated copy of user preferences, run the transition matrix below. Prove an unsplit user layout survives; prove whether the existing startup-only scale is part of the observed failure. Exercise a detached panel on a different DPI. Check ImGui scopes/IDs and validation logs. Record actual Windows scaling percentages; do not infer them from resolution.

**Docs:** add verified observations to the implementation record in this plan; shared docs change only if shipped behavior changes.

**Exit:** before/after captures and transition traces identify each cause; exact vendored-version restore and mixed-DPI policy are proven or the design is revised. **Rollback:** remove optional probes; retain original preferences and current editor behavior.

#### Phase 2 — Existing layouts survive display transitions

**Goal:** fix the reported problem independently of workspace tabs.

**Changes:** `ImGuiLayer.{h,cpp}` and `ImGuiFonts.{h,cpp}` implement the Phase 1 scaling policy using the unscaled baseline and dynamic font facilities verified against 1.92.6 WIP. Cover font-size/index consumers, custom titlebar heights, dock minimum sizes and hit-test regions. `Window.cpp` changes only where the observed callback/awareness path requires it; do not override GLFW callbacks or process awareness blindly. `EditorLayer` replaces the split-node heuristic with explicit initialization/restore status and uses the actual dock host content rectangle for factory construction. Restore/recovery preserves topology and reachable floating windows without rebuilding defaults on resize.

**Playbook:** if new files, regenerate; platform-specific changes require the corresponding platform implementation or an explicit no-op rationale. No vendor edits. No new editor panel.

**Thread/lifetime:** all geometry/UI changes on main before frames; existing font-texture updates and render snapshots retain their upload/release contract. No captured font/viewport pointer may outlive its owner.

**Verify:** Debug Editor and Lux-Runtime; fresh timestamps. Repeat monitor/resizing matrix in Single and Multi. Verify sharp readable fonts, no double scaling, no split collapse or cumulative drift, accurate picking/gizmos/titlebar controls and no validation/assert/ID errors. Compare default and custom themes after repeated scale changes. Verify Linux docked scaling when available; report unavailable hardware rather than claiming a pass.

**Docs:** Architecture §§2.2/2.9, Conventions font/scaling helper contract if changed.

**Exit:** the existing layout survives repeated 4K↔Full HD and maximize/restore cycles without repair. **Rollback:** revert this phase's behavior changes; legacy files remain readable and intact.

#### Phase 3 — Every existing window can be managed consistently

**Goal:** independently open/close Scene View, Hierarchy and Properties, with a common in-memory layout snapshot.

**Changes:** `PanelManager.{h,cpp}` gains capture/apply, window metadata and explicit visibility transitions; `EditorPanel.h` gains a visibility hook only if required. `SceneHierarchyPanel.{h,cpp}` separates hierarchy and inspector draw entry points. Add **NEW** `PropertiesPanel.{h,cpp}` and register it. `EditorLayer` adds Scene View visibility/metadata without duplicating its renderer, integrates menu/palette actions and retains correct scene updates while Scene View is hidden. Migrate the old shared visibility flag. Fix focus/audition cleanup when panels become hidden.

**Playbook — new panel, all steps:** derive PropertiesPanel from EditorPanel under `Editor/Source/Panels`; implement `OnImGuiRender(bool&)` and context delegation; register with `AddPanel`; add menu/command toggle; regenerate projects. Reuse existing inspector ownership and undo callbacks.

**Thread/lifetime:** main-thread registry/UI; `Ref` to shared hierarchy/controller with no ownership cycle. Hidden documents stay alive; hide resets input state and stops audition without discarding work.

**Verify:** Debug Editor and Runtime, timestamps. Hide/reopen each window independently, including Properties while Hierarchy is collapsed; scene selection, component edits/undo and embedded hierarchy use still work. Test empty scene, no project, Play/Stop, hidden Scene View, Beam edits, unsaved material/sound and audition. Save/load the legacy layout with missing new fields. Inspect every Begin/End and stable ID, including popup/dock names; no conflicts or validation output.

**Docs:** Architecture §2.9 and bundled `Editor/Resources/Docs/editor/interface.md`.

**Exit:** all managed windows capture/restore visibility consistently without lost document data or stale input. **Rollback:** restore legacy coupling and menus; retain original layout files.

#### Phase 4 — Two workspaces work end to end

**Goal:** Layout and Scripting tabs switch independently, and a customized default can become a durable Personal layout.

**Changes:** add **NEW** manager, docking adapter and store files defined above. `EditorLayer::OnAttach/OnDetach/OnImGuiRender` wires ownership, startup-before-splash restore, transactional switches and clean shutdown. Extend `ImGuiLayer`'s queued restore boundary so dock/window state and panel visibility change together. Implement Save as Personal Layout, personal autosave and tab order/active ID. Disable competing legacy auto-writes only after migration is committed. Keep old named-layout import accessible until Phase 5 completes its UI.

**Playbook — new thread, all steps:** read Threading; use a named `Lux::Thread`, profile its body, pass only immutable owned values, prohibit ECS/asset/ImGui access, no GPU work in the writer (any future GPU work must use `Renderer::Submit`); stop/coalesce/flush/join explicitly. This is I/O, not a data-parallel JobSystem job. Regenerate all new files.

**Thread/lifetime:** capture/apply on main; persistence on owned writer under both policies. Complete active widget/undo transactions before hiding panels; do not switch through a modal document decision. Preserve ImGui viewport weak captures.

**Verify:** Debug Editor/Runtime with fresh artifacts. Rearrange each tab, switch back, rename a Personal layout and restart; arrangements remain distinct and correctly named. Alter a default, switch away/back, save a personal copy; restart restores factory defaults and personal changes. Test rapid switches, a legitimate one-node layout, pending text/gizmo edits, dirty documents and write failure. A load failure retains the outgoing workspace. Exercise every new UI state and scope/ID branch; zero validation errors.

**Docs:** Architecture §§2.1/2.9/2.13, Threading writer lifecycle, bundled interface manual.

**Exit:** Default→Personal creation and two-workspace restore survive restart with atomic persistence and safe focus/document behavior. **Rollback:** disable the new manager and restore backed-up legacy preferences; do not delete the new store.

#### Phase 5 — Complete Default and Personal layout management

**Goal:** all proposed task defaults and the complete user-facing management flow are available.

**Changes:** manager/adapter factory definitions add Shading, Audio and Debugging using registered windows only. Add Personal Rename/Duplicate/Delete/Reset, tab reorder/overflow and categorized searchable Panels menu. `EditorLayer::RegisterCommands` and Application Settings replace Simple/Advanced as the primary control while retaining migration. Finish named-layout import, retain unavailable/unknown panel records, and resolve duplicate names using stable IDs. Route asset-open and Play/Stop focus/visibility through the active workspace owner.

**Playbook:** no additional editor panel is required; regenerate if implementation adds files. Do not split the material graph/preview or add a creative editor in this phase.

**Thread/lifetime:** manager/UI on main, immutable saves through the existing writer; no panel recreation or renderer teardown on tab switches.

**Verify:** Debug Editor/Runtime, timestamps. Exercise every default, all actions and error/empty states; long/unicode/duplicate names, many tabs at 1080p, narrow window overflow, delete active/last personal layout, reset recovery, asset opens and Play/Stop. Confirm unsaved Beam/material/sound data survives. Confirm `.luxproj` and asset data are unchanged by workspace actions. Check scopes, stable reorder IDs, popup names and docking IDs in every branch; no ImGui or validation errors.

**Docs:** Architecture §2.9, bundled interface manual and shortcuts if changed. Update source-grounded helper guidance when adding a reusable widget; do not rewrite historical plans as current architecture.

**Exit:** user can start from any default and keep/manage a personal variant without file manipulation. **Rollback:** disable newly added actions/defaults; existing personal data remains readable.

#### Phase 6 — Recovery and platform proof

**Goal:** prove the complete feature across migrations, display changes, persistence faults and supported builds.

**Changes:** bounded store validation and recovery, last-good fallback, unplugged-monitor handling and targeted fixes found by the verification matrix. Migration must not mark success before atomic publication. Preserve unsupported future schemas and malformed legacy sources. Add focused automated coverage for data/migration/transaction behavior; do not add tests that merely mirror ImGui calls.

**Playbook:** no new subsystem expected; apply regeneration/platform playbooks if fixes add files or platform behavior.

**Thread/lifetime:** test close during queued save, rapid revisions, writer errors and detached-window destruction under Multi; Single must also avoid long I/O in the UI loop.

**Verify:** Windows Debug Editor/Runtime and Dist Runtime, plus Linux Debug Editor/Runtime and Dist Runtime when the host is available; fresh artifact timestamps for each. Smoke-test exported runtime and its overlay to establish shared ImGui changes did not introduce editor dependencies. Run the full matrix below, including corrupt/truncated/missing/read-only layout data and interruption during save. Logs identify failures with layout/path/recovery action; no silent success or Vulkan error suppression.

**Docs:** synchronize shared architecture/threading/helper contracts and bundled manual with the shipped behavior; record actual verified platforms and remaining limitations.

**Exit:** all acceptance cases pass or a concrete unsupported case is explicitly resolved before shipping. **Rollback:** recover the prior store/legacy data and previous implementation without altering projects or dirty assets.

### 6. Whole-feature verification matrix

Use disposable copied preferences and a sample project; never reset the user's real arrangement as a test setup.

| Scenario | Observable pass condition |
|---|---|
| 4K↔Full HD, both directions, equal and mixed scale | Topology/membership persist; fonts and controls resize correctly; no repair required. |
| 100/125/150/200% where available | Correct content-vs-framebuffer handling, crisp font/icon rendering and accurate input. |
| Maximize/restore, snap, manual resize, minimize/restore | No spontaneous default reset or saved small-window corruption; intended proportions return. |
| At least 20 repeated monitor/size cycles | No cumulative scaling or splitter drift. Before/after geometry within rounding tolerance established in Phase 1. |
| Detached panel and editor on different DPIs | Each remains usable; moving one does not resize the other's UI; no stale render-thread pointer. |
| Secondary monitor disconnect/reconnect; negative coordinates | Off-screen windows become reachable; valid windows are not unnecessarily moved. |
| Workspace switching in Edit/Play/Simulate | Scene/playback/selection and dirty documents retained; hidden panels cannot consume stale shortcuts or leave mouse capture active. |
| Default edits, personal copy and restart | Defaults return to factory state; personal layout preserves changes; clear saved/unsaved distinction. |
| A→B→A and restart, including a single dock | No geometry/visibility bleed, no one-node reset, active dock tabs restored. |
| Old current layout and all named layouts | Non-destructive, repeatable migration, names preserved and original sources recoverable. |
| Missing/truncated/unsupported schema, unknown panel, write failure | Useful error and recovery, previous good state retained, no false saved/migration-complete status. |
| Project change, no project, scene load, application shutdown | Correct startup order, no layout overwrite during splash, clean writer/viewport lifetime. |
| Linux X11/Wayland, Single/Multi and runtime Dist | Supported docking/scaling works; no new dependency on editor workspaces in runtime/export. |

## Part Two — Future creative editors

This is a retained product roadmap, not executable phases or a promise of existing support. Each item needs its own source-grounded `$plan-le` or Spec Kit design before code. Workspace infrastructure should allow its registration later; it must not ship empty placeholders now.

### 1. Proposed editor families

| Family | Future panels/tools | Initial useful slice | Prerequisites and proof of completion |
|---|---|---|---|
| Modeling | Mesh editor; vertex/edge/face selection; topology tools and tool settings | Edit a private mesh document: select, transform, extrude/inset simple geometry; undo, save, reload | **NEW** editable topology/document model; submesh/material/UV preservation; authored mesh format and import/reimport policy; normals/tangents, LOD/meshlet and collider invalidation. Exported runtime renders the result. Do not mutate every user of a shared imported mesh accidentally. |
| UV editing | 2D UV canvas, island selection, seams, transforms, checker preview; later unwrap/packing | Inspect UV0/UV1, then edit islands with linked 3D selection | **NEW** per-corner/seam-capable representation and asset save path; synchronize selection and material preview; rebuild tangent-dependent data. A UV viewer can ship first without claiming unwrap support. Any unwrap/packing library needs license/size review and a separate decision. |
| Sculpting | Brush shelf/settings, sculpt viewport; later symmetry, masks and resolution tools | Bounded fixed-topology push/pull/smooth on an owned mesh document | Modeling document and ray picking, brush falloff/spacing, acceleration structure, efficient updates and stroke undo budget. Remesh/multiresolution are separate designs. **Mesh sculpting is not a terrain implementation**; any terrain initiative starts independently from scratch. |
| Texture/image work | Image viewer/editor, texture paint canvas and 3D painting, layers later | Image inspection first; then one brush/layer/channel set with explicit Save/Revert | **NEW** image-document ownership and writable source format; color-space/channel rules, stroke undo, seams/projection, thumbnail and material invalidation, asset packing. No mandatory external image editor. |
| Animation | Timeline, Dope Sheet, Graph Editor; later skeletal clip preview and state machine | Transform/property keyframes on one entity: scrub, key, interpolate, undo, save and replay in runtime | **NEW** clip format/evaluation/time ownership and property binding; Edit preview vs Play behavior; prefab/scene/export compatibility. Skeletal import, skeletons, skinning and graph evaluation need a separate end-to-end audit; enum entries are not implementation. Timeline and Dope Sheet can share one keyframe model. |
| Shading | More independently arranged material graph, preview, node inspector and asset picker | Optionally expose the existing material document's views as dockable subpanels | Reuse `MaterialGraphCanvas`/`MaterialGraph`; **NEW** shared document/view contract if separated. Preserve graph compile, material preview, dirty state and undo. Do not create a second material editor model. |
| Geometry nodes | Procedural geometry graph, viewer, node inspector and attribute spreadsheet | Small deterministic mesh-generator graph that can bake to an authored mesh | Modeling/attribute model first; **NEW** geometry graph types, evaluator, cycle/error handling, budgets and bake/export policy. The material shader graph is not a geometry evaluator. |
| Rendering/compositing | Still-render/capture queue, image viewer, spatial compositor | One camera still render saved at a chosen resolution; inspect result | Audit existing capture/export paths in that feature's plan; **NEW** authoring/output lifecycle as needed. Color management, cancellation and output errors; no temporal accumulation, TAA or temporal rendering techniques. |
| Scripting and data | Optional C# interactive console; richer asset/file browser; attribute spreadsheet | Build on Beam, Log, Content Browser and existing Settings/Help | **NEW** interactive execution contract if a console is wanted; use the already required .NET environment. No mandatory Python interpreter. Data spreadsheets follow a concrete attribute model rather than becoming a generic placeholder. |
| Video/sequencing | Video Sequencer, clip editor/tracking, NLA/drivers | Retain as ideas; no implementation commitment | Independent media/codec, dependency/license, timing and evaluation designs. NLA/drivers follow working animation; they are not workspace features. |

### 2. Likely future workspace compositions

- **Modeling:** 3D View + mesh tools + hierarchy/properties.
- **UV Editing:** UV canvas + 3D View + image/asset picker.
- **Sculpting:** sculpt viewport + brush shelf/settings + hierarchy.
- **Texture Paint:** image canvas + 3D paint viewport + brush/layer controls.
- **Animation:** Scene View + Timeline/Dope Sheet + Properties; Graph Editor available.
- **Geometry Nodes:** node graph + 3D result + attribute spreadsheet.
- **Rendering:** camera/result view + output/capture settings; compositing only once its editor exists.

These are compositions of real editors once delivered, not separate copies of the same tools. Default/Personal layout behavior stays the same across them.

### 3. Suggested order and product gates

After Part One, prioritize one complete authoring workflow at a time. Suggested dependency order: editable mesh documents → basic modeling and UV inspection/editing → texture painting and fixed-topology sculpting. Animation can be planned as an independent track, beginning with transform/property clips before skeletal animation. Geometry nodes comes after authored geometry and attributes; NLA/drivers after animation. Shading improvements reuse the existing material editor.

Before choosing the first creative feature, the user decides: which workflow matters most; game-focused modeling versus broader DCC goals; transform animation versus character animation; and what source asset formats should remain editable. No schedule or dependency cost is claimed before those designs and audits exist.

Every future editor must include document ownership, dirty-state recovery, undo/redo, asset serialization, editor integration, runtime/export parity and safe unsupported-data behavior. For a new panel follow all four Architecture Part 4 steps: derive/implement/context hooks, register, menu toggle, regenerate. New asset types also require enum, class, serializer, importer and extension registration; new components require copy/prefab/serializer/inspector and packet paths where renderer-visible. Apply the renderer/thread playbooks whenever those designs introduce GPU work. Do not revive deleted terrain, ray-tracing or GI attempts as shortcuts.

## Risks, unknowns and invalidation gates

| Risk / unknown | Handling |
|---|---|
| Exact monitor bug has not been reproduced here | Phase 1 obtains traces on the actual display setup; startup scaling, minimum sizes and reset heuristics are candidates, not a complete diagnosis. |
| ImGui's global style is limited for simultaneous mixed DPI | Phase 1 verifies scoped per-window metrics/font behavior with the vendored version. If consistent geometry cannot be achieved without vendor changes, revise the approach with explicit evidence before Phase 2. |
| Loading ini settings merges more state than expected | Adapter spike verifies replacement/isolation and preserves non-workspace settings. No blind global `ClearIniSettings` as a substitute for scoped restoration. |
| Small-window clamping permanently rewrites intended ratios | Keep requested geometry distinct from constrained presentation; test round trips and only classify actual user layout edits as changes. If this cannot be proven with the proposed snapshot adapter, revisit the representation. |
| Visibility changes discard state or retain focus/audition | Explicit transition hook, live document ownership and dirty-document/input tests in Phases 3–5. |
| Project callback reloads legacy layout over a workspace | Phase 4 makes one owner authoritative and audits `PanelManager::OnProjectChanged` plus `EditorLayer` startup/shutdown. |
| Async save publishes an old revision or outlives its owner | Single serialized writer, monotonic revision IDs, main-thread completion and flush/join; failure keeps dirty state. |
| Concurrent editor instances write one per-user store | Phase 4 must detect external revisions and preserve conflicts/recovery copies rather than silently lose changes. Exact conflict UI is an implementation decision; multi-instance behavior is a required test. |
| Linux display hardware is unavailable in a Windows session | Record unverified cases and obtain Linux execution before declaring platform acceptance. |
| Part Two grows into a DCC rewrite | Keep it a separately prioritized roadmap. No blank panels or speculative engine abstraction in Part One. |

The plan must be revisited if the spike disproves scoped ini isolation or reversible resizing, if mixed-DPI correctness requires a dependency/vendor change, or if the user chooses multiple independent instances/documents per workspace. Future creative editors require their own plans rather than extending Part One implicitly.

## Research brief — workspaces, docking and DPI

Goal: task-focused, editable workspace tabs whose layouts survive monitor and window changes.

- **Prior art:** Blender exposes task-based workspaces as topbar tabs with duplicate/add/rename/delete controls. This supports using the user's familiar workspace model. Lux's protected Default/Personal distinction is the user's own product choice. [Blender developer documentation](https://developer.blender.org/docs/release_notes/2.80/ui/)
- **Persistence trade-off:** Blender can store workspaces in its project file; Lux deliberately uses personal, machine-local layouts per the user's decision. [Blender workspace manual](https://docs.blender.org/manual/id/dev/interface/window_system/workspaces.html)
- **Docking:** ImGui already supplies docking, user-driven splitting and programmatic initial layouts; `DockBuilder` remains internal/version-sensitive. Encapsulate it rather than distributing calls across panels. [ImGui docking documentation](https://github.com/ocornut/imgui/wiki/Docking)
- **Lifecycle pitfall:** dockspaces must be submitted before hosted windows and kept alive if hidden. Lux's transition boundary must maintain that order; hiding the workspace is not a reason to accidentally undock its windows. [ImGui docking documentation](https://github.com/ocornut/imgui/wiki/Docking)
- **DPI:** ImGui 1.92 supports dynamic font sizes; font scaling and style/padding scaling are distinct, and mixed-DPI style handling remains limited. Do not assume two experimental flags repair the complete editor. Checked against local `imgui.h` and `docs/FAQ.md` for **1.92.6 WIP**. [ImGui DPI FAQ](https://github.com/ocornut/imgui/blob/docking/docs/FAQ.md#q-how-should-i-handle-dpi-in-my-application)
- **Scaling pitfall:** style scaling multiplies values; changing scale requires an unscaled baseline. Existing startup font baking must be reconciled with dynamic DPI to avoid double scaling. [ImGui DPI FAQ](https://github.com/ocornut/imgui/blob/docking/docs/FAQ.md#q-how-should-i-handle-dpi-in-my-application)
- **Coordinates:** window content scale can change on monitor movement, and framebuffer size is a separate quantity. This informs the transition matrix and keeps UI geometry separate from rendered viewport pixels. GLFW's public guide is not proof of fork behavior; verify against Lux's vendored **3.5.0** fork/backend. [GLFW window guide](https://www.glfw.org/docs/latest/window.html#window_scale)
- **Rejected shortcuts:** replacing ImGui, hardcoding 4K/1080p profiles, rebuilding default docks on every resize, forcing a GPU idle to hide lifetime errors, or adding external mandatory tools. None fits the goal or Lux's engineering rules.

Research informs the adaptive-layout choice, scoped docking adapter, early mixed-DPI spike and workspace persistence boundaries. It does not settle the actual monitor repro, exact geometry restoration behavior or future authoring-system costs.

## Handoff

First implementation: **Phase 1 — Explain the monitor and resize failure**, followed by the independently useful reliability fix in Phase 2. Use `$dev` to implement each approved phase, `$cr` for local review, and `$send-pr` only when a pull request is requested. This planning pass changes no engine code and does not claim runtime/build verification.

## Implementation record — 2026-11-04

Part One's implementation is present in the working tree. The ledger above records the planning
baseline; the architecture documentation describes the new implementation. Part Two remains a roadmap.

Delivered: Layout/Shading/Audio/Scripting/Debugging workspace tabs, protected session-only Default
arrangements, autosaved Personal layouts shared across projects, copy/rename/reorder/reset/undo-reset,
recent-deletion recovery, categorized panel search, and independently managed Properties/Hierarchy and
Scene/Game views. Switching retains the live panel/document instances. Play's automatic Game View
reveal is transient. Legacy layouts import without modifying originals; a damaged import does not
block valid ones. The versioned store has a dedicated snapshot writer, atomic replacement, last-good
recovery, explicit recovery of damaged files, and cross-process conflict preservation.

The reliability investigation reproduced a 25% sidebar becoming 49.9% at 1080p because the central
ImGui dock preserves adjacent pixel widths. The docking adapter now retains intended split ratios
separately from size constraints and records actual splitter drags. UI metrics scale from an unscaled
baseline and fonts use ImGui's dynamic viewport DPI. Moving the native window also exposed desktop
coordinates incorrectly passed as a local menu position; that titlebar coordinate error is fixed.
Loading a workspace now detaches retired live-window settings offsets, preventing duplicated ini
records from making copied layouts float. Nested layout snapshots exposed a rapidyaml multiline
scalar assertion; explicit quoted string emission fixes it and has a regression case.

Verified on Windows:

- Fresh Debug Editor, Debug Lux-Runtime and Dist Lux-Runtime artifacts, with project regeneration.
- `python tests/editor/run_workspaces.py`: exact vendored ImGui, original failure reproduction,
  20 4K/1080p/constrained-host cycles, DPI baseline round trips, unrelated clicks under constraints,
  actual splitter drag/release followed by resizing, and a valid unsplit layout.
- `python tests/editor/run_workspace_smoke.py`: production workspace lifecycle, default protection,
  personal persistence/restart, panel identity and dirty-document retention, dock membership,
  preset selection, reset/delete recovery, Play visibility, legacy and partial imports, damaged/future
  files, and conflicting writers under Single and Multi policies. Final evidence:
  `bin/workspace-smoke/1793821305585189000/`.
- Native UI in an isolated blank project: factory switching, copying a Shading arrangement without
  undocking, restart persistence, maximize/restore, and movement from 4K/150% to Full HD/100%, maximize
  there, and return across the boundary. Panes remained docked and proportional; no Vulkan validation
  errors or ImGui assertions appeared in the session log. Evidence:
  `bin/workspace-ui/1793820675580156500/final-ui.log`.
- YAML self-tests pass. All 214 currently listed YAML assets passed parse/write/reparse comparison
  against their current values. The historical `tests/yaml/reference.txt` gate still reports existing
  asset/reference drift; its reference and user assets were not rewritten to make it pass.

Platform acceptance remains open for Linux execution, repeated physical monitor cycles, monitor
disconnect/reconnect, simultaneous detached panels at different DPIs, and an exported-runtime launch.
The Windows runtime builds prove compilation independence, not package launch coverage. Run `$cr`
before committing; no commit or pull request was created by this implementation session.

### Pre-commit review — 2026-11-04

`$cr` found and fixed two issues: malformed `PreservedPanels` could bypass load validation and fail
again while restoring the active startup layout; workspace tabs also allocated labels and ordering
buffers every frame. Preserved panel data is now validated before accepting the store, with a
startup/recovery regression case. Labels are cached, scratch buffers are reused, and capture/apply/save
and tab drawing have named profiler zones.

Project regeneration, a fresh Debug Editor artifact, docking tests and Single/Multi production
workspace smoke tests passed after the fixes. Review evidence:
`bin/workspace-cr-build.txt`, `bin/workspace-tests/1793823811663157000/` and
`bin/workspace-smoke/1793823904400787000/`. The platform and historical YAML-reference limitations
above remain; no must-fix or should-fix review findings remain open.
