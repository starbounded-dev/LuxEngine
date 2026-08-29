# Editor Architecture

How the editor is wired together: the layer, the panel system, the panel contract, scene states, and
the docking/layout system. Read this before touching any editor-wide behaviour.

---

## The one layer: `EditorLayer`

The editor is a single engine `Layer`. Everything the editor does hangs off
`EditorLayer` (`Editor/Source/EditorLayer.h`, `Editor/Source/EditorLayer.cpp`).

### What it owns

| Member | Type | Role |
|---|---|---|
| `m_PanelManager` | `Scope<PanelManager>` | Owns and drives every dockable panel |
| `m_EditorViewport` | `Ref<Viewport>` | The 3D scene view (framebuffer + scene renderer + camera) |
| `m_SceneRenderer` | `Ref<SceneRenderer>` | High-level renderer for the active scene |
| `m_EditorScene` / `m_ActiveScene` | `Ref<Scene>` | The scene being edited vs. the scene currently ticking (they differ during Play) |
| `m_SceneHierarchyPanel`, `m_SceneRendererPanel`, `m_ConsolePanel`, … | `Ref<…>` | Direct handles the layer keeps to panels it talks to often |
| `m_GizmoType` | `int` | Active ImGuizmo op (`-1` none, translate/rotate/scale) |
| `m_SceneState` | `enum SceneState` | `Edit` / `Play` / `Simulate` |
| `m_SimpleLayout` | `bool` | Editor layout mode (see [Layout modes](#layout-modes-simple-vs-advanced)) |

### Lifecycle methods (engine `Layer` overrides)

- **`OnAttach`** — one-time setup: load resources, build the viewport + renderer, register panels,
  restore project/scene and preferences. (`EditorLayer.cpp:348`+)
- **`OnUpdate(Timestep)`** — per-frame simulation: camera update, scene tick (edit/play/simulate),
  render the viewport, mouse-picking bookkeeping.
- **`OnImGuiRender`** — per-frame UI: titlebar, menubar, dockspace, panels, viewport overlays.
- **`OnEvent(Event&)`** — routes input to the camera, the panels (`m_PanelManager->OnEvent`), and the
  editor's own handlers (`OnKeyPressed`, `OnMouseButtonPressed`, `OnTitleBarHitTest`).
- **`OnDetach`** — teardown; saves preferences.

### How it was made

`EditorLayer` is deliberately a **single fat layer** rather than many layers. The engine's layer
stack is for cleanly separable subsystems; the editor is one cohesive tool, so keeping it in one
layer means the viewport, the scene state machine, and the panels all share one owner and one frame
lifecycle without cross-layer plumbing. The cost is that `EditorLayer.cpp` is large (~3000 lines);
the panels are the escape valve — anything self-contained becomes an `EditorPanel` instead of more
`EditorLayer` code.

---

## The panel system: `PanelManager`

`PanelManager` (`Core/Source/Lux/Editor/PanelManager.{h,cpp}`) is the registry and driver for panels.

### The data model

```cpp
struct PanelData {
    const char* ID   = "";     // stable string id, hashed to a uint32 key
    const char* Name = "";     // display name (menu + window title)
    Ref<EditorPanel> Panel = nullptr;
    bool IsOpen = false;       // persisted open/closed state
};

enum class PanelCategory { Edit, View, _COUNT };   // which menu the panel lists under
```

Panels are stored in `std::array<unordered_map<uint32_t, PanelData>, _COUNT>` — one map per category,
keyed by `Hash::GenerateFNVHash(ID)`.

### Registering a panel

Three `AddPanel` overloads (all in `PanelManager.h`):

```cpp
// The one used in practice — constructs the panel in place:
template<typename TPanel, typename... TArgs>
Ref<TPanel> AddPanel(PanelCategory category, const char* strID,
                     const char* displayName, bool isOpenByDefault, TArgs&&... args);
```

`AddPanel` hashes the id, refuses a duplicate (logs `PanelManager` error, returns null), stores the
`PanelData`, and returns the typed `Ref` so the caller can keep a handle. All registration happens in
`EditorLayer::OnAttach` (`EditorLayer.cpp:350`–`393`):

```cpp
m_SceneHierarchyPanel = m_PanelManager->AddPanel<SceneHierarchyPanel>(
    PanelCategory::View, SCENE_HIERARCHY_PANEL_ID, "Scene Hierarchy", true);
```

The panel id strings are `#define`d at the top of `EditorLayer.cpp` (e.g. `SCENE_HIERARCHY_PANEL_ID`
= `"SceneHierarchyPanel"`, `EditorLayer.cpp:69`).

### What the manager does each frame / event

- **`OnImGuiRender`** — iterates every category, and for each open panel calls
  `panel.Panel->OnImGuiRender(panel.IsOpen)`. The `IsOpen` bool is passed by reference so a panel that
  draws its own close button (via `ImGui::Begin(name, &isOpen)`) flips the manager's stored state.
- **`OnEvent`** — forwards the event to every panel's `OnEvent`.
- **`SetSceneContext` / `OnProjectChanged`** — fan out the active scene / project to every panel.
- **`Serialize` / `Deserialize`** — persist which panels are open (see [Persistence](#persistence)).

### Getting a panel back

```cpp
Ref<TPanel> GetPanel(const char* strID);   // by id, searches all categories
PanelData*  GetPanelData(uint32_t panelID); // raw data incl. IsOpen — used to force a panel open
```

`EditorLayer` uses `GetPanelData(Hash::GenerateFNVHash("TextEditorPanel"))` to open Beam when a
script file is activated in the Content Browser (`EditorLayer.cpp:453`).

---

## The panel contract: `EditorPanel`

Every panel derives from `EditorPanel` (`Core/Source/Lux/Editor/EditorPanel.h`) — a tiny virtual
interface:

```cpp
class EditorPanel : public RefCounted {
public:
    virtual void OnImGuiRender(bool& isOpen) = 0;          // required: draw the panel
    virtual void OnEvent(Event& e) {}                      // optional: handle input
    virtual void OnProjectChanged(const Ref<Project>&) {}  // optional: project switched
    virtual void SetSceneContext(const Ref<Scene>&) {}     // optional: active scene changed
    virtual void OnClose() {}                              // optional: cleanup on close
};
```

That is the whole contract. A panel is any `RefCounted` class implementing at least
`OnImGuiRender`. The `bool& isOpen` is the manager's persisted open flag — pass it to
`ImGui::Begin(Name, &isOpen)` so the window's `×` button closes the panel and the state persists.

See [Extending → Add a panel](Extending.md#recipe-add-a-new-panel) for the full recipe.

---

## The registered panels

Registered in `OnAttach` (`EditorLayer.cpp:350`–`393`), all under `PanelCategory::View`:

| Display name | Class | Open by default | Docs |
|---|---|:---:|---|
| Scene Hierarchy | `SceneHierarchyPanel` | ✔ | [Panels](Panels.md#scene-hierarchy--inspector) |
| Content Browser | `ContentBrowserPanel` | ✔ | [Content Browser](Content-Browser.md) |
| Beam | `TextEditorPanel` | ✔ | [Panels](Panels.md#beam-text-editor) |
| Log | `EditorConsolePanel` | ✔ | [Panels](Panels.md#log--console) |
| Scene Renderer | `SceneRendererPanel` | ✘ | [Panels](Panels.md#scene-renderer) |
| Renderer Debugger | `RendererDebuggerPanel` | ✘ | [Panels](Panels.md#renderer-debugger) |
| Statistics | `StatisticsPanel` | ✘ | [Panels](Panels.md#statistics) |
| Application Settings | `ApplicationSettingsPanel` | ✘ | [Panels](Panels.md#application-settings) |
| Asset Manager | `AssetManagerPanel` | ✘ | [Panels](Panels.md#asset-manager) |
| Project Settings | `ProjectSettingsWindow` | ✘ | [Panels](Panels.md#project-settings) |
| Light Settings | `LightSettingsPanel` | ✘ | [Panels](Panels.md#light-settings) |

(A few panels — `MaterialsPanel`, `MaterialEditorPanel`, `RenderStatsPanel` — exist in the source
tree and are used contextually rather than registered as always-available View panels.)

---

## Scene states: Edit / Play / Simulate

`EditorLayer::m_SceneState` (`enum SceneState { Edit, Play, Simulate }`) is the editor's core state
machine, driven from the titlebar transport buttons:

- **Edit** — the default. `m_EditorScene` is the live scene; the editor camera flies freely; nothing
  simulates.
- **Play** (`OnScenePlay`) — the editor **copies** `m_EditorScene` into a runtime `m_ActiveScene`,
  starts scripts + physics, and ticks it. Stopping restores the untouched editor scene. This is why
  the two scene refs exist: edits during Play must not survive.
- **Simulate** (`OnSceneSimulate`) — physics runs but scripts don't; used to preview physics without
  game logic.

Transitions live in `OnScenePlay` / `OnSceneSimulate` / `OnSceneStop` / `OnScenePause`
(`EditorLayer.cpp`). Each calls `m_PanelManager->SetSceneContext(...)` so panels retarget to the
scene that is now active.

**Debug-view suspension.** Entering Play snapshots the renderer's debug views into
`m_PlayModeDebugViewState` and suspends them (`SuspendRendererDebugViewsForPlay`), restoring them on
Stop — so wireframe/collider overlays don't bleed into a play session. See
`RestoreRendererDebugViewsAfterPlay`.

---

## The frame: what `OnImGuiRender` draws, in order

`EditorLayer::OnImGuiRender` (`EditorLayer.cpp`, ~line 600+) each frame:

1. **`UI_DrawTitlebar()`** — the custom titlebar: LUX wordmark, project/scene breadcrumb, window
   controls, and the centred **transport** (gizmo tools + play/simulate/stop) via
   `UI_TitlebarTransport`. See [Viewport & Camera](Viewport-and-Camera.md) and
   [Keybindings](Keybindings.md#menus).
2. **`UI_DrawMenubar()`** — File / Edit / View / Tools / Help.
3. The **dockspace** — the full-window ImGui dockspace that panels dock into.
4. **`m_PanelManager->OnImGuiRender()`** — every open panel (`EditorLayer.cpp:651`).
5. The **viewport window** + its overlays (`UI_ViewportSettings`, `UI_ViewportOrientationGizmo`,
   `UI_ViewportSelectionBadge`, `UI_ViewportPerformanceHUD`) — but only when the viewport is the
   visible tab (`m_EditorViewport->IsVisible()`).

---

## Layout modes: Simple vs Advanced

`m_SimpleLayout` (persisted as `Editor.SimpleLayout`) toggles between two dock arrangements:

- **Simple** — a minimal default: viewport, hierarchy, content browser, log. For everyday scene
  work.
- **Advanced** — a fuller workspace with the diagnostic panels (Scene Renderer, Renderer Debugger,
  Statistics) docked in.

`SetEditorLayoutMode(bool simple)` requests a switch; because the dock rebuild needs the live
dockspace id (only valid inside `OnImGuiRender`), the switch sets `m_PendingLayoutReset` and the
rebuild happens next frame in `ResetDefaultDockLayout(dockspaceId)`. **View → Reset Layout** forces a
rebuild of the current mode's arrangement.

### How to modify the default layout

Edit `ResetDefaultDockLayout` in `EditorLayer.cpp` — it splits the dockspace with
`ImGui::DockBuilderSplitNode` and assigns panel windows to nodes by their display name. To change
where a panel lands by default, change its `DockBuilderDockWindow("<Display Name>", nodeId)` call.
The display name must match the panel's registered `Name`.

---

## Persistence

The editor persists state across sessions in a few places:

| What | Where | Written by |
|---|---|---|
| Which panels are open | ImGui ini / panel serialize | `PanelManager::Serialize` / `Deserialize` |
| Dock layout | `Editor/imgui.ini` (ImGui's own) | ImGui automatically |
| Editor prefs (VSync, frame rate, layout mode, gizmo snap…) | Editor preferences file | `SaveEditorPreferences` / `LoadEditorPreferences` |
| Recent projects, startup project | `UserPreferences` | `SaveUserPreferences` / `LoadUserPreferences` |
| Per-panel settings (e.g. Content Browser view mode) | `Application::GetSettings()` (key/value) | each panel's own `SaveSettings` |

`Application::Get().GetSettings()` is a global key/value store (`GetFloat/GetInt/Get/Set…` +
`Serialize`) that panels use for their own persistent options — e.g. the Content Browser stores
`ContentBrowser.ViewMode`, `ContentBrowser.Favorites`, etc. See [Content Browser](Content-Browser.md).
</content>
