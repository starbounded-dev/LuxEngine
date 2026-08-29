# Theme, Fonts & Icons

The visual language of the editor: the colour theme, the font stack, the two kinds of icons
(FontAwesome glyphs and PNG textures), and the `ImGuiEx` helpers panels use to draw with them.

---

## The theme — "Monolith, warmed"

All editor colours come from one table: `Colors::Theme` in `Core/Source/Lux/ImGui/Colors.h`. They
are `constexpr` `IM_COL32` values, so the palette is a single edit away.

The design: **a warm-graphite dark base with a single luminous lime accent**. The accent is used
sparingly (selection, active separators, focus, checkmarks) so the UI reads calm. It is deliberately
distinct from Hazel's orange/cyan and from this engine's earlier cool-graphite/indigo theme.

### The palette

| Token | Value | Role |
|---|---|---|
| `accent` | `#C8FF4D` | The signature acid-lime — selection, focus, active states |
| `highlight` | brighter lime | Hover/emphasis relative of the accent |
| `background` | `rgb(34,31,28)` | Default surface (warm graphite) |
| `backgroundDark` | `rgb(24,22,20)` | Recessed surface |
| `titlebar` | `rgb(20,18,16)` | Titlebar |
| `propertyField` | `rgb(16,14,12)` | Input field background |
| `groupHeader` | `rgb(43,39,35)` | Collapsing-header background |
| `backgroundPopup` | `rgb(45,41,36)` | Popups / menus |
| `titlebarGreen` / `Orange` / `Red` | — | Play / Simulate / stopped-state titlebars (semantic) |
| `text` | `rgb(210,204,196)` | Body text (warm white) |
| `textBrighter` | `rgb(236,230,220)` | Emphasised text |
| `textDarker` | `rgb(138,128,116)` | Dim text (timestamps, hints) |
| `textError` | `rgb(232,84,84)` | Error text |
| `muted` | `rgb(86,78,68)` | Disabled / very dim |
| `selection` / `selectionMuted` | accent / accent α38 | Selected row fill / muted selection |
| `accentTabHovered` / `accentTabActive` / `accentSeparatorHovered` | accent at varying α | Non-widget hover/active tints |
| `validPrefab` / `invalidPrefab` / `missingMesh` / `meshNotSet` | — | Asset-status semantic colours |

### How the theme is applied

`ImGuiLayer::SetDarkThemeV2Colors()` (`Core/Source/Lux/ImGui/ImGuiLayer.cpp:438`) maps these tokens
onto ImGui's `ImGuiCol_*` style slots. It's called once in `ImGuiLayer::OnAttach` right after
`ImGui::StyleColorsDark()` (`ImGuiLayer.cpp:184`).

### How to modify the theme

- **Change the accent:** edit `accent` (and optionally `highlight`) in `Colors.h`. Because
  `selection`, `selectionMuted`, and the `accentTab*` tints all derive from the same lime, keep them
  in step (they are literal copies of the accent at different alphas — update them together).
- **Change a surface / text colour:** edit the token in `Colors.h`; every panel that references it
  updates automatically.
- **Remap a colour onto a different ImGui slot:** edit `SetDarkThemeV2Colors()`.
- **Live-tweak:** the header suggests turning the `constexpr`s into static members of a `Theme` class
  and adding a small ImGui window to adjust them at runtime — a known extension point, not yet built.

---

## Fonts

The editor uses a small, deliberate font stack. Fonts are registered **once**, in
`ImGuiLayer::OnAttach` (`ImGuiLayer.cpp:108`–`180`), through the `ImGuiEx::Fonts` API.

### The API

`Core/Source/Lux/ImGui/ImGuiFonts.h`:

```cpp
struct FontConfiguration {
    std::string FontName;        // the name you PushFont() by
    std::string_view FilePath;   // ttf path, relative to the working dir
    float Size = 16.0f;          // logical size (multiplied by the HiDPI scale at bake time)
    const ImWchar* GlyphRanges = nullptr;
    bool MergeWithLast = false;  // merge this face into the previous one (used for icons)
};

class Fonts {
    static void SetScale(float scale);            // HiDPI multiplier — before any Add()
    static void Add(const FontConfiguration&, bool isDefault = false);
    static void PushFont(const std::string& name);
    static void PopFont();
    static ImFont* Get(const std::string& name);
};
```

### The registered fonts

| Name | File | Size | Purpose |
|---|---|---|---|
| `Bold` | Archivo-Bold | 18 | `io.Fonts->Fonts[0]` — bold UI text |
| `Large` | Archivo-Regular | 24 | `Fonts[1]` — large headings |
| `Default` | Archivo-Medium | 15 | The default UI face (`isDefault = true`) |
| `FontAwesome` | fontawesome-webfont | 16 | **Merged into `Default`** (`MergeWithLast`) so icon glyphs render inline in normal text |
| `Medium` | Archivo-Medium | 18 | |
| `Small` | Archivo-Regular | 12 | |
| `ExtraSmall` | Archivo-Regular | 10 | |
| `BoldTitle` | Archivo-SemiBold | 16 | |
| `Mono` | JetBrainsMono-Medium | 13 | **Numeric readouts** — perf HUD, stats, console, ImPlot |
| `Display` | BricolageGrotesque-Bold | 18 | Wordmarks / large headers (LUX, BEAM) |

> **Order is load-bearing.** Several call sites index `io.Fonts->Fonts[0]` (= `Bold`) and `Fonts[1]`
> (= `Large`), and named lookups depend on the keys — so `Add` order must not change. `Mono` and
> `Display` are appended **after** the UI fonts specifically so the existing `Fonts[]` indices stay
> put (`ImGuiLayer.cpp:110`, `:165`).

> **Two registration sites, kept in lockstep.** Fonts are registered in `ImGuiLayer.cpp` (the live
> path) and also in the platform `VulkanImGuiLayer.cpp` (currently dormant but kept identical). If you
> change one, change the other.

### Using a font in a panel

```cpp
ImGuiEx::Fonts::PushFont("Mono");
ImGui::Text("%.2f ms", frameTime);
ImGuiEx::Fonts::PopFont();
```

Always pair `PushFont`/`PopFont` (the frame-end sanity asserts catch an imbalance). `ImGuiEx` also
has convenience wrappers `PushFontBold()` / `PushFontLarge()` (`ImGuiEx.h:67`, `:72`).

### HiDPI

`Fonts::SetScale(contentScale)` is called before any `Add`, so every glyph is baked at its physical
pixel size on HiDPI displays. The atlas is static, so this must happen before the first `Add` and
sizes cannot change afterwards (`ImGuiFonts.h:19`).

### How to add / change a font

See [Extending → Add a font](Extending.md#recipe-add-a-font). In short: drop the `.ttf` under
`Editor/Resources/Fonts/…`, add an `Add(FontConfiguration{…})` block **at the end** of the font block
in *both* `ImGuiLayer.cpp` and `VulkanImGuiLayer.cpp`, and `PushFont("YourName")` where you want it.

---

## Icons, kind 1: FontAwesome glyphs

FontAwesome is merged into the default font, so any string can contain an icon glyph. The glyphs are
`#define`d as `LUX_ICON_*` macros in `Core/Source/Lux/Editor/FontAwesome.h` (711 macros; codepoint
range `LUX_ICON_MIN` = `0xf000` .. `LUX_ICON_MAX` = `0xf307`).

```cpp
ImGui::Button(LUX_ICON_SEARCH "  Find");   // an icon then text, in one label
```

Commonly used across the editor: `LIST`, `TH_LARGE`, `SORT`, `STAR`, `STAR_O`, `PLUS`, `EYE`,
`EYE_SLASH`, `CUBE`, `FILE_O`, `FLOPPY_O`, `SEARCH`, `TIMES`, `COG`, `SUN_O`, `FILM`, `MAGIC`,
`TACHOMETER`, `PAINT_BRUSH`, `SLIDERS`, `VIDEO_CAMERA`, `MUSIC`, `CODE`.

**Use these for UI chrome** (toolbar buttons, chips, breadcrumbs) — they scale with the font, tint
with the text colour, and cost nothing to add. To find a glyph, search `FontAwesome.h` for a name.

---

## Icons, kind 2: `EditorResources` textures

Larger, full-colour icons (asset-type thumbnails, entity-type icons, window controls) are **PNG
textures** loaded into `EditorResources` (`Core/Source/Lux/Editor/EditorResources.h`).

- Every icon is an `inline static Ref<Texture2D>` field on the `EditorResources` class.
- `EditorResources::Init()` loads them all via `LoadTexture(relativePath, name, spec)`, which resolves
  paths under **`Resources/Editor/`** (e.g. `LoadTexture("Icons/Camera.png", …)` →
  `Resources/Editor/Icons/Camera.png`) and fatally errors if a file is missing
  (`EditorResources.h:336`).
- Categories: **Generic** (gear, plus, pencil, search…), **Icons** (entity/component types —
  Camera, DirectionalLight, RigidBody, MeshCollider…), **Viewport** (play/pause/stop/simulate/move/
  rotate/scale), **Window** (minimize/maximize/restore/close), **Content Browser** (folder + per-file
  -type icons: FBX, OBJ, GLTF, WAV, MP3, CS, PNG, Material, Scene, Prefab…), **Node Graph**, and a
  few **Textures** (checkerboard, shadow).

Use these when you need a colourful, recognisable icon that a font glyph can't convey — chiefly asset
and entity thumbnails.

### How to add a texture icon

See [Extending → Add an icon](Extending.md#recipe-add-a-texture-icon). In short: put the PNG under
`Editor/Resources/Editor/<Category>/`, declare the `Ref<Texture2D>` field, load it in `Init()`, and
`.Reset()` it in `Shutdown()`.

---

## The `ImGuiEx` drawing helpers

Panels rarely call raw ImGui for styling — they use `ImGuiEx` (`Core/Source/Lux/ImGui/ImGuiEx.h`,
`ImGuiUtilities.h`, `ImGuiWidgets.h`). The ones you'll use most:

### RAII scoping (never leave the stack unbalanced)

```cpp
ImGuiEx::ScopedColour  c(ImGuiCol_Text, Colors::Theme::accent);   // pushes; pops on scope exit
ImGuiEx::ScopedStyle   s(ImGuiStyleVar_FrameRounding, 4.0f);
ImGuiEx::ScopedColourStack cs(ImGuiCol_HeaderHovered, a, ImGuiCol_HeaderActive, b);
```

These push on construction and pop on destruction, so an early `return` can't leak a colour/var onto
the stack — which the frame-end sanity asserts would otherwise trip on. Prefer them over manual
`PushStyleColor`/`PopStyleColor`.

Utility: `ImGuiEx::ColourWithMultipliedValue(colour, multiplier)` derives a lighter/darker shade of a
theme colour (`ImGuiUtilities.h:215`) — used to build hover/active variants without new hardcoded
colours.

### The property grid

The standard "label on the left, editable value on the right" rows:

```cpp
ImGuiEx::BeginPropertyGrid();
ImGuiEx::Property("Speed", speed, 0.0f, 100.0f);         // float/int/bool/string overloads
ImGuiEx::PropertyDropdown("Mode", count, &selected, getName);
ImGuiEx::PropertyColor("Tint", color);
ImGuiEx::EndPropertyGrid();

if (ImGuiEx::PropertyGridHeader("Section"))  { /* collapsible section */ }
```

`Property` has overloads for `bool`, all int widths, `float`, `std::string`, `char*`, etc.
(`ImGuiEx.h:281`+). Most take a `doPushUndo` flag (default true) so edits register in the undo stack.

> **Watch out (the transform-panel bug):** `Property(const char* label, …)` draws the label as visible
> `ImGui::Text`. Passing a hidden `"##X"` label to it prints `##X` literally and it manages its own
> item width. For a bare drag field with a hidden label, call `ImGui::DragFloat("##X", …)` directly —
> see [Panels → Scene Hierarchy](Panels.md#scene-hierarchy--inspector).

### Search widget

`ImGuiEx::Widgets::SearchWidget<N>(buffer, hint, &focus)` (`ImGuiWidgets.h:83`) — the themed search box
used by the Content Browser and available for any panel that needs a filter.
</content>
