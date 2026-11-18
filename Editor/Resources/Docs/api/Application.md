# Application

`public static class Application`

The running game: quitting it, pausing it, and being told when the window gains or loses focus, the view is resized, or the game ends. Handlers added to its events stay until removed, so remove them in `OnDestroy`; they are all dropped when the game stops.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Height` | `int` | get | The height in pixels of the game's view: the window in an exported game, the Game View in the editor. |
| `IsFocused` | `bool` | get | Whether the game's window (or, in the editor, any editor window) has the keyboard focus and is not minimized. |
| `Paused` | `bool` | get, set | Whether the game is paused: scripts get no `OnUpdate` or `OnFixedUpdate`, physics and animation stand still. Timers and coroutines keep counting real time (`Timer.After(..., unscaled: true)`, [`WaitForSecondsRealtime`](WaitForSecondsRealtime)), which is how a paused game gets unpaused. The accessibility menu pauses it too; [`Application.PauseChanged`](Application) reports both. |
| `Width` | `int` | get | The width in pixels of the game's view: the window in an exported game, the Game View in the editor. |

## Events

| Name | Type | Description |
|---|---|---|
| `FocusChanged` | `Action<bool>` | Raised when the window gains or loses focus, with the new [`Application.IsFocused`](Application). Checked once a frame. |
| `PauseChanged` | `Action<bool>` | Raised when the game is paused or unpaused, with the new [`Application.Paused`](Application). Checked once a frame. |
| `Quitting` | `Action` | Raised once when the game ends: the window is closed or [`Application.Quit`](Application) is called, and in the editor when Play stops. Scripts still exist then; their `OnDestroy` follows. Not raised when a script loads another scene. |
| `Resized` | `Action<int, int>` | Raised when the game's view changes size, with the new [`Application.Width`](Application) and [`Application.Height`](Application). Not raised for a size of 0 (minimized). |

## Methods

```
public static void Quit()
```

Ends the game after the current frame, as closing its window does. In the editor it does nothing but log a warning: stop Play there instead.

[All types](index)
