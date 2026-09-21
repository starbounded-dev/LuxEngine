// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

namespace Lux
{
	// ushort-backed to match the native MouseButton (enum class MouseButton : uint16_t).
	public enum MouseButton : ushort
	{
		Left = 0,
		Right = 1,
		Middle = 2
	}

	public enum CursorMode
	{
		Normal = 0,
		Hidden = 1,
		Locked = 2
	}

	// Standard gamepad layout, the same on every mapped controller (Xbox, PlayStation, Switch Pro...).
	// Face buttons are named by position: South is Xbox A / PlayStation Cross, East is B / Circle,
	// West is X / Square, North is Y / Triangle. Mirrors the native Lux::GamepadButton.
	public enum GamepadButton : int
	{
		South = 0,
		East = 1,
		West = 2,
		North = 3,
		LeftBumper = 4,
		RightBumper = 5,
		Back = 6,
		Start = 7,
		Guide = 8,
		LeftStick = 9,
		RightStick = 10,
		DPadUp = 11,
		DPadRight = 12,
		DPadDown = 13,
		DPadLeft = 14
	}

	// Sticks are -1..1 with +Y pointing DOWN (push the stick forward to get a negative Y);
	// triggers are 0..1. The deadzone is already applied. Mirrors the native Lux::GamepadAxis.
	public enum GamepadAxis : int
	{
		LeftX = 0,
		LeftY = 1,
		RightX = 2,
		RightY = 3,
		LeftTrigger = 4,
		RightTrigger = 5
	}

	public static unsafe class Input
	{
		public static bool IsKeyDown(KeyCode keycode) => InternalCalls.Input_IsKeyDown(keycode);
		public static bool IsKeyPressed(KeyCode keycode) => InternalCalls.Input_IsKeyPressed(keycode);
		public static bool IsKeyHeld(KeyCode keycode) => InternalCalls.Input_IsKeyHeld(keycode);
		public static bool IsKeyReleased(KeyCode keycode) => InternalCalls.Input_IsKeyReleased(keycode);

		public static bool IsMouseButtonDown(MouseButton button) => InternalCalls.Input_IsMouseButtonDown(button);
		public static bool IsMouseButtonPressed(MouseButton button) => InternalCalls.Input_IsMouseButtonPressed(button);
		public static bool IsMouseButtonHeld(MouseButton button) => InternalCalls.Input_IsMouseButtonHeld(button);
		public static bool IsMouseButtonReleased(MouseButton button) => InternalCalls.Input_IsMouseButtonReleased(button);

		public static float MouseX => InternalCalls.Input_GetMouseX();
		public static float MouseY => InternalCalls.Input_GetMouseY();

		public static Vector2 MousePosition
		{
			get
			{
				Vector2 position;
				InternalCalls.Input_GetMousePosition(&position);
				return position;
			}
			set => InternalCalls.Input_SetMousePosition(value.X, value.Y);
		}

		public static void SetCursorMode(CursorMode mode) => InternalCalls.Input_SetCursorMode(mode);

		public static bool IsControllerPresent(int id) => InternalCalls.Input_IsControllerPresent(id);
		public static bool IsControllerButtonDown(int id, int button) => InternalCalls.Input_IsControllerButtonDown(id, button);
		public static float GetControllerAxis(int id, int axis) => InternalCalls.Input_GetControllerAxis(id, axis);

		// Gamepads (standard layout). 'gamepad' is the controller slot; the default -1 means the
		// first connected gamepad. Prefer these over the raw Controller calls above, whose button
		// and axis numbers differ between devices.
		public static bool IsGamepadConnected(int gamepad = -1) => InternalCalls.Input_IsGamepadConnected(gamepad);
		public static string GetGamepadName(int gamepad = -1) => (string?)InternalCalls.Input_GetGamepadName(gamepad) ?? string.Empty;
		public static bool IsGamepadButtonDown(GamepadButton button, int gamepad = -1) => InternalCalls.Input_IsGamepadButtonDown(button, gamepad);
		public static bool IsGamepadButtonPressed(GamepadButton button, int gamepad = -1) => InternalCalls.Input_IsGamepadButtonPressed(button, gamepad);
		public static bool IsGamepadButtonReleased(GamepadButton button, int gamepad = -1) => InternalCalls.Input_IsGamepadButtonReleased(button, gamepad);
		public static float GetGamepadAxis(GamepadAxis axis, int gamepad = -1) => InternalCalls.Input_GetGamepadAxis(axis, gamepad);

		// Both sticks as a vector, deadzone applied (Y is +1 down).
		public static Vector2 GetGamepadLeftStick(int gamepad = -1) => new Vector2(GetGamepadAxis(GamepadAxis.LeftX, gamepad), GetGamepadAxis(GamepadAxis.LeftY, gamepad));
		public static Vector2 GetGamepadRightStick(int gamepad = -1) => new Vector2(GetGamepadAxis(GamepadAxis.RightX, gamepad), GetGamepadAxis(GamepadAxis.RightY, gamepad));

		// Radial deadzone for all gamepads, as a fraction of full deflection (default 0.15).
		public static float GamepadDeadzone
		{
			get => InternalCalls.Input_GetGamepadDeadzone();
			set => InternalCalls.Input_SetGamepadDeadzone(value);
		}
	}
}
