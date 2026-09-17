# LuxEngine — Code Conventions

Project-wide style and helper-reuse rules. Read before writing or reviewing C++ in this repo.

This doc complements the rule list in `.claude/skills/send-pr/SKILL.md` (enforced by `/cr` and
`/send-pr`). Those rules are general engineering quality; the rules below are the specific choices
LuxEngine has already made. Where a rule states a ratio ("dominant", "~90%"), that number was
measured against the tree — it is the existing convention, not an aspiration imported from
somewhere else.

---

## Naming and layout

| Thing | Form | Example |
|---|---|---|
| Namespace | `Lux`, plus `Lux::Utils`, `Lux::ImGuiEx`, `Lux::Log` | `namespace Lux {` |
| Types | PascalCase | `SceneRenderer`, `MeshDrawCommand` |
| Functions / methods | PascalCase | `Renderer::BeginFrame()` |
| Member variables | `m_PascalCase` | `m_RenderThread`, `m_CurrentFrameIndex` |
| Static class members | `s_PascalCase` | `s_Instance`, `s_UniformBuffers` |
| File-scope constants | `k_PascalCase` / `kPascalCase` | `kHLSLConstantBufferPrefix` |
| Macros | `LUX_SCREAMING_SNAKE` | `LUX_CORE_ASSERT`, `LUX_PROFILE_FUNCTION` |
| Render-thread-only methods | `RT_` prefix | `RT_GetCurrentFrameIndex`, `RT_BindMaterialDescriptorSet` |

**The `RT_` prefix is load-bearing, not decorative.** A function named `RT_*` may only be called
from the render thread (or from inside a `Renderer::Submit` lambda, which runs there). Adding a
`RT_`-prefixed function means you are asserting that contract; calling one from the main thread is a
bug. See `.claude/docs/Threading.md`.

**Indentation is tabs** (`.editorconfig`: `indent_style = tab`, LF line endings, final newline).
~97% of engine lines are tab-indented — do not introduce space-indented blocks.

**Braces are Allman** (opening brace on its own line) for functions, types, and control flow.

---

## Style

These rules apply to every line you write or edit, even when nearby code differs. Bring the lines
you touch into conformance; do **not** mass-reformat untouched lines in the same change (that is
drive-by churn — see `send-pr` §1). If a whole file is inconsistent, fixing it is its own change.

### Control-flow bodies go on their own line

Measured: ~3600 Allman-style `if (...)` vs ~305 same-line bodies. The former is the convention.

```cpp
// Good
if (!m_Instance)
    return;

// Good
if (!m_Instance)
{
    Cleanup();
    return;
}

// Avoid in new code
if (!m_Instance) return;
```

Same for `else`, `for`, `while`, `do`. Exception: one-line getters/setters declared inline in a
class body (`bool IsRunning() const { return m_IsRunning; }`) — those are member definitions, not
control flow, and are used heavily throughout the engine.

### Namespace qualification

Inside `namespace Lux { ... }`, **omit the `Lux::` prefix** for anything ordinary lookup finds.
Measured: ~55 in-namespace `Lux::` uses across the whole engine — it is the rare exception, not the
norm.

```cpp
// Good — inside namespace Lux
Ref<Texture2D> tex = Renderer::GetWhiteTexture();

// Bad — redundant
Ref<Texture2D> tex = Lux::Renderer::GetWhiteTexture();
```

Exception: disambiguating a nested type against a same-named member or local, which is why
`Scene.h` writes `Ref<::Lux::RenderScene>` (the class has a private `RenderScene(EditorCamera&)`
method that would otherwise shadow the type).

### C standard-library functions — `std::`-qualified

Use `std::memcpy`, `std::memset`, `std::malloc`, `std::strlen`; include `<cstring>` / `<cstdlib>`,
not `<string.h>` / `<stdlib.h>`. Measured: 64 qualified vs 41 bare — qualified is already the
majority, and it is the portability-correct spelling (the `<cxxx>` headers only *guarantee* the
names in namespace `std`; leaking them into the global namespace is unspecified).

### Casts

Prefer `static_cast` / `reinterpret_cast` / `const_cast` / `dynamic_cast` in new code. C-style casts
are common in older engine code (`Ref.h`, `Renderer::Submit`) — treat those as legacy, not
precedent. Do not mass-rewrite them as a drive-by; do write named casts in the lines you touch.

Aggregate/functional construction of a class type (`ImVec2{ x, y }`, `Ref<T>(ptr)`) is construction,
not a cast, and stays as-is.

### File-scope declarations at the top

Anonymous namespaces, file-`static`s, and `constexpr` constants belong just below the includes —
never introduced partway down a `.cpp`. Hoist non-obvious literals into a named `constexpr` rather
than inlining them at the use site.

### Precompiled header

`Core` builds with `lpch.h` (`pchsource Source/lpch.cpp`). New `Core` `.cpp` files must include
`lpch.h` **first**. A handful of files are excluded via `flags { "NoPCH" }` in `Core/premake5.lua`
(vendored FastNoise / yaml-cpp / imgui_stdlib, plus `ApplicationSettings.cpp` and
`DiscordppImpl.cpp`) — if you add a file that cannot use the PCH, add it to that filter rather than
fighting the include order.

---

## Memory and ownership

LuxEngine uses its own smart pointers. Headers: `Core/Source/Lux/Core/Ref.h`,
`Core/Source/Lux/Core/Base.h`, `Core/Source/Lux/Core/Memory.h`.

- **`Ref<T>`** — intrusive, atomically refcounted. `T` must derive from `RefCounted`. Create with
  `Ref<T>::Create(args...)` (or the free `CreateRef<T>(...)`). Cast with `.As<T2>()`; raw access via
  `.Raw()` / `.get()`. Pass by value to share ownership, `const Ref<T>&` to observe.
- **`WeakRef<T>`** — non-owning, with a liveness check (`IsValid()`), backed by the global live-object
  registry in `RefUtils`. Note: `WeakRef` does **not** hand back a `Ref` — there is no `Lock()`. It
  checks liveness and dereferences directly, so it is only safe where the owner cannot die
  concurrently.
- **`Scope<T>`** — alias for `std::unique_ptr<T>`; create with `CreateScope<T>(...)`.

**Why `Ref` deletes off the atomic's own result:** `DecRefCount()` returns the post-decrement value
and `DecRef()` deletes only when *that* returns 0. Do not "simplify" this into a decrement followed
by a `GetRefCount() == 0` check — two threads can both observe 0 and double-free. The comment in
`Ref.h` says so; keep it.

**Forbidden in engine code:** raw `new` / `delete`, `std::shared_ptr`, `std::make_shared`. Bare
`std::unique_ptr` appears in a few places (`Application::m_Window`) but `Scope<T>` is the spelling to
use.

`LUX_TRACK_MEMORY` (Debug + Release) routes `Ref<T>::Create` through a tagged allocator
(`new(typeid(T).name()) T(...)`) for the memory panel. Anything allocating outside `Ref`/`Scope`
disappears from those stats.

---

## Logging, asserts, profiling

### Logging — always tagged

`Core/Source/Lux/Core/Log.h`. Two logger families plus an editor-console logger.

```cpp
LUX_CORE_TRACE_TAG("Renderer", "GPU Info:");
LUX_CORE_WARN_TAG("AssetManager", "Missing asset {0}", handle);
LUX_CORE_ERROR_TAG("Renderer", "Uniform buffer binding collision at (set={0}, binding={1})", set, binding);
```

- Core (engine): `LUX_CORE_{TRACE,INFO,WARN,ERROR,FATAL}_TAG(tag, ...)`
- Client: `LUX_{TRACE,INFO,WARN,ERROR,FATAL}_TAG(tag, ...)`
- Untagged `LUX_CORE_INFO(...)` variants exist; **prefer the tagged forms** — the editor console
  filters on the tag (`Log::EnabledTags()`), so an untagged line cannot be filtered.

Tag with the subsystem name (`"Renderer"`, `"AssetManager"`, `"ScriptEngine"`, `"Physics"`,
`"Project"`). Reuse an existing tag string rather than inventing a near-duplicate.

Format strings go through `std::format` (spdlog is built with `SPDLOG_USE_STD_FORMAT`), so
`{0}`/`{}` placeholders — never `printf` style.

### Asserts

`Core/Source/Lux/Core/Assert.h`.

| Macro | Compiled in | Use for |
|---|---|---|
| `LUX_CORE_ASSERT(cond, ...)` | Debug only (`LUX_ENABLE_ASSERTS`) | Internal invariants; free in Release/Dist |
| `LUX_CORE_VERIFY(cond, ...)` | **All configs** (`LUX_ENABLE_VERIFY` is unconditional) | Invariants that must hold in shipped builds |
| `LUX_ASSERT` / `LUX_VERIFY` | same, Client logger | Client/editor-side code |

Both break into the debugger (`LUX_DEBUG_BREAK`) rather than throwing. There is no soft
"log-and-continue" assert — if you want that, log an error and handle the case explicitly.

### Profiling

`Core/Source/Lux/Debug/Profiler.h` wraps Tracy. `LUX_ENABLE_PROFILING` is `(!LUX_DIST &&
TRACY_ENABLE)` — off in Dist, and off entirely when projects were generated with `--no-tracy`.

- `LUX_PROFILE_FUNCTION_AUTO` — zone named from the source location. The default for broad coverage.
- `LUX_PROFILE_FUNCTION("Name")` / `LUX_PROFILE_SCOPE("Name")` — explicitly named zone.
- `LUX_PROFILE_FUNCTION_COLOR("Name", 0x8CCBFF)` — named + colored.
- `LUX_PROFILE_MARK_FRAME` — frame boundary. `LUX_PROFILE_THREAD("name")` — name a thread.

All macros compile to nothing when profiling is off, so they are safe to leave in hot paths. ~570
call sites exist; match the surrounding density rather than instrumenting every new function.

---

## Helper reuse — look here before writing a utility

If an existing helper *almost* does what you need, **extend the helper** in its home namespace
rather than writing a one-off at the call site. A near-miss utility is a signal to grow it, not to
route around it.

### Files and directories — `Lux::FileSystem`

`Core/Source/Lux/Utilities/FileSystem.h`. Do not reach for `std::filesystem` directly until you have
checked for a wrapper.

Queries: `Exists`, `IsDirectory`, `IsNewer`, `GetLastWriteTime`, `GetUniqueFileName`,
`GetWorkingDirectory`, `GetPersistentStoragePath`.
Mutations: `CreateDirectory`, `DeleteFile`, `MoveFile`, `CopyFile`, `Move`, `Copy`, `Rename`,
`RenameFilename`, `WriteBytes`, `ReplaceFileAtomically` (atomically replace a destination with a completed
same-volume temporary file; implemented on Windows and Linux, reports failure).
Reads: `ReadBytes`, `TryOpenFile`, `TryOpenFileAndWait`.
Shell/OS: `ShowFileInExplorer`, `OpenDirectoryInExplorer`, `OpenExternally`,
`{Has,Get,Set}EnvironmentVariable`.
Dialogs: `OpenFileDialog`, `OpenFolderDialog`, `SaveFileDialog` (also `Utilities/FileDialogs.h`).

Note the `#undef CreateDirectory / DeleteFile / MoveFile / CopyFile` block at the top of the header —
that is deliberate (Windows headers macro-define those names). Don't "clean it up".

### Strings — `Lux::Utils` / `Lux::Utils::String`

`Core/Source/Lux/Utilities/StringUtils.h`: `ToLowerCopy`, `ToUpperCopy`, `SubStr`, `TrimWhitespace`,
`RemoveWhitespace`, `GetCurrentTimeString`, `GetExtension`, `RemoveExtension`, `SplitAtUpperCase`,
`BytesToString`, `DurationToString`, `ReadFileAndSkipBOM`, `AddSuffixToMakeUnique`,
`CreateUserFriendlyTypeName`, `TemplateToParenthesis`.

`BytesToString` and `DurationToString` in particular are re-implemented ad hoc a lot — use them.

### ImGui — `Lux::ImGuiEx`

`Core/Source/Lux/ImGui/`. RAII scopes live in **`ImGuiUtilities.h`** (not `ImGuiEx.h`, despite the
namespace being shared): `ScopedStyle`, `ScopedColour`, `ScopedFont`, `ScopedID`, `ScopedColourStack`,
`ScopedStyleStack`, `ScopedItemFlags`, `ScopedDisable`.

Use those instead of paired `ImGui::Push*` / `Pop*` — an early `return` between a push and its pop is
a corrupted style stack, and the scopes make that unrepresentable.

Widgets and layout helpers are in `ImGuiEx.h` / `ImGuiWidgets.h` (property rows, message boxes,
collapsing headers, `ShiftCursor`, `HelpMarker`, `Draw::Underline`, …); fonts in `ImGuiFonts.h`.
New reusable widgets go into `ImGuiEx`, not inline in a panel.
`ImGuiEx::PropertyEntityReference` provides a scene entity picker with search, clear, hierarchy
drag/drop, and scene snapshot undo. Pass the current scene and a UUID field; it validates dropped
entities against that scene.

### ImGui correctness — close every scope, give every widget a unique ID

Every ImGui change is checked against the three lists below before it is called done. A missing
`End`/`Pop` corrupts the stacks for everything drawn after it, and a duplicate ID makes two widgets
share hover/active/open state — clicking one toggles the other. Both usually *look* fine until a
specific state is reached, so they are verified by reading, not by glancing at the panel.

**1. Every scope is closed, on every path** (vendored ImGui is 1.92 — contracts from `imgui.h`):

| Open | Close | Rule |
|---|---|---|
| `Begin` / `BeginChild` | `End` / `EndChild` | **Always**, whatever the return value |
| `BeginGroup`, `BeginDisabled`, and every push: `PushID`, `PushStyleColor`, `PushStyleVar`, `PushFont`, `PushItemWidth`, `PushTextWrapPos`, `PushClipRect`, `Indent` | matching `End*` / `Pop*` / `Unindent` | **Always**, with matching counts (`PopStyleColor(n)` / `PopStyleVar(n)`) |
| `BeginMenuBar`, `BeginMainMenuBar`, `BeginMenu`, `BeginPopup*`, `BeginTable`, `BeginTabBar`, `BeginTabItem`, `BeginCombo`, `BeginListBox`, `BeginTooltip` / `BeginItemTooltip`, `BeginDragDropSource` / `Target` | matching `End*` | **Only if it returned true** |
| `TreeNode*`, `ImGuiEx::PropertyGridHeader`, `ImGuiEx::TreeNodeWithIcon` | `ImGui::TreePop()` | Only if it returned true (and not `ImGuiTreeNodeFlags_NoTreePushOnOpen`). `CollapsingHeader` needs no pop |

- Check every early `return`, `continue`, and `break` between an open and its close. Prefer the RAII
  scopes (`ScopedID`, `ScopedStyle`, `ScopedColour`, `ScopedFont`, `ScopedDisable`, …) so an early
  exit cannot leak a push.
- A loop that opens per item closes per item, inside the loop.
- `ImGuiEx::PushID()` / `PopID()` pair the same way as `ImGui::PushID` / `PopID`.

**2. Every ID in a scope is unique.** An ID is the hash of the label combined with the ID stack
(window → `PushID` scopes → tree nodes, tables, tab bars).

- Two widgets with the **same label in the same ID scope are the same widget.** Two `Button("Reset")`
  in one window, two icon buttons using the same `LUX_ICON_*` glyph, or two empty labels (`""`,
  `"##"`) all collide. Give each a suffix: `"Reset##Shadows"`, `LUX_ICON_TRASH "##remove_light"`.
- The text after `##` is hidden but **is** part of the ID; `###` makes **only** the text after it
  the ID. So a toggle that shows `On` / `Off` should use a stable ID —
  `std::format("{}###shadow_toggle", enabled ? "On" : "Off")` — and two *different* toggles must
  not share the part after `###`.
- Anything drawn in a loop is wrapped in `ImGui::PushID` / `ScopedID` keyed by a **stable identity**
  (entity UUID, asset handle, object pointer), not the loop index when the list can reorder or
  filter.
- String IDs that must match each other are checked as a pair: `OpenPopup` / `BeginPopup*`, a
  window title and its `DockBuilderDockWindow` name, the panel name used in `s_AdvancedPanels`.
  Window titles are global IDs — two panels with the same title merge into one window.
- `ImGuiEx::GenerateID()` / `GenerateLabelID()` number widgets by call order since the last
  `ImGuiEx::PushID()` and return a shared buffer. Use the result immediately, and use an explicit
  `##name` for a stateful widget that follows conditionally drawn ones — otherwise its ID shifts when
  the condition changes.

**3. Verify it running.** Open the panel in the editor and drive **every** state the change added —
each branch, toggle value, empty and non-empty list, open popup, Play mode. ImGui 1.92 highlights ID
conflicts and shows an error popup (`io.ConfigDebugHighlightIdConflicts`, on by default); a missing
`End`/`Pop` asserts in Debug. Do not add `ImGuiItemFlags_AllowDuplicateId` or turn those checks off
to quiet a report — fix the ID.

ImGui is main-thread only: never call it inside a `Renderer::Submit` lambda (`Threading.md`).

### Colours — `Colors::Theme`

`Core/Source/Lux/ImGui/Colors.h`. Never hardcode `IM_COL32(...)` / `ImVec4(...)` for theme colours.
Use the named constants (`Theme::accent`, `Theme::background`, `Theme::titlebar`, `Theme::text`,
`Theme::textError`, `Theme::selection`, `Theme::groupHeader`, `Theme::muted`, …). The theme is
`constexpr` so a new colour means adding a constant, not a literal at the call site.

### Assets and project — `AssetManager`, `Project`

`AssetManager::` (`Asset/AssetManager.h`) is the **only** entry point to assets from outside the
asset module. Do not call `EditorAssetManager` / `RuntimeAssetManager` directly from renderer, scene,
script, or editor code — that is what breaks runtime builds.

`Project::GetActive()`, `GetActiveProjectDirectory()`, `GetActiveAssetDirectory()`,
`GetActiveCacheDirectory()`, `GetActiveScriptModuleFilePath()`, `GetAssetManager()`. Don't parse
`.luxproj` yourself when an accessor exists.

### Serialization

`Utilities/SerializationMacros.h` (`LUX_SERIALIZE_PROPERTY`) and the YAML helpers used by
`SceneSerializer` / `ProjectSerializer` / `MaterialSerializer`. Missing keys must deserialize to the
struct default — never hard-fail a load on an absent optional field.

---

## Code organisation

### Platform divergence: separate translation units, not scattered `#ifdef`

Platform-specific implementations live in `Core/Platform/Windows/` and `Core/Platform/Linux/`
(`WindowsFileSystem.cpp` / `LinuxFileSystem.cpp`, `WindowsThread.cpp` / `LinuxThread.cpp`,
`WindowsRenderThread.cpp` / `LinuxRenderThread.cpp`), selected by
`Core/premake5.lua`'s `"Platform/" .. firstToUpper(os.target()) .. "/**.cpp"` glob.

Add a platform implementation by adding the file to both platform folders — not by threading
`#ifdef LUX_PLATFORM_WINDOWS` through shared code. Reserve `#ifdef` for a guarded include or a
one-line constant.

### The DX11/DX12 folders are intentional

`Source/Lux/Platform/DX11/` and `DX12/` are `removefiles`'d in `Core/premake5.lua` and their
`LUX_HAS_DX11` / `LUX_HAS_DX12` branches look dead. They are reserved scaffolding for a future
backend (NVRHI already builds `NVRHI-D3D11` / `NVRHI-D3D12`). **Do not sweep them as dead code.**

### Configuration macros

`LUX_PLATFORM_WINDOWS` / `LUX_PLATFORM_LINUX`, `LUX_DEBUG` / `LUX_RELEASE` / `LUX_DIST`,
`LUX_TRACK_MEMORY` (Debug + Release), `LUX_HAS_VULKAN` (always), `LUX_ENABLE_DISCORD` (only with
`--discord`), `LUX_DISABLE_AFTERMATH` (only with `--no-aftermath`), `TRACY_ENABLE` (unless
`--no-tracy`).

### Prefer a static library when code can stand alone

`Core` is a `StaticLib`; `Editor` and `Lux-Runtime` are the consumers. `Core` must stay usable
without the editor — engine code never includes `Editor/Source/**`. Editor-facing base classes that
the engine genuinely owns (`EditorPanel`, `PanelManager`, `EditorCamera`, `SelectionManager`) live in
`Core/Source/Lux/Editor/`; the editor *application* and its panels live in `Editor/Source/`.

---

## Adding to this doc

When you find a helper that exists but isn't listed, a convention enforced by review but unwritten,
or two places in the tree that contradict each other — add the entry (and, for contradictions, the
resolution). Keep entries terse and link to the canonical header rather than enumerating every
function; function lists drift, headers don't.
