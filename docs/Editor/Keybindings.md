# Keybindings & Menus

Every keyboard shortcut and menu entry in the editor, and where each is wired in code.

---

## Global shortcuts

Handled in `EditorLayer::OnKeyPressed` (`Editor/Source/EditorLayer.cpp:1644`). `control` /`shift` are
either side's modifier.

| Shortcut | Action | Notes |
|---|---|---|
| `Ctrl+N` | New scene | |
| `Ctrl+O` | Open project | |
| `Ctrl+S` | Save scene | |
| `Ctrl+Shift+S` | Save scene as… | |
| `Ctrl+D` | Duplicate selected entity | `OnDuplicateEntity` |
| `Ctrl+Z` | Undo (Edit mode) | skipped while a text field is focused; see [Undo/Redo](Undo-Redo.md) |
| `Ctrl+Shift+Z` / `Ctrl+Y` | Redo (Edit mode) | same text-field guard |
| `Ctrl+R` | Reload C# script assembly | also re-synchronises script storage |
| `Ctrl+Shift+R` | Reload all shaders | `Renderer::ReloadShaders(true)` |
| `Q` | Gizmo: none | ignored while a gizmo is being dragged (`ImGuizmo::IsUsing()`) |
| `W` | Gizmo: translate | |
| `E` | Gizmo: rotate | |
| `R` | Gizmo: scale | (only when not reloading — `R` alone) |

**Gizmo snapping:** hold `Ctrl` while dragging a gizmo to snap to `m_TranslationSnapValue` (0.5) /
`m_RotationSnapValue` (45°) — gated by `m_UseGizmoSnap` (`EditorLayer.cpp:773`).

---

## Editor camera (viewport hovered)

Handled in `EditorCamera::OnUpdate` (`Core/Source/Lux/Editor/EditorCamera.cpp`). See
[Viewport & Camera → Controls](Viewport-and-Camera.md#controls) for the full table. Summary:

| Input | Action |
|---|---|
| Hold `RMB` + `W`/`A`/`S`/`D`/`Q`/`E` | Fly (forward/left/back/right/down/up) |
| `RMB` + mouse | Look |
| `RMB` + scroll | Change fly speed |
| `Shift` / `Ctrl` (while flying) | Faster / slower |
| `Alt` + `LMB` / `MMB` / `RMB` drag | Orbit / pan / dolly |
| Scroll | Zoom |
| `LMB` click (no Alt, not over gizmo) | Select entity under cursor (mouse picking) |

---

## Beam (text editor, when focused)

Handled in `TextEditorPanel::HandleShortcuts` (`Editor/Source/Panels/TextEditorPanel.cpp:226`).

| Shortcut | Action |
|---|---|
| `Ctrl+N` | New file |
| `Ctrl+S` | Save current tab |
| `Ctrl+W` | Close current tab |
| `Ctrl+F` | Find / Replace |
| `Ctrl+G` | Go to line |
| `Ctrl+PageDown` / `Ctrl+PageUp` | Next / previous tab |

(`Ctrl+Tab` is intentionally avoided — reserved by ImGui for window navigation.)

---

## Menus

Drawn in `EditorLayer::UI_DrawMenubar` (`EditorLayer.cpp:891`+).

### File
- **Create Project**
- **Open Project…** `Ctrl+O`
- **Save Project**
- **Export Runtime…** — opens the runtime-export window (build a standalone player)
- **Recent Projects** ▸ — the recent list (disabled entries for missing paths)
- **Save Scene** `Ctrl+S`
- **Save Scene As…** `Ctrl+Shift+S`
- **Exit**

### Edit
- **Undo** `Ctrl+Z` / **Redo** `Ctrl+Shift+Z` — Edit-mode only; greyed out when the stack is empty. See [Undo/Redo](Undo-Redo.md).
- **Reload C# Assembly** `Ctrl+R`
- **Reload All Shaders** `Ctrl+Shift+R`
- **Second Viewport** (toggle `m_SecondViewportEnabled`)

### View
- **Reset Layout** — rebuilds the current layout mode's dock arrangement
- **(one toggle per registered panel)** — each `PanelData.Name` with its `IsOpen` checkbox; this is
  how you show/hide any panel (`EditorLayer.cpp:978`).

### Tools
- **ImGui Metrics** (toggle)
- **ImGui Style Editor** (toggle)

### Help
- **About**

### Titlebar (context)
- **Switch to Simple Mode** / advanced — toggles `m_SimpleLayout` (`EditorLayer.cpp:1497`).

---

## The titlebar transport

Not a menu — the centred cluster in the custom titlebar (`UI_TitlebarTransport`,
`EditorLayer.cpp`): the gizmo tool buttons and the **play / simulate / stop** transport. It drives the
[scene state machine](Architecture.md#scene-states-edit--play--simulate). Its rect is excluded from
the window-drag zone so the buttons stay clickable.

---

## Adding a shortcut

See [Extending → Add a shortcut](Extending.md#recipe-add-a-shortcut). For a global one, add a `case`
to the `switch` in `EditorLayer::OnKeyPressed`; for a panel-local one, handle it in that panel's
`OnEvent` or (ImGui-style) an `ImGui::IsKeyPressed` check in its render, like Beam does.
</content>
