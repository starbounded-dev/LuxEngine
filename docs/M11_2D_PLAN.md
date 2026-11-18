# M11: 2D as a full project type: plan

Parent issue #47. Five sub-issues, worked in this order (the order the board lists them in the
catalog, §9 item 5): D5 -> D4 -> D2 -> P6 -> D3. Each one is one checkpoint: it builds, one concrete
check proves it, it is committed and pushed, its box in the PR is ticked.

## What exists (read from the tree, 2026-11-18)

- `SpriteRendererComponent` (colour, texture, tiling, `UVStart/UVEnd`), `CircleRendererComponent`,
  `TextComponent`, `CameraComponent` (orthographic supported), `RigidBody2DComponent`,
  `BoxCollider2DComponent`, `CircleCollider2DComponent`.
- 2D draw path: `Scene::CaptureDraw2D` -> `FrameRenderPacket::Draw2D` -> `Renderer2D` in the world
  overlay callback. Quads do not write depth, so the **submission order is the draw order**.
- `PhysicsScene2D` (Box2D 2.4): one step per frame with the frame time, box + circle fixtures only,
  no triggers, no queries, no joints, no bodies for spawned entities.
- `EditorCamera` is perspective only; the grid pass draws the XZ floor.
- `Editor/Source/ProjectTemplate.h` only copies the default meshes. `EditorLayer::CreateProject` makes
  one project shape.

## Decisions

| Topic | Decision |
|---|---|
| Project type | `ProjectConfig::Type` (`3D` default, `2D`), serialized as `ProjectType: 2D` only when 2D. The create form gets a Template choice. |
| 2D template | No default meshes; a `Main.lscene` with an orthographic camera, a ground sprite with a static box collider and a player sprite with a dynamic body. Physics API Box2D. |
| 2D editor view | `EditorCamera` 2D mode: orthographic, looks down -Z at the XY plane, pan (MMB / RMB / Alt+LMB), wheel zoom about the cursor. On by default in a 2D project, toggle in the viewport settings. XY grid. Gizmo limited to X/Y translate, Z rotate, XY scale; pixel snapping (1 / PPU) option. |
| Sorting | Project list of sorting layers (stable `uint32` ID, name, list order = draw order; "Default" ID 0 always present). Sprite, circle, text and tilemap carry `SortingLayer` (ID) + `OrderInLayer`. `CaptureDraw2D` stable-sorts by (layer position, order, world Z). |
| Pixel-perfect camera | `CameraComponent::PixelPerfect`, `PixelsPerUnit`, `ReferenceHeight`: integer pixel scale, ortho size derived from it, camera position snapped to the pixel grid when the packet is built. |
| Sprite sheets | New asset `SpriteSheet` (`.lsheet`): texture + frames (name, pixel rect, pivot) + grid slicing parameters. New asset `SpriteAnimation` (`.lsanim`): sheet, frame list, fps, wrap mode. `SpriteRendererComponent` gets `Sheet` + `SpriteIndex`; new `SpriteAnimatorComponent` advances it in Play. |
| Atlas packing | `SpriteAtlas::Pack` (stb_rect_pack) builds a sheet + PNG from many textures; an editor command runs it; export ships sheets as ordinary assets. |
| Box2D completion | Fixed step (`Project FixedTimestep`, at most 8 per frame) with C# `OnFixedUpdate` before each step; polygon, capsule and edge colliders; `IsTrigger` on every collider with `OnTriggerBegin/End`; `Physics2D` C# queries (raycast, overlap point/circle/box); joints (distance, revolute, prismatic, weld); bodies for spawned entities. |
| Tilemaps | New asset `TileSet` (`.ltileset`): texture + tile size + margin/spacing + per-tile solid flag. `TilemapComponent`: tile set, cell size, layers (sparse cells, visible, collides). Rendered as one packet item per layer, culled to the camera. Collision: solid cells merged into boxes on one static body. Tile Palette panel: pick, paint, erase, rectangle, fill, with undo. |

## Verification

- Headless harness (console exe linking `Core.lib`, no window): scene round trips, sorting order,
  sprite sheet slicing, animation sampling, tile merge, Box2D fixed-step determinism and queries.
- `tests/editor/run_project_templates.py` extended for the project type and template text.
- Debug build of `Lux.sln` after every checkpoint.
- No editor launch (it shares per-user state with the main checkout): editor UI is checked by reading
  against the ImGui rules and by the editor self-test hooks where one exists.
