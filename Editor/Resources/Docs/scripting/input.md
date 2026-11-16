# Input

The static `Input` class reads the keyboard, mouse and gamepads. Use it from `OnUpdate` in a [C# script](csharp-scripts).

## Keyboard and mouse

| Call | True when |
|---|---|
| `Input.IsKeyPressed(KeyCode.Space)` | the key went down this frame |
| `Input.IsKeyDown(KeyCode.W)` | the key is down |
| `Input.IsKeyHeld(KeyCode.W)` | the key is down and was down last frame too |
| `Input.IsKeyReleased(KeyCode.W)` | the key went up this frame |
| `Input.IsMouseButtonPressed(MouseButton.Left)` | likewise for mouse buttons (`Down`, `Held`, `Released` too) |

`Input.MousePosition` (and `MouseX`, `MouseY`) give the cursor position. `Input.SetCursorMode(CursorMode.Locked)` hides and locks the cursor for mouse-look; `CursorMode.Normal` gives it back.

```
void OnUpdate(float ts)
{
	Vector3 move = Vector3.Zero;
	if (Input.IsKeyDown(KeyCode.W)) move.Z -= 1.0f;
	if (Input.IsKeyDown(KeyCode.S)) move.Z += 1.0f;
	if (Input.IsKeyDown(KeyCode.A)) move.X -= 1.0f;
	if (Input.IsKeyDown(KeyCode.D)) move.X += 1.0f;
	Translation += move * (5.0f * ts);
}
```

## Gamepads

Gamepads use a standard layout, whatever the brand. Face buttons are named by position: `South` is A on Xbox and Cross on PlayStation, `East` is B / Circle, `West` is X / Square, `North` is Y / Triangle. Each call takes an optional gamepad number; leave it out to use the first connected gamepad.

| Call | Gives |
|---|---|
| `Input.IsGamepadConnected()` | whether a gamepad is connected |
| `Input.IsGamepadButtonPressed(GamepadButton.South)` | a button went down this frame (`Down`, `Released` too) |
| `Input.GetGamepadAxis(GamepadAxis.LeftX)` | a stick or trigger value; sticks are -1 to 1 (pushing a stick forward gives a negative Y), triggers 0 to 1 |
| `Input.GetGamepadLeftStick()` | the left stick as a `Vector2` |
| `Input.GetGamepadType()` | Xbox, PlayStation, Nintendo or Unknown, for showing the right button prompts |
| `Input.RumbleGamepad(low, high, seconds)` | vibration on gamepads that support it |

`Input.GamepadConnected` and `Input.GamepadDisconnected` are events raised when a gamepad is plugged in or removed during play.
