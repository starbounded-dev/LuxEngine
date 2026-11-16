# Input

`public static class Input`

Keyboard, mouse and gamepad input, read from scripts.

`Down` is true while a key or button is down; `Pressed` only on the frame it went down; `Held` while it stays down after that frame; `Released` only on the frame it went up.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `GamepadDeadzone` | `float` | get, set | The radial stick deadzone for every gamepad, as a fraction of full deflection (default 0.15). |
| `MousePosition` | `Vector2` | get, set | The cursor's position in pixels from the window's top-left corner (in the editor, the editor window's). Setting it moves the cursor. |
| `MouseX` | `float` | get | The cursor's X position in pixels from the window's left edge. |
| `MouseY` | `float` | get | The cursor's Y position in pixels from the window's top edge. |

## Events

| Name | Type | Description |
|---|---|---|
| `GamepadConnected` | `Action<int>` | Raised during Play when a gamepad is plugged in, with its slot. Pads connected before Play starts don't raise it. Handlers are cleared when Play stops. |
| `GamepadDisconnected` | `Action<int>` | Raised during Play when a gamepad is removed, with its slot. Handlers are cleared when Play stops. |

## Methods

```
public static float GetControllerAxis(int id, int axis)
```

Raw axis `axis` of joystick `id`. Axis numbers differ between devices; prefer [`Input.GetGamepadAxis`](Input).

```
public static float GetGamepadAxis(GamepadAxis axis, int gamepad = -1)
```

A gamepad axis, deadzone applied (see [`GamepadAxis`](GamepadAxis) for ranges).

- `gamepad`: The controller slot; -1 means the first connected gamepad.

```
public static Vector2 GetGamepadLeftStick(int gamepad = -1)
```

The left stick as a vector, deadzone applied (+Y is down).

- `gamepad`: The controller slot; -1 means the first connected gamepad.

```
public static string GetGamepadName(int gamepad = -1)
```

The gamepad's device name, or an empty string.

- `gamepad`: The controller slot; -1 means the first connected gamepad.

```
public static Vector2 GetGamepadRightStick(int gamepad = -1)
```

The right stick as a vector, deadzone applied (+Y is down).

- `gamepad`: The controller slot; -1 means the first connected gamepad.

```
public static GamepadType GetGamepadType(int gamepad = -1)
```

The gamepad's family, to show matching button prompts (Cross instead of A, say).

- `gamepad`: The controller slot; -1 means the first connected gamepad.

```
public static bool IsControllerButtonDown(int id, int button)
```

Whether raw button `button` of joystick `id` is down. Button numbers differ between devices; prefer [`Input.IsGamepadButtonDown`](Input).

```
public static bool IsControllerPresent(int id)
```

Whether a joystick is connected in slot `id` (raw; prefer the gamepad calls).

```
public static bool IsGamepadButtonDown(GamepadButton button, int gamepad = -1)
```

Whether a gamepad button is down.

- `gamepad`: The controller slot; -1 means the first connected gamepad.

```
public static bool IsGamepadButtonPressed(GamepadButton button, int gamepad = -1)
```

Whether a gamepad button went down this frame.

- `gamepad`: The controller slot; -1 means the first connected gamepad.

```
public static bool IsGamepadButtonReleased(GamepadButton button, int gamepad = -1)
```

Whether a gamepad button went up this frame.

- `gamepad`: The controller slot; -1 means the first connected gamepad.

```
public static bool IsGamepadConnected(int gamepad = -1)
```

Whether a gamepad is connected.

- `gamepad`: The controller slot; -1 means the first connected gamepad.

```
public static bool IsKeyDown(KeyCode keycode)
```

Whether the key is down.

```
public static bool IsKeyHeld(KeyCode keycode)
```

Whether the key has stayed down since an earlier frame.

```
public static bool IsKeyPressed(KeyCode keycode)
```

Whether the key went down this frame.

```
public static bool IsKeyReleased(KeyCode keycode)
```

Whether the key went up this frame.

```
public static bool IsMouseButtonDown(MouseButton button)
```

Whether the mouse button is down.

```
public static bool IsMouseButtonHeld(MouseButton button)
```

Whether the mouse button has stayed down since an earlier frame.

```
public static bool IsMouseButtonPressed(MouseButton button)
```

Whether the mouse button went down this frame.

```
public static bool IsMouseButtonReleased(MouseButton button)
```

Whether the mouse button went up this frame.

```
public static void ResetGamepadLights()
```

Restores every pad's lightbar and player LEDs to the default.

```
public static void ResetGamepadTriggerEffects()
```

Clears every adaptive-trigger effect on every pad.

```
public static void RumbleGamepad(float low, float high, float duration, int gamepad = -1)
```

Rumbles a gamepad. Works on Xbox pads, and PlayStation pads over USB on Windows; on Linux on any pad with force feedback. Other pads ignore it. Stopped when Play stops.

- `low`: The large low-frequency motor (a heavy thud), 0 to 1.
- `high`: The small high-frequency motor (a fine buzz), 0 to 1.
- `duration`: Seconds; 0 or less rumbles until [`Input.StopGamepadRumble`](Input).
- `gamepad`: The controller slot; -1 means every connected gamepad.

```
public static void SetCursorMode(CursorMode mode)
```

Shows, hides or captures the mouse cursor.

```
public static void SetGamepadLightColor(Vector3 color, int gamepad = -1)
```

Sets the lightbar color. Restored to the default when Play stops.

- `color`: RGB, components from 0 to 1.
- `gamepad`: The controller slot; -1 means every pad with a lightbar.

```
public static void SetGamepadPlayerLights(int player, int gamepad = -1)
```

Sets the DualSense player LEDs. Restored to the default when Play stops.

- `player`: 0 (off) to 4.
- `gamepad`: The controller slot; -1 means every DualSense.

```
public static void SetGamepadTriggerEffect(GamepadTrigger trigger, TriggerEffect effect, int gamepad = -1)
```

Sets a DualSense adaptive-trigger effect (USB only; over Bluetooth it logs a warning and does nothing). Cleared when Play stops.

- `trigger`: Which trigger.
- `effect`: The effect.
- `gamepad`: The controller slot; -1 means every connected DualSense. Other pads ignore it.

```
public static void StopGamepadRumble(int gamepad = -1)
```

Stops rumbling.

- `gamepad`: The controller slot; -1 means every connected gamepad.

```
public static bool SupportsLightbar(int gamepad = -1)
```

Whether the gamepad has a lightbar (a DualSense or DualShock 4 over USB).

- `gamepad`: The controller slot; -1 asks about any connected gamepad.

```
public static bool SupportsRumble(int gamepad = -1)
```

Whether the gamepad can rumble.

- `gamepad`: The controller slot; -1 asks about any connected gamepad.

```
public static bool SupportsTriggerEffects(int gamepad = -1)
```

Whether the gamepad has adaptive triggers (a DualSense over USB).

- `gamepad`: The controller slot; -1 asks about any connected gamepad.

[All types](index)
