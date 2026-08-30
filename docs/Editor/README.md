# LuxEngine Editor — Documentation

This is the reference for the **LuxEngine editor** (the `Editor` project): what every part is, how
to use it, how it was built, and how to change it. It is written to double as the source material
for a public "How to use LuxEngine" website, so each page is self-contained and grounded in the
actual code (files are cited as `path:line` so a reader can jump straight to the implementation).

> Scope: the **editor application** — the ImGui shell, panels, viewport, camera, theming, and the
> extension points. The runtime engine systems (renderer internals, ECS, scripting, physics) are
> documented in `.claude/docs/` and the top-level `CLAUDE.md`; this set only touches them where the
> editor drives them.

---

## The map

| Page | What it covers |
|---|---|
| [Architecture](Architecture.md) | How the editor boots, `EditorLayer`, the `PanelManager`, the `EditorPanel` contract, scene states, docking & layout modes |
| [Panels](Panels.md) | Every panel — purpose, how to use it, how it is built, how to modify it |
| [Viewport & Camera](Viewport-and-Camera.md) | The viewport, editor camera controls, gizmos, mouse picking, the on-viewport overlays |
| [Content Browser](Content-Browser.md) | The asset browser in depth — grid/list, filters, sort, favourites, details footer |
| [Theme, Fonts & Icons](Theme-Fonts-Icons.md) | `Colors::Theme`, the font stack, FontAwesome glyphs, `EditorResources` textures, the `ImGuiEx` helpers |
| [Keybindings & Menus](Keybindings.md) | Every shortcut and menu entry, and where they are wired |
| [Undo / Redo](Undo-Redo.md) | The snapshot-based undo system, its coverage, and the phased plan to complete it |
| [Extending the Editor](Extending.md) | Recipes: add a panel, add a font, add an icon, change the accent, add a shortcut |

---

## What the editor is, in one paragraph

The editor is a single native window built on **Dear ImGui** (docking branch) rendered through the
engine's **Vulkan** backend. Its one `Layer` — `EditorLayer` — owns the scene(s), the viewport, the
scene renderer, and a `PanelManager` that holds every dockable panel. Each panel is a small class
deriving from `EditorPanel`; the manager renders them, routes events to them, and persists which
ones are open. The look — a warm-graphite base with a single acid-lime accent, called **"Monolith,
warmed"** — comes from one theme table (`Colors::Theme`) and a small font stack (Archivo for UI,
JetBrains Mono for numbers, Bricolage Grotesque for wordmarks, FontAwesome for icons).

## How the editor starts

1. **`LuxEditorApp.cpp`** defines `LuxEditor : Application` and, in `CreateApplication`, builds the
   `ApplicationSpecification` (window 1600×900, start maximized, VSync on) and reads the threading
   policy from `App.lsettings` before pushing a single `EditorLayer`.
   (`Editor/Source/LuxEditorApp.cpp`)
2. The engine's `Application` creates the window, the Vulkan device, and the `ImGuiLayer`, which
   loads the fonts and applies the dark theme. (`Core/Source/Lux/ImGui/ImGuiLayer.cpp`)
3. **`EditorLayer::OnAttach`** loads editor resources, creates the viewport + scene renderer,
   registers every panel with the `PanelManager`, restores the last project/scene and the saved
   panel-open state, and sets up the dock layout. (`Editor/Source/EditorLayer.cpp:348`+)
4. Each frame, `EditorLayer::OnImGuiRender` draws the titlebar, the menubar, the dockspace, and then
   calls `m_PanelManager->OnImGuiRender()` to draw every open panel.

The next page, [Architecture](Architecture.md), walks each of these in detail.

---

## Conventions used across these docs

- **Accent** always means the signature lime `#C8FF4D` (`Colors::Theme::accent`).
- Code references look like `Editor/Source/EditorLayer.cpp:651` and are exact at the time of writing;
  if a line has drifted, search for the symbol named next to it.
- "**How it was made**" sub-sections explain the design decision behind a piece so you know what is
  load-bearing before you change it.
- "**How to modify**" sub-sections are copy-pasteable recipes.
</content>
</invoke>
