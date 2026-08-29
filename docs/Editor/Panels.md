# Panels Reference

Every panel in the editor: what it is for, how to use it, how it was built, and how to change it.
All panels derive from `EditorPanel` and are registered in `EditorLayer::OnAttach`
(`Editor/Source/EditorLayer.cpp:350`–`393`). See [Architecture → Panel system](Architecture.md#the-panel-system-panelmanager)
for the mechanism; this page is the per-panel catalogue.

Panels split across two source homes:

- **Core panels** (`Core/Source/Lux/Editor/`) — reusable, engine-level panels that a runtime tool or
  another front-end could also use (Scene Hierarchy, Console).
- **Editor panels** (`Editor/Source/Panels/`) — editor-application-specific panels.

---

## Scene Hierarchy + Inspector

**Class:** `SceneHierarchyPanel` · `Core/Source/Lux/Editor/SceneHierarchyPanel.{h,cpp}`
**Registered as:** "Scene Hierarchy", open by default.

### What it is

The entity tree of the active scene **and** the property inspector for the selected entity — one
panel, split into a hierarchy region on top and a components region below (Hazel-style combined
outliner + inspector).

### How to use it

- **Select** an entity by clicking its row; the selection drives the gizmo, the inspector, and the
  viewport selection badge.
- **Reparent** by dragging one row onto another.
- **Rename** via double-click / F2.
- **Add components** through the inspector's "Add Component" button; each component draws its own
  editable widgets.
- The **Transform** component draws position/rotation/scale as three coloured X/Y/Z drag fields.

### How it was built

`SetSceneContext` (called by the manager on scene changes) points the panel at the live
`entt::registry`. The tree is drawn recursively from root entities; the inspector switches on each
attached component type and draws the matching widgets.

The transform's three-axis control is `DrawVec3Control` (`SceneHierarchyPanel.cpp`, ~line 200). It
uses ImGui's `PushMultiItemsWidths(3, …)` so the X/Y/Z fields share the row width, a coloured button
per axis, and `ImGui::DragFloat("##X", &values.x, …)` for each field.

> **Gotcha, already fixed — do not reintroduce it.** The fields must be raw `ImGui::DragFloat`, *not*
> `ImGuiEx::Property("##X", …)`. `Property` draws its label as visible `ImGui::Text`, so the `##X`
> would render literally, and it pushes/pops its own item width (ignoring `PushMultiItemsWidths`),
> breaking the layout. Row vertical spacing comes from a trailing `ImGui::Dummy(ImVec2(0, 5))` and
> the axis buttons are separated with `ImGui::SameLine(0.0f, 8.0f)`.

### How to modify

- **Add a component to the inspector:** add a `DrawComponent<YourComponent>(...)` block in the
  inspector section, following an existing component as a template. (Also update `Components.h`, the
  serializer, copy/prefab paths — see the silent-failure checklist in `.claude/skills/cr/SKILL.md`.)
- **Change transform field spacing/precision:** edit `DrawVec3Control` — the `"%.2f"` format, the
  `0.1f` drag speed, the `Dummy` height, the `SameLine` gap.

---

## Log / Console

**Class:** `EditorConsolePanel` · `Core/Source/Lux/Editor/EditorConsolePanel.{h,cpp}`,
with `EditorConsole/ConsoleMessage.h` and `EditorConsole/EditorConsoleSink.h`.
**Registered as:** "Log", open by default.

### What it is

The in-editor log. It receives engine and script log messages through a **spdlog sink**
(`EditorConsoleSink`, a `friend class`) and renders them as a filterable, colour-coded list.

### How to use it

- Messages are tagged **INFO / WARNING / ERROR** and tinted accordingly.
- Filter by level and clear the log from the panel's controls.
- Script `Console.Log`-style calls and engine `LUX_*` logs both land here.

### How it was built

`EditorConsoleSink` implements spdlog's sink interface and pushes each formatted record as a
`ConsoleMessage` into the panel's buffer (thread-safe, since logs can come from any thread). The
panel renders rows with the message body and timestamp in the **Mono** font for alignment, the
timestamp dimmed (`textDarker`), and the level tag coloured:

```cpp
s_InfoTint    = ImVec4(0.471f, 0.667f, 1.0f, 1.0f);   // blue
s_WarningTint = ImVec4(0.878f, 0.635f, 0.302f, 1.0f); // amber
s_ErrorTint   = ImVec4(0.910f, 0.329f, 0.329f, 1.0f); // red
```

### How to modify

- **Retune the level colours:** edit `s_InfoTint` / `s_WarningTint` / `s_ErrorTint` in
  `EditorConsolePanel.cpp`.
- **Change the row font:** the row wraps the timestamp+message in
  `ImGuiEx::Fonts::PushFont("Mono")` / `PopFont()` — swap the font name to restyle.
- **Capture a new log source:** anything logged through the engine's spdlog logger already appears;
  no wiring needed.

---

## Content Browser

**Class:** `ContentBrowserPanel` · `Editor/Source/Panels/ContentBrowserPanel.{h,cpp}` +
`Editor/Source/Panels/ContentBrowser/ContentBrowserItem.{h,cpp}`.
**Registered as:** "Content Browser", open by default.

The asset browser is large and was recently rebuilt (grid/list, type-filter chips, sort, favourites,
details footer). It has its **[own page: Content Browser](Content-Browser.md)**.

---

## Beam (text editor)

**Class:** `TextEditorPanel` · `Editor/Source/Panels/TextEditorPanel.{h,cpp}`.
**Registered as:** "Beam", open by default.

### What it is

An in-editor, multi-tab code/text editor (branded **Beam**), used to edit script and text files
without leaving the editor. Built on the ImGuiColorTextEdit widget with a custom themed shell.

### How to use it

Open a file by activating a script (`.cs`) in the Content Browser — the editor forces the Beam panel
open and loads the file into a new tab (`EditorLayer.cpp:450`+). Shortcuts (Beam must be focused):

| Shortcut | Action |
|---|---|
| `Ctrl+N` | New file |
| `Ctrl+S` | Save current tab |
| `Ctrl+W` | Close current tab |
| `Ctrl+F` | Find / Replace |
| `Ctrl+G` | Go to line |
| `Ctrl+PageDown` / `Ctrl+PageUp` | Next / previous tab |

(`Ctrl+Tab` is intentionally **not** used — ImGui reserves it for window navigation;
`TextEditorPanel.cpp:249`.)

### How it was built

Each open file is a `Document { TextEditor Editor; std::filesystem::path Path; bool Dirty; }`, held in
a `std::vector<Scope<Document>>` with `m_ActiveDocument` as the current index. The shell adds:

- A **"BEAM" wordmark** (Display font) and a toolbar of icon buttons (new/save/find — FontAwesome
  glyphs `LUX_ICON_FILE_O`, `LUX_ICON_FLOPPY_O`, `LUX_ICON_SEARCH`).
- A **Mono status bar** (line/column, dirty state).
- A **tab bar**, one tab per document, id'd by the document's address so tabs stay unique.
- Shortcut handling in `HandleShortcuts()` (`TextEditorPanel.cpp:226`), and a go-to-line popup and
  find/replace window.

### How to modify

- **Add a shortcut:** extend `HandleShortcuts()` — guard on `io.KeyCtrl` and
  `ImGui::IsKeyPressed(ImGuiKey_X, false)` (the `false` disables repeat).
- **Restyle the shell:** the wordmark uses `ImGuiEx::Fonts::PushFont("Display")`, the status bar
  `"Mono"`; toolbar buttons go through the panel-local `ToolbarIconButton(...)` helper.
- **Change what opens in Beam:** the Content Browser activation callback for `AssetType::ScriptFile`
  in `EditorLayer.cpp` decides which asset types route here.

---

## Scene Renderer

**Class:** `SceneRendererPanel` · `Editor/Source/Panels/SceneRendererPanel.{h,cpp}`.
**Registered as:** "Scene Renderer", closed by default (Advanced layout).

### What it is

The control surface for **every renderer setting** of the active scene's `SceneRenderer` — quality
presets, culling, LODs, mesh shaders/VRS, render-scale modes, GTAO, SSR, SMAA, shadows, bloom, DOF,
and the **scene post-processing** (exposure, tonemap, colour grading). It also owns the **Save
Renderer Settings** button (project-level) and the "Unsaved renderer settings" dirty indicator.

### Key design facts

- Two accumulators drive the end-of-frame refresh: `projectSettingsChanged` (writes back to project
  settings via `SyncProjectSettingsFromContext()`) and `screenSpaceResourcesChanged` (triggers
  `m_Context->RefreshScreenSpaceEffectResources()`). Every setting `|=`'s the right one.
- **Post-processing** (exposure/tonemap/grading) is saved with the **scene**, not the project — the
  scene owns those settings now that per-volume post-processing is gone
  (`SceneRendererPanel.h:17`).
- Some settings gate on GPU capability (`Renderer::SupportsMeshShaders` /
  `SupportsVariableRateShading`) and show a disabled fallback when unsupported.

### How to modify

- **Add a setting:** add the widget in the matching section, wire it to the `SceneRendererOptions`
  field, and `|=` the correct accumulator. Preserve the clamps and the `m_Context->` refresh calls.
- A from-scratch presentation revamp of this panel (search box + collapsible category cards + a
  modern toggle widget) is designed in `.claude/plans/lexical-scribbling-robin.md` — read it before
  restyling; the key risk is silently dropping a setting or a `|=`.

---

## Renderer Debugger

**Class:** `RendererDebuggerPanel` · `Editor/Source/Panels/RendererDebuggerPanel.{h,cpp}`.
**Registered as:** "Renderer Debugger", closed by default.

The deep per-pass GPU profiler: a frame-history plot and a per-pass GPU-time bar chart (ImPlot),
plus render-graph introspection. Distinct from **Statistics**, which is the lightweight always-on
overview. Use this when you need to see where GPU time goes pass-by-pass.

---

## Statistics

**Class:** `StatisticsPanel` · `Editor/Source/Panels/StatisticsPanel.{h,cpp}`.
**Registered as:** "Statistics", closed by default.

> A lightweight, always-available performance overview: a Tracy-style frame-time timeline plus the
> headline CPU/GPU/draw/memory metrics. Deliberately separate from the `RendererDebuggerPanel`, which
> keeps the deep per-pass profiler. (`StatisticsPanel.h:10`)

Numbers use the **Mono** font; the timeline uses ImPlot. Use this for an at-a-glance health check.

> **Build note:** `StatisticsPanel` is a source file that must be in the generated project. On
> Windows, adding it requires re-running `scripts\Win-GenProjects.bat`, or you get
> `LNK2001 unresolved external symbol … StatisticsPanel::OnImGuiRender`. Premake does not regenerate
> itself (see `.claude/docs/Building.md`).

---

## Application Settings

**Class:** `ApplicationSettingsPanel` · `Editor/Source/Panels/ApplicationSettingsPanel.{h,cpp}`.
**Registered as:** "Application Settings", closed by default.

Editor-wide / engine-wide settings that are not project-specific: VSync, target frame rate (0 =
unlimited when VSync off), swapchain buffer count, present mode (Immediate vs Mailbox), and the
**core threading policy** (Single vs Multi — the setting that `LuxEditorApp.cpp` reads from
`App.lsettings` at startup). Constructed with the content-browser handle, the editor-preferences
bindings, and the user preferences (`EditorLayer.cpp:388`).

---

## Asset Manager

**Class:** `AssetManagerPanel` · `Editor/Source/Panels/AssetManagerPanel.{h,cpp}`.
**Registered as:** "Asset Manager", closed by default.

A view into the `AssetManager`'s registry — every known `AssetHandle`, its type, and its source path.
Use it to inspect/debug asset import state (which handles resolved, which are missing). See the asset
pipeline section of `CLAUDE.md` for the underlying model.

---

## Project Settings

**Class:** `ProjectSettingsWindow` · `Editor/Source/Panels/ProjectSettingsWindow.{h,cpp}`.
**Registered as:** "Project Settings", closed by default.

Per-project configuration (the `Project` object's settings) — physics, scripting, renderer defaults,
and the startup scene. Changes here write to the `.luxproj` file.

---

## Light Settings

**Class:** `LightSettingsPanel` · `Editor/Source/Panels/LightSettingsPanel.{h,cpp}`.
**Registered as:** "Light Settings", closed by default.

Scene-level lighting/environment controls (sky, ambient, environment map) that aren't tied to a
single light entity.

---

## Material panels (contextual)

**Classes:** `MaterialsPanel`, `MaterialEditorPanel` · `Editor/Source/Panels/`.

The material list and the per-material editor (albedo/metallic/roughness/normal/emissive + texture
slots). These are used contextually (opened for a selected material asset) rather than as
always-registered View panels.

---

## Where to go next

- The Content Browser has its own deep-dive: **[Content Browser](Content-Browser.md)**.
- To add your own panel: **[Extending → Add a panel](Extending.md#recipe-add-a-new-panel)**.
- Panel styling (fonts, colours, icons, the `ImGuiEx` helpers): **[Theme, Fonts & Icons](Theme-Fonts-Icons.md)**.
</content>
