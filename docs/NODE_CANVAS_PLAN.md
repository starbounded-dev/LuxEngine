# LuxEngine Node Canvas Plan

One editor panel, the **Node Canvas**, that hosts every node graph as a tab: animation graphs, material graphs, sound
trees, later procedural materials. Opening a graph asset opens (or focuses) its tab. This is a planning document, not
architecture; what is built is described in `.claude/docs/Architecture-LuxEngine.md`. Facts below were read from the
source on 2026-11-17 on `origin/dev` (bf065e64) unless marked otherwise.

**Goal card**
- **Goal:** one Node Canvas panel with a tab per graph document, so every graph type shares navigation, palette, minimap, clipboard and undo.
- **Success:** double-click a `.lanimgraph`, `.lmaterial` or `.lsound` and a tab opens in the Node Canvas; two graphs can be open at once; state (pan, zoom, selection) survives tab switches and editor restarts; the material graph behaves as before.
- **Non-goals:** a visual scripting language; changing graph file formats; replacing ImGui; new mandatory dependencies.
- **Constraints:** self-contained (reuse the vendored imgui-node-editor); main thread only (ImGui); existing `.lmaterial` and `.lanimgraph` files load unchanged; no temporal anything.
- **User words:** "a single node canva panel ... different tabs for the different uses ... animations graphs, materials, audio, procedural materials".

## Decisions

| Decision | Choice | Consequence |
|---|---|---|
| Panel count | One `NodeCanvasPanel`, **multiple instances allowed** (a second one can be opened and docked beside the first) | Two graphs side by side without tearing tabs out; each instance owns its tabs |
| Tab state | One `ed::EditorContext` per open document | Pan, zoom and selection are per tab; only one context is current per frame |
| Extension point | A `GraphDocument` interface, one implementation per graph type | New graph types add one class and one registration, no canvas changes |
| Build order | Run the material canvas first, then extract; animation graph first, materials last | Avoids carrying unverified bugs into the shared layer; the animation graph is the cheapest first consumer |
| File formats | Unchanged | No migration; old assets load |
| Open routing | By `AssetType` through `ContentBrowserPanel::RegisterItemActivateCallbackForType` | One registry replaces the per-panel callbacks |

## Part 0 - Where we are

| Capability | State | Evidence |
|---|---|---|
| Vendored node editor | Working library | `Core/vendor/imgui-node-editor` (thedmd, MIT) |
| Material graph canvas | Built, **never run** (FEATURE_CATALOG_2027 E5) | `Editor/Source/Panels/MaterialEditor/MaterialGraphCanvas.{h,cpp}` (108 + 1084 lines): toolbar, palette, inspector, minimap, find, clipboard, `Snapshot`/`Restore` text snapshots, `SaveState`/`LoadState`, one `ed::EditorContext` |
| Material canvas coupling | Tied to `MaterialAsset` / `MaterialGraph` in every method signature | `Draw(const Ref<MaterialAsset>&, UndoPushFn)`; hosted by `MaterialEditorPanel` (`m_GraphCanvas`) |
| Undo | Callback-based, already abstract | `UndoPushFn(label, undo, redo)`; the panel forwards it to the editor undo stack (`SetUndoCallback`) |
| Opening assets | Per-type callbacks | `EditorLayer.cpp`: `RegisterItemActivateCallbackForType(AssetType::Material, ...)` |
| Sound tree editor | Separate panel | `Editor/Source/Panels/SoundEditor/SoundEditorPanel`; `.lsound` is a node tree |
| Animation graph | Data model and YAML only; **on PR #160, not on dev** (`feature/skeletal-animation`); the inspector lists states as text | `Core/Source/Lux/Animation/AnimationGraph.h`, `SceneHierarchyPanel.cpp` `DrawSkeletonAnimationTools` |
| Procedural materials | Does not exist | n/a |
| Shared canvas layer | Does not exist | n/a |

## Part 1 - Design

```
NodeCanvasPanel (EditorPanel, main thread)
  tab bar  -->  open GraphDocument list
  active tab: toolbar | canvas | side panel (document supplied)

GraphDocument (interface, one per graph type)
  AssetHandle Handle(); AssetType Type(); std::string Title(); bool IsDirty();
  Nodes: node library for the palette; pins and link rules (pin kinds, can-connect)
  Draw node bodies / node inspector (document specific)
  Save(); Snapshot(); Restore(snapshot)           // undo and dirty checks, text based
  Compile/Validate() -> diagnostics shown on nodes
  optional: DrawPreview(), DrawSidePanel()

NodeCanvasCore (shared, extracted from MaterialGraphCanvas)
  one ed::EditorContext per document, pan/zoom/selection save-load
  palette + fuzzy search, find, minimap, clipboard, comment groups, undo push
```

- **System and threads.** `Editor` only; ImGui on the main thread. `Core` never includes the canvas. Documents read and write assets through `AssetManager` on the main thread, as the Material Editor does today.
- **Ownership.** Documents are `Scope<GraphDocument>` owned by the panel; they hold `Ref<>` to their asset. Closing a tab drops the context. A document whose asset is deleted or reloaded closes its tab with a notice.
- **Serialization.** Open tabs, active tab and per-tab view state are saved in editor layout state (the existing `SaveState`/`LoadState` pattern), never in scene or asset files, so they cannot dirty a scene or leak into `LuxSample.luxproj`.
- **Opening.** The content browser keeps one registry: `AssetType -> factory(AssetHandle) -> GraphDocument`. Activating an asset focuses its tab if open, else creates one. The scene hierarchy "open material" shortcut goes through the same call.
- **Docking.** Instances are ordinary dockable panels. An opened asset goes to the most recently focused Node Canvas instance.
- **Dirty and close.** Dirty tabs ask Save, Discard or Cancel on close (reuse the Material Editor flow). Undo applies to the active document only.
- **Runtime, Dist, Linux.** Editor tooling; not in `Lux-Runtime`. Nothing here touches `Platform/`.

## Part 2 - Phases

### Phase 0 - Run what exists
Open the Material Editor with a graph material in the editor once (needs explicit permission to launch it) and record what works and what is broken. Existing coverage: `MaterialEditorPanelTests.cpp` self-tests. **Exit:** a short list of material canvas defects, fixed or filed.

### Phase 1 - Extract the shared canvas, no behavior change
- NEW `Editor/Source/Panels/NodeCanvas/NodeCanvasCore.{h,cpp}`: palette, find, minimap, clipboard, groups, per-context view state, taken out of `MaterialGraphCanvas`.
- NEW `GraphDocument.h`; NEW `MaterialGraphDocument` implementing it. `MaterialGraphCanvas` becomes a thin wrapper, still hosted by `MaterialEditorPanel`.
- **Verification:** `MaterialEditorPanelTests` unchanged and passing; the material graph looks and edits as in Phase 0 (screenshots before and after). New files need `scripts\Win-GenProjects.bat`.
- **Rollback:** revert the extraction commit.

### Phase 2 - The Node Canvas panel with one tab type
- NEW `NodeCanvasPanel`: tab bar, per-tab context, multiple instances, open/focus/close, dirty prompt, layout state save and load.
- Move the material graph into it. Decide at the start of this phase, from the Phase 0 findings, whether the preview and parameters stay in the Material Editor or arrive as the document side panel.
- **Verification:** open two materials in two tabs; edit, undo and redo in each; close a dirty tab; restart the editor and see the tabs restored. ImGui audit: every `ed::Begin` has its `ed::End`, `SetCurrentEditor(nullptr)` on all paths, unique IDs per tab (`###` titles, `PushID(document)`).

### Phase 3 - Animation graph document
- NEW `AnimationGraphDocument`: states as nodes, transitions as links with a condition list in the link inspector, parameters in a side panel, live current-state highlight and parameter widgets while previewing (these exist as text in `DrawSkeletonAnimationTools`); writes through `AnimationGraphSerializer`.
- Prerequisite: PR #160 merged (the data model is on `feature/skeletal-animation`).
- Double-click on `.lanimgraph` opens the tab; Create > Animation Graph stays.
- **Verification:** round-trip the LuxBiped `.lanimgraph` through the editor with no diff; `Validate` errors shown on the offending transition; animation self-tests pass.

### Phase 4 - Sound tree document
Port `.lsound` from `SoundEditorPanel` as a document if its tree view maps onto the canvas; otherwise keep the panel and record it as a follow-up. Decide after reading `SoundEditorPanel`; do not force it.

### Phase 5 - Procedural materials
A new document type, only after the material graph is stable. Its scope belongs in its own plan; this phase exists so the interface is checked against a fourth consumer.

### Phase 6 - Hardening and docs
Large graphs (150+ nodes, `/profile` canvas cost per tab; inactive tabs must cost nothing), narrow-panel layout, shortcuts scoped to the focused canvas. Update `.claude/docs/Architecture-LuxEngine.md` (editor panels) and `docs/EDITOR_REMASTER_PLAN.md`, which describes the Material Editor as a standalone panel.

## Part 3 - Risks and open questions

| Risk or question | Likelihood | Handling |
|---|---|---|
| imgui-node-editor has one current context at a time; several instances plus tabs could cross wires | Medium | Phase 2 spike: two instances drawing in one frame, `SetCurrentEditor` strictly bracketed |
| The material canvas is unverified, so extraction hides defects | Medium | Phase 0 first |
| Material preview and canvas compete for space (`EDITOR_REMASTER_PLAN` phase 3) | Medium | Layout decision at the start of Phase 2 |
| Graph types differ: state-machine links with conditions versus data-flow pins | Medium | The interface asks the document for pin kinds and link rules instead of assuming data flow; Phase 3 is the test |
| The sound tree may not fit a node canvas | Low-Medium | Phase 4 is optional |

**Would invalidate the plan:** Phase 0 showing the material canvas needs a rewrite, not fixes. Then extract from the animation graph first and port materials afterward.

## Part 4 - Research notes
- imgui-node-editor keeps interaction in the library and leaves node content, link rules and persistence to the application ([docs](https://github.com/thedmd/imgui-node-editor/blob/master/docs/README.md)). This plan keeps that split: library and shared behavior in the core, content in documents.
- Editors that host many graph types over one graph view (Unreal Blueprint, Unity Shader Graph on GraphView) use a shared view plus per-asset-type document classes. This is the same shape. Their pain point is graph types with different link semantics, hence the pin and link-rule hook.
