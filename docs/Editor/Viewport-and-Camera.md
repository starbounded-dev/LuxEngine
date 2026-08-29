# Viewport & Camera

The 3D scene view: how it renders, how the editor camera flies, how gizmos and mouse-picking work,
and the overlays drawn on top. Source: `Editor/Source/Viewport/Viewport.h`,
`Core/Source/Lux/Editor/EditorCamera.{h,cpp}`, and the `UI_Viewport*` methods in
`Editor/Source/EditorLayer.cpp`.

---

## The `Viewport` object

`Viewport` (`Editor/Source/Viewport/Viewport.h`) bundles everything needed to show one scene view:

```cpp
class Viewport : public RefCounted {
    Framebuffer  m_Framebuffer;   // the render target
    SceneRenderer m_Renderer;     // draws the scene into the framebuffer
    EditorCamera m_Camera;        // the free-fly camera
    // + size, bounds, image bounds, focus/hover/visible flags, display mode
};
```

`Init(scene, framebufferSpec, rendererSpec)` creates the framebuffer, the scene renderer, and a
60°-FOV camera, and points the scene at the framebuffer. `EditorLayer` owns the single editor
viewport (`m_EditorViewport`); a second viewport can be enabled via **Edit → Second Viewport**
(`m_SecondViewportEnabled`).

### Render size vs panel size — the letterboxing rule

This is the subtle part. The **panel size** is the *output* size; the **render size** is what the
scene is actually rendered at, which can differ when the renderer is scaling (render-scale modes) or
pinned to an absolute resolution.

- `SyncSceneViewport` pushes the panel size to the renderer, reads back the real render size
  (`GetRenderSize()`), and resizes the framebuffer + camera bounds to *that*.
- `UpdateImageBounds` fits the render aspect inside the panel and **centres it**, letterboxing when
  the aspects differ (bars left/right or top/bottom) instead of stretching.
- **Anything mapping screen position → scene position (mouse picking, ImGuizmo) must use
  `GetImageBounds()`, not `GetBounds()`** — otherwise it is offset by the letterbox bars. `GetBounds`
  is the whole panel rect (use it for overlays anchored to the panel); `GetImageBounds` is the
  rendered-image rect inside it. This is called out in the header comments (`Viewport.h:146`–`159`).

### The visibility flag

`BeginImGui()` sets `m_Visible` from whether the viewport's ImGui window is the active, non-collapsed
tab. `IsVisible()` is **true only when the viewport is the visible tab** — false when another panel
(e.g. Beam) is docked on top of it. The editor gates all viewport overlays on this so they don't draw
over an unrelated tab (`Viewport.h:157`).

`IsFocused()` / `IsHovered()` are the usual ImGui focus/hover of the viewport window — picking and
camera input gate on hover.

---

## The Editor Camera

`EditorCamera` (`Core/Source/Lux/Editor/EditorCamera.{h,cpp}`) is a free-fly + orbit camera. It has
two interaction modes, selected by whether **Left Alt** is held.

### Controls

**Fly mode (default — Right mouse button held):**

| Input | Action |
|---|---|
| Hold **RMB** | Enter fly mode (cursor hidden/locked) |
| **W / S** | Forward / back |
| **A / D** | Left / right |
| **Q / E** | Down / up |
| **Mouse move** (RMB held) | Look around |
| **Scroll** (RMB held) | Change fly speed (`m_NormalSpeed`, clamped `MIN_SPEED..MAX_SPEED`) |
| Hold **Left Shift** | Move faster |
| Hold **Left Control** | Move slower |

(`EditorCamera.cpp:100`–`110` for WASDQE; `:169`–`173` for the shift/ctrl speed scaling; `:275` for
scroll-to-speed.)

**Orbit / pan / dolly mode (Left Alt held):**

| Input | Action |
|---|---|
| **Alt + LMB** drag | Orbit (rotate around focal point) |
| **Alt + MMB** drag | Pan |
| **Alt + RMB** drag | Dolly / zoom |
| **Scroll** (no RMB) | Zoom |

(`EditorCamera.cpp:130`–`146`.)

### Cursor handling — the bug that was fixed

Fly/orbit modes hide-and-lock the cursor (`DisableMouse` → `CursorMode::Locked`/`Hidden`); releasing
returns it to `CursorMode::Normal` (`EnableMouse`). The subtle part: when the camera becomes inactive
it must re-enable the OS cursor, not just re-enable ImGui input:

```cpp
if (Input::GetCursorMode() != CursorMode::Normal)
    EnableMouse();
```

(`EditorCamera.cpp:84`.) Without this, releasing the button while the camera deactivated left the
cursor invisible until the window lost focus. Don't remove that guard.

### How to modify the camera

- **Default speed / limits:** `m_NormalSpeed`, `MIN_SPEED`, `MAX_SPEED` in `EditorCamera.cpp`.
- **Rebind fly keys:** the `Input::IsKeyDown(KeyCode::…)` checks around `:100`–`110`.
- **FOV / near / far:** set at construction — `EditorCamera(60.0f, w, h, 0.1f, 10000.0f)` in
  `Viewport::Init`.

---

## Gizmos (ImGuizmo)

The transform gizmo is **ImGuizmo**, driven by `EditorLayer::m_GizmoType`:

| Key | `m_GizmoType` | Op |
|---|---|---|
| `Q` | `-1` | None (deselect gizmo) |
| `W` | `ImGuizmo::TRANSLATE` | Move |
| `E` | `ImGuizmo::ROTATE` | Rotate |
| `R` | `ImGuizmo::SCALE` | Scale |

(`EditorLayer.cpp:1662`–`1673`. The keys are ignored while a gizmo is actively being dragged —
`if (!ImGuizmo::IsUsing())`.)

**Snapping:** `m_UseGizmoSnap` with `m_TranslationSnapValue` (0.5) and `m_RotationSnapValue` (45°);
hold **Ctrl** while dragging to snap (`EditorLayer.cpp:773`). The gizmo operates on the selected
entity's transform and is rendered inside the viewport image rect.

---

## Mouse picking

Clicking in the viewport (LMB, hovered, not over a gizmo, Alt not held) selects the entity under the
cursor:

1. `OnMouseButtonPressed` → `CastMousePick()` (`EditorLayer.cpp:1682`, `:1696`).
2. `CastMousePick` builds a ray from the cursor. **NDC is derived from the rendered image rect**
   (`GetImageBounds`), not the panel — because a letterboxed render offsets panel-relative
   coordinates (`EditorLayer.cpp:1701`+).
3. The ray is tested against entities (`RayIntersectsEntity`), and the nearest hit is selected and
   pushed to the Scene Hierarchy (`m_SceneHierarchyPanel->SetSelectedEntity`).

---

## Viewport overlays

Drawn on top of the viewport image, **only when `m_EditorViewport->IsVisible()`** (so they vanish
when another tab covers the viewport). Each is a `UI_Viewport*` method in `EditorLayer.cpp`:

| Overlay | Method | What it shows |
|---|---|---|
| Settings | `UI_ViewportSettings` | Display-mode / grid / snap quick controls |
| Orientation gizmo | `UI_ViewportOrientationGizmo` | The corner axis widget |
| Selection badge | `UI_ViewportSelectionBadge` | The selected entity's name |
| Performance HUD | `UI_ViewportPerformanceHUD` | Mono FPS/frame-time readout (toggle `m_ShowViewportPerformanceHUD`) |

The **transport** (gizmo tool buttons + play/simulate/stop) is **not** a viewport overlay — it lives
in the titlebar (`UI_TitlebarTransport`), centred, so it doesn't cover the scene and stays available
even when Beam is the visible tab. Its rect is excluded from the titlebar drag zone
(`m_TitleBarTransportRect`, via `OnTitleBarHitTest`) so the buttons stay clickable instead of
starting a window move.

### How to modify overlays

- **Hide an overlay:** they're already gated on `IsVisible()`; add your own condition in the
  `UI_Viewport*` method or its call site in `OnImGuiRender`.
- **Move the transport:** edit `UI_TitlebarTransport(float titlebarWidth)` — it centres the tools and
  records `m_TitleBarTransportRectMin/Max`. Keep the rect recording so the drag-exclusion still works.
- **Restyle the HUD:** it uses `ImGuiEx::Fonts::PushFont("Mono")`; edit `UI_ViewportPerformanceHUD`.
</content>
