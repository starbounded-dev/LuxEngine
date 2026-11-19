# UI

`public static class UI`

Game UI events that are not about one widget. A widget's own events ([`UIWidgetComponent.Clicked`](UIWidgetComponent) and the others) are on its component.

## Events

| Name | Type | Description |
|---|---|---|
| `Cancelled` | `Action` | Raised when the player presses Cancel (Escape, or the east button of a gamepad) while no text input is being edited: the place to close a menu. Handlers are dropped when the scene ends. |

[All types](index)
