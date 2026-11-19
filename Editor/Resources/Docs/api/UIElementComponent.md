# UIElementComponent

`public class UIElementComponent : Component`

A rectangle in a user interface, placed inside its parent by anchors and offsets.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `AnchorMax` | `Vector2` | get, set | The top-right anchor, as a fraction of the parent (1, 1 is its top right corner). |
| `AnchorMin` | `Vector2` | get, set | The bottom-left anchor, as a fraction of the parent (0, 0 is its bottom left corner). |
| `OffsetMax` | `Vector2` | get, set | Distance in UI units from the maximum anchor to the element's top-right corner. |
| `OffsetMin` | `Vector2` | get, set | Distance in UI units from the minimum anchor to the element's bottom-left corner. |
| `Visible` | `bool` | get, set | Whether the element and everything under it is shown and takes part in layout. |

[All types](index)
