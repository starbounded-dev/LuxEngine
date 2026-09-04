# Content Browser

The asset browser — where you navigate project files, create assets, and drag them into the scene or
inspector. It was rebuilt to add a grid/list toggle, type-filter chips, sorting, favourite folders,
and a selected-asset details footer. Source:
`Editor/Source/Panels/ContentBrowserPanel.{h,cpp}` and
`Editor/Source/Panels/ContentBrowser/ContentBrowserItem.{h,cpp}`.

---

## Anatomy

The panel is laid out top-to-bottom:

```
┌───────────────────────────────────────────────── top bar (58px) ──┐
│  breadcrumb (Assets / Meshes / …)          [＋ New] [view] [sort] [⚙] │
│  [All] [Meshes] [Materials] [Textures] [Scripts] [Scenes] [Prefabs]  │  ← filter chips
├──────────────┬────────────────────────────────────────────────────┤
│  ★ FAVORITES │                                                     │
│  folder tree │   items (grid of cards, or list of rows)            │
│              │                                                     │
├──────────────┴────────────────────────────────────────────────────┤
│  name · type · size · path         (single selection)   [size ──○] │  ← bottom bar (30px)
└────────────────────────────────────────────────────────────────────┘
```

- **Top bar** — flat mono breadcrumb (dim path, accent current folder, muted `/` separators) on the
  left; a right-aligned control group: a lime **＋ New**, a **view toggle** (grid ⇄ list), a **sort**
  button, and an **options** (⚙) button.
- **Filter chips** — one row of asset-type chips; the active chip gets an accent background.
- **Left column** — a **FAVORITES** section (pinned folders) above the folder tree.
- **Item area** — asset cards (grid) or compact rows (list).
- **Bottom bar** — details for the current selection (name / type / size / path), or a count
  (`N selected` / `N items`); a thumbnail-size slider on the right in grid view.

---

## How to use it

| Action | How |
|---|---|
| Navigate into a folder | double-click it (or click it in the tree) |
| Go back / forward | the breadcrumb, or `OnBrowseBack` / `OnBrowseForward` |
| Switch grid ⇄ list | the view-toggle button (icon `LUX_ICON_LIST` ⇄ `LUX_ICON_TH_LARGE`) |
| Sort | the sort button → Name / Type / Modified, Ascending / Descending |
| Filter by type | click a filter chip (All, Meshes, Materials, Textures, Scripts, Scenes, Prefabs) |
| Pin a favourite | right-click a folder → **Add to Favorites** (a lime ★ appears in the FAVORITES list) |
| Create an asset | **＋ New** → Folder / Scene / Material / Import |
| Rename | select + F2 |
| Delete | select + Delete (confirmation dialog) |
| Select multiple | Ctrl-click (toggle) / Shift-click (range) |
| Drag into scene/inspector | drag an item — it starts an ImGui drag-drop source carrying the `AssetHandle` |
| Resize thumbnails | the slider on the bottom-right (grid view only) |

---

## How it is built

### Two objects

- **`ContentBrowserPanel`** — owns the current directory, the item list, selection, favourites, and
  the view/sort/filter state. Renders the chrome (top bar, favourites, tree, bottom bar).
- **`ContentBrowserItem`** — one entry (a folder or an asset). Renders itself as a card (grid) or a
  row (list) and handles its own interaction (select, rename, drag-drop source, context menu).

### State (persisted)

From `ContentBrowserPanel.h`:

```cpp
enum class ViewMode { Grid, List };
enum class SortMode { Name, Type, Modified };

ViewMode m_ViewMode = ViewMode::Grid;
SortMode m_SortMode = SortMode::Name;
bool     m_SortAscending = true;
int      m_TypeFilter = 0;                 // 0 = All; index into s_FilterLabels
std::vector<std::string> m_Favorites;      // favourite folders, generic paths relative to Assets/
float    m_ThumbnailSize = 128.0f;
```

All of these persist through `LoadSettings` / `SaveSettings`, which read/write
`Application::Get().GetSettings()` under `ContentBrowser.*` keys (favourites are joined with `|`).

### The render methods

| Method | Draws |
|---|---|
| `RenderTopBar(height)` | breadcrumb + right controls + the New/Sort/Settings popups + the filter-chip row |
| `RenderFavorites()` | the FAVORITES header and one selectable per pinned folder |
| `RenderDirectoryHierarchy(dir)` | the folder tree (with the per-folder favourite context menu) |
| `RenderItems()` | the grid/list of items (skips filtered-out assets) |
| `RenderBottomBar(height)` | the details footer + thumbnail slider |

`OnImGuiRender` sizes the bars (`topBarHeight = 58`, `bottomBarHeight = 30`), sets
`columnCount = IsListView() ? 1 : <computed from thumbnail size>`, and calls the render methods in
order.

### Filtering

`AssetMatchesFilter(handle)` maps the active filter index to an `AssetType` (or set of types) and
`RenderItems` skips non-matching assets:

```cpp
if (m_TypeFilter != 0
    && item->GetType() == ContentBrowserItem::ItemType::Asset
    && !AssetMatchesFilter(item->GetID()))
    continue;
```

The chip labels are a file-scope array **defined above `OnImGuiRender`** (it must precede first use,
or you get an "undeclared identifier" error):

```cpp
static const char* s_FilterLabels[] =
    { "All", "Meshes", "Materials", "Textures", "Scripts", "Scenes", "Prefabs" };
```

### Sorting

`SortItemList` always puts **directories first**, then sorts by `m_SortMode`:

- **Name** — lexicographic.
- **Type** — by `AssetTypeToString`.
- **Modified** — by `std::filesystem::last_write_time` on the resolved asset path.

`m_SortAscending` flips the comparison.

### Favourites

- `IsFavorite(genericPath)` / `ToggleFavorite(genericPath)` search/mutate `m_Favorites` (and call
  `SaveSettings`).
- `RenderFavorites` draws a **FAVORITES** header, then a `Selectable` per entry with a lime ★; clicking
  navigates via `GetDirectory(path)`; right-click removes.
- The folder tree adds these entries through a per-folder context menu
  (`BeginPopupContextItem("##DirectoryContext")`) with **Add / Remove Favorites**
  (`LUX_ICON_STAR` / `LUX_ICON_STAR_O`).

### Grid vs list item geometry

`ContentBrowserItem::OnRender` branches on `context->IsListView()`:

- **List** — a compact full-width row: `rowHeight = GetFrameHeight() + 6`, a small icon, the name via
  `drawList->AddText`, and a right-aligned type label.
- **Grid** — the thumbnail card (icon/thumbnail + name).

Crucially, the shared interaction (F2 rename trigger, drop update, `BeginDragDropSource`, hover
click-select with Ctrl/Shift, context menu) is written **once**, after the branch, against variables
(`itemRect`, `hovered`, `drawList`) hoisted to function scope — so grid and list behave identically.
The trailing `SetCursorScreenPos` in the list branch is not an item, so it doesn't steal the
drag-drop binding from the row's `InvisibleButton`.

---

## How to modify

### Add a filter chip
1. Add the label to `s_FilterLabels[]`.
2. Add its case to `AssetMatchesFilter` mapping the new index to the `AssetType`(s) it should show.

### Add a "New" action
Add a `MenuItem` to the `"ContentBrowserNew"` popup in `RenderTopBar`, and a `CreateXAsset()` helper
alongside `CreateNewFolder` / `CreateSceneAsset` / `CreateMaterialAsset`.

### Add a sort mode
Add a value to `enum class SortMode`, a `MenuItem` in the `"ContentBrowserSort"` popup, and a branch
in `SortItemList`. Persist it in `Load/SaveSettings`.

### Change the details footer
Edit `RenderBottomBar` — the single-selection branch formats name/type/size (an inline `formatSize`
lambda) / path in the **Mono** font; the multi-selection branch shows the count.

### Register an "activate" handler for a type
From `EditorLayer`, call
`contentBrowserPanel->RegisterItemActivateCallbackForType(AssetType::Scene, [](const AssetMetadata&){…})`.
This is how double-clicking a `.luxscene` opens it and a `.cs` opens Beam (`EditorLayer.cpp:443`+).

### Icons and thumbnails
Type icons come from `EditorResources` textures (`FolderIcon`, `FileIcon`, `FBXFileIcon`, …, see
[Theme, Fonts & Icons](Theme-Fonts-Icons.md#editorresources-textures)); live thumbnails come from the
`ThumbnailCache` (`Editor/Source/Panels/ThumbnailCache.{h,cpp}`), fetched via `GetItemThumbnail`.
