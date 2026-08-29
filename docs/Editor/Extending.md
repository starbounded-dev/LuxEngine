# Extending the Editor

Copy-pasteable recipes for the common editor changes: adding a panel, a font, an icon, a shortcut,
retheming, and adding a menu entry. Each recipe lists the exact files to touch and the gotchas that
bite.

> **Before you start:** a source-file addition on Windows requires regenerating the project
> (`scripts\Win-GenProjects.bat`) — Premake does not regenerate itself, and a missing file shows up
> as `LNK2001 unresolved external symbol`. On Linux, re-run `./premake5 gmake2`. See
> `.claude/docs/Building.md`.

---

## Recipe: add a new panel

**Goal:** a new dockable panel listed in the **View** menu.

1. **Create the class** (e.g. `Editor/Source/Panels/MyPanel.{h,cpp}`), deriving from `EditorPanel`:

   ```cpp
   #include "Lux/Editor/EditorPanel.h"

   namespace Lux {
       class MyPanel : public EditorPanel {
       public:
           void OnImGuiRender(bool& isOpen) override {
               if (!ImGui::Begin("My Panel", &isOpen)) { ImGui::End(); return; }
               // ... draw ...
               ImGui::End();
           }
           // optional:
           void OnEvent(Event& e) override {}
           void SetSceneContext(const Ref<Scene>& scene) override { m_Scene = scene; }
           void OnProjectChanged(const Ref<Project>& project) override {}
       private:
           Ref<Scene> m_Scene;
       };
   }
   ```

   Pass the `bool& isOpen` to `ImGui::Begin` so the window's `×` closes the panel and the state
   persists.

2. **Register it** in `EditorLayer::OnAttach` (`Editor/Source/EditorLayer.cpp` around line 350):

   ```cpp
   m_PanelManager->AddPanel<MyPanel>(PanelCategory::View, "MyPanel", "My Panel", /*openByDefault*/ false);
   ```

   The 2nd arg is the stable id (hashed), the 3rd is the display name (menu + dock window title).
   Keep a `Ref<MyPanel>` return handle if `EditorLayer` needs to talk to it.

3. **Include** `Panels/MyPanel.h` at the top of `EditorLayer.cpp`.

4. **Regenerate the project** (new `.cpp`), then build.

That's it — the panel now appears under **View**, docks, persists open/closed, and receives scene /
project / event callbacks. To dock it somewhere specific by default, add a
`DockBuilderDockWindow("My Panel", node)` line in `ResetDefaultDockLayout`.

---

## Recipe: add a font

1. Drop the `.ttf` under `Editor/Resources/Fonts/<Family>/`.
2. In **`Core/Source/Lux/ImGui/ImGuiLayer.cpp`**, in the font block in `OnAttach` (~line 108–180),
   **append** an `Add` at the end (don't reorder existing entries — `Fonts[0]`/`Fonts[1]` indices are
   load-bearing):

   ```cpp
   ImGuiEx::FontConfiguration myFace;
   myFace.FontName = "MyFace";
   myFace.FilePath = "Resources/Fonts/MyFamily/MyFace.ttf";
   myFace.Size = 15.0f;
   ImGuiEx::Fonts::Add(myFace);
   ```

3. **Mirror the same block** into `VulkanImGuiLayer.cpp` (kept in lockstep, currently dormant).
4. Use it: `ImGuiEx::Fonts::PushFont("MyFace"); … ImGuiEx::Fonts::PopFont();`

**Merging an icon face** into another (like FontAwesome into `Default`): set `MergeWithLast = true`
and a `GlyphRanges` array, and `Add` it immediately after the face it merges into.

---

## Recipe: add a texture icon

For a full-colour PNG icon (asset/entity/window art):

1. Put the PNG under `Editor/Resources/Editor/<Category>/MyIcon.png` (paths resolve under
   `Resources/Editor/`).
2. In `Core/Source/Lux/Editor/EditorResources.h`, declare the field:

   ```cpp
   inline static Ref<Texture2D> MyIcon = nullptr;
   ```

3. Load it in `EditorResources::Init()`:

   ```cpp
   MyIcon = LoadTexture("Category/MyIcon.png", "MyIcon", spec);
   ```

4. Reset it in `EditorResources::Shutdown()` (`MyIcon.Reset();`).
5. Draw it: `ImGui::Image((ImTextureID)EditorResources::MyIcon->GetRendererID(), size);` (or the
   editor's image-button helper).

For simple monochrome chrome (buttons, chips), prefer a **FontAwesome glyph** instead — no texture,
scales with the font. See [Theme, Fonts & Icons](Theme-Fonts-Icons.md#icons-kind-1-fontawesome-glyphs).

---

## Recipe: add a shortcut

**Global** (works anywhere in the editor): add a `case` to the `switch` in
`EditorLayer::OnKeyPressed` (`EditorLayer.cpp:1649`):

```cpp
case Key::G:
    if (control) DoTheThing();
    break;
```

`control` / `shift` are already computed at the top of the function.

**Panel-local:** handle it in the panel. The ImGui idiom (used by Beam) is, inside the panel's
render while it's focused:

```cpp
ImGuiIO& io = ImGui::GetIO();
if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, /*repeat*/ false))
    DoTheThing();
```

Add the shortcut to [Keybindings](Keybindings.md) when you add it.

---

## Recipe: retheme (change the accent or a surface)

1. Edit the token in `Core/Source/Lux/ImGui/Colors.h` — e.g. change `accent`.
2. If you changed the accent, update its derivations in the same file so they stay in step:
   `selection`, `selectionMuted`, `accentTabHovered`, `accentTabActive`, `accentSeparatorHovered`
   (they are the accent at different alphas).
3. If you need the colour on a different ImGui element, edit the mapping in
   `ImGuiLayer::SetDarkThemeV2Colors()` (`ImGuiLayer.cpp:438`).

No rebuild of assets is needed — colours are `constexpr` compiled into the editor.

---

## Recipe: add a menu entry

In `EditorLayer::UI_DrawMenubar` (`EditorLayer.cpp:891`), inside the relevant `BeginMenu` block:

```cpp
if (ImGui::BeginMenu("Tools")) {
    if (ImGui::MenuItem("My Tool"))          DoMyTool();
    ImGui::MenuItem("My Toggle", nullptr, &m_MyToggle);   // checkbox item
    ImGui::EndMenu();
}
```

For a shortcut hint in the menu, pass it as the 2nd arg (`ImGui::MenuItem("Save Scene", "Ctrl+S")`) —
this is display-only; the actual binding lives in `OnKeyPressed`.

---

## Recipe: register a Content Browser "activate" handler

To make double-clicking an asset type do something (like `.cs` → open Beam):

```cpp
contentBrowserPanel->RegisterItemActivateCallbackForType(
    AssetType::YourType,
    [this](const AssetMetadata& metadata) { /* handle activation */ });
```

Do this in `OnAttach` after the panel is created (`EditorLayer.cpp:443`+). See
[Content Browser](Content-Browser.md#register-an-activate-handler-for-a-type).

---

## Recipe: add a component to the inspector

This is the one with the most hidden steps — a component that "works" in the inspector but wasn't
wired everywhere silently breaks copy/prefab/serialize. Touch **all** of:

1. `Core/Source/Lux/Scene/Components.h` — define the component.
2. `SceneSerializer` — serialize **and** deserialize it.
3. Copy / duplicate / prefab paths — so it survives entity duplication and prefab instantiation.
4. `SceneHierarchyPanel` inspector — a `DrawComponent<YourComponent>(…)` block for editing.
5. `FrameRenderPacket` — only if the renderer needs to see it.

This is the "adds/changes a component" row of the silent-failure checklist in
`.claude/skills/cr/SKILL.md § 5`. Run `/cr` before committing component changes.

---

## Where the persisted state lives (so you don't lose settings)

If your extension has a setting to persist, use the right store:

| Kind of setting | Store | Example |
|---|---|---|
| Per-panel option | `Application::Get().GetSettings()` (`Set/Get*` + `Serialize`) | Content Browser view mode |
| Editor-wide pref | `SaveEditorPreferences` / `LoadEditorPreferences` | VSync, layout mode |
| User/global | `UserPreferences` | recent projects |
| Per-project | `Project` settings → `.luxproj` | startup scene |
| Per-scene | scene serialization → `.luxscene` | post-processing/grading |
| Dock layout / open panels | `Editor/imgui.ini` + `PanelManager::Serialize` | automatic |

See [Architecture → Persistence](Architecture.md#persistence).
</content>
