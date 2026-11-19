# Game UI plan (M3, issue #39)

Point-in-time plan. Checkpoints are the board's sub-issues, in dependency order.

## Decisions

- **UI is scene data.** A canvas is an entity with `UICanvasComponent`; its descendants carry `UIElementComponent`
  (rectangle, anchors, optional layout group) and a look: `UIImageComponent`, `UITextComponent`, `UIWidgetComponent`
  (button, toggle, slider, progress bar, text input, scroll list). The scene hierarchy, undo, prefabs, YAML and the
  C# `Entity` API therefore work with no new machinery.
- **Screen space only** for 2027.1 (world-space UI is U7, deferred to 2028).
- **Layout is a pure function** (`Lux/UI/UILayout`): parent rectangle + anchors + offsets + pivot -> rectangle;
  stack and grid groups; resolution scaling from a reference resolution. No registry, no GPU, so it is checked
  headless (`tests/ui`).
- **Origin bottom-left, y up**, like the existing runtime overlay projection and Unity's rect transform.
- **Drawing** goes through the existing frame packet: `Scene::CaptureUI` fills `FrameRenderPacket::UI` (quads,
  textured quads, text, in draw order) and the overlay pass draws them with a pixel-space orthographic projection after
  the world 2D items. Same code in editor Play, the Game View and the exported player.
- **Input:** focus and activation live in `UISystem` (Core), fed by `Input` (keyboard, gamepad, mouse); it runs in
  `OnUpdateRuntime`.

## Checkpoints, in order

1. U1 canvas, anchors, stack/grid layout, resolution scaling, drawing (rects and images, text)
2. U2 widgets (image 9-slice, text, button, toggle, slider, progress bar, text input, scroll list)
3. U3 keyboard and gamepad focus navigation
4. U6 C# API (find widget, set text, callbacks)
5. U4 captions and subtitles drawn by it
6. U5 / E9 UI editing in the Game View (rect gizmo, create menu, inspector)
