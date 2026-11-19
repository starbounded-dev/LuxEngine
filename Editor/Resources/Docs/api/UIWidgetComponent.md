# UIWidgetComponent

`public class UIWidgetComponent : Component`

Behaviour of an element: a button, toggle, slider, progress bar, text input or scroll list. Subscribe to the events to react to the player; handlers are dropped when the scene ends. Setting a value from a script raises no event.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Checked` | `bool` | get, set | A toggle's state. |
| `Interactable` | `bool` | get, set | Whether the widget reacts to the pointer, keyboard and gamepad. A disabled one is drawn dimmed. |
| `IsFocused` | `bool` | get | True while the widget has the keyboard and gamepad focus. |
| `IsHovered` | `bool` | get | True while the pointer is over the widget (and no widget in front of it is). |
| `IsPressed` | `bool` | get | True while the primary button is held down on the widget. |
| `MaxValue` | `float` | get, set | The value at the right end of a slider or progress bar. |
| `MinValue` | `float` | get, set | The value at the left end of a slider or progress bar. |
| `Placeholder` | `string` | get, set | The text a text input shows, in half strength, while it is empty and not being edited. |
| `ScrollOffset` | `float` | get, set | How far a scroll list's content is moved up, in UI units. |
| `Value` | `float` | get, set | A slider's or progress bar's value, between [`UIWidgetComponent.MinValue`](UIWidgetComponent) and [`UIWidgetComponent.MaxValue`](UIWidgetComponent). |
| `WholeNumbers` | `bool` | get, set | Whether a slider moves in whole steps. |

## Events

| Name | Type | Description |
|---|---|---|
| `Cancelled` | `Action` | The player pressed Cancel while this widget was focused (not while editing a text input). |
| `Clicked` | `Action` | A button was clicked (pressed and released on it, or confirmed with the keyboard or gamepad). |
| `Submitted` | `Action<string>` | The player pressed Enter in a text input; the argument is its text. |
| `TextChanged` | `Action<string>` | The player changed a text input's text; the argument is the whole text. |
| `Toggled` | `Action<bool>` | A toggle was flipped by the player; the argument is its new state. |
| `ValueChanged` | `Action<float>` | A slider was moved by the player; the argument is its new value. |

## Methods

```
public void Click()
```

Clicks the widget as the player would: a button raises [`UIWidgetComponent.Clicked`](UIWidgetComponent), a toggle flips and raises [`UIWidgetComponent.Toggled`](UIWidgetComponent). The handlers run at the start of the next frame. Does nothing to a disabled widget.

```
public void Focus()
```

Gives the widget the focus, as if the player had navigated to it. Use it to pick the first button of a menu.

[All types](index)
