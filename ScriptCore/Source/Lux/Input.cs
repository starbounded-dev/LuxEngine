// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using System.Runtime.InteropServices;

using Coral.Managed.Interop;

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

	// Which button-prompt set to show for a pad. Mirrors the native Lux::GamepadType.
	public enum GamepadType : int
	{
		Unknown = 0,
		Xbox = 1,
		PlayStation = 2,
		Nintendo = 3
	}

	public enum GamepadTrigger : int
	{
		Left = 0,
		Right = 1
	}

	public enum TriggerEffectType : int
	{
		Off = 0,
		Resistance = 1,
		Weapon = 2,
		Vibration = 3
	}

	// DualSense adaptive-trigger effect. Positions are trigger-travel zones from 0 (at rest) to 9
	// (fully pulled); strength is 1..8. Build one with the factory methods. Layout mirrors the
	// native Lux::TriggerEffect, field for field.
	[StructLayout(LayoutKind.Sequential)]
	public struct TriggerEffect
	{
		public TriggerEffectType Type;
		public int Start;
		public int End;
		public int Strength;
		public int Frequency;

		public static TriggerEffect Off => new TriggerEffect { Type = TriggerEffectType.Off };

		// Constant resistance from 'start' to the end of travel.
		public static TriggerEffect Resistance(int start, int strength)
			=> new TriggerEffect { Type = TriggerEffectType.Resistance, Start = start, Strength = strength };

		// Gun-trigger feel: resistance from 'start' (2..7) that gives way with a click at 'end' (start+1..8).
		public static TriggerEffect Weapon(int start, int end, int strength)
			=> new TriggerEffect { Type = TriggerEffectType.Weapon, Start = start, End = end, Strength = strength };

		// Vibrates from 'start' onward. 'amplitude' is 1..8, 'frequency' is 1..255 Hz.
		public static TriggerEffect Vibration(int start, int amplitude, int frequency)
			=> new TriggerEffect { Type = TriggerEffectType.Vibration, Start = start, Strength = amplitude, Frequency = frequency };
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

		// Button-prompt style for the pad (e.g. show "Cross" instead of "A" for PlayStation).
		public static GamepadType GetGamepadType(int gamepad = -1) => InternalCalls.Input_GetGamepadType(gamepad);

		// Raised during Play when a controller is plugged in or removed, with its slot. Pads already
		// connected when Play starts do not raise GamepadConnected. Handlers are cleared when Play stops.
		public static event Action<int>? GamepadConnected;
		public static event Action<int>? GamepadDisconnected;

		// Both sticks as a vector, deadzone applied (Y is +1 down).
		public static Vector2 GetGamepadLeftStick(int gamepad = -1) => new Vector2(GetGamepadAxis(GamepadAxis.LeftX, gamepad), GetGamepadAxis(GamepadAxis.LeftY, gamepad));
		public static Vector2 GetGamepadRightStick(int gamepad = -1) => new Vector2(GetGamepadAxis(GamepadAxis.RightX, gamepad), GetGamepadAxis(GamepadAxis.RightY, gamepad));

		// DualSense adaptive triggers, USB only (Bluetooth logs a warning and does nothing).
		// gamepad -1 targets every connected DualSense; a slot that is not a DualSense is ignored.
		// Every effect is cleared automatically when Play stops or the application exits.
		public static bool SupportsTriggerEffects(int gamepad = -1) => InternalCalls.Input_SupportsTriggerEffects(gamepad);
		public static void SetGamepadTriggerEffect(GamepadTrigger trigger, TriggerEffect effect, int gamepad = -1)
			=> InternalCalls.Input_SetGamepadTriggerEffect(trigger, &effect, gamepad);
		public static void ResetGamepadTriggerEffects() => InternalCalls.Input_ResetGamepadTriggerEffects();

		// Rumble. 'low' drives the large low-frequency motor (heavy thud), 'high' the small
		// high-frequency one (fine buzz), both 0..1. A duration <= 0 rumbles until
		// StopGamepadRumble. gamepad -1 targets every connected gamepad. Works on Xbox, DualSense
		// and DualShock 4 (PlayStation pads over USB) on Windows, and on any pad with kernel force
		// feedback on Linux; other pads ignore it. Stopped automatically when Play stops.
		public static bool SupportsRumble(int gamepad = -1) => InternalCalls.Input_SupportsRumble(gamepad);
		public static void RumbleGamepad(float low, float high, float duration, int gamepad = -1)
			=> InternalCalls.Input_RumbleGamepad(low, high, duration, gamepad);
		public static void StopGamepadRumble(int gamepad = -1) => InternalCalls.Input_StopGamepadRumble(gamepad);

		// Lightbar (DualSense, DualShock 4) and player LEDs (DualSense, player 0 = off .. 4), USB only.
		// gamepad -1 targets every supporting pad; a PlayStation slot reaches every pad of its model.
		// Colour channels are 0..1. Restored to the default when Play stops.
		public static bool SupportsLightbar(int gamepad = -1) => InternalCalls.Input_SupportsLightbar(gamepad);
		public static void SetGamepadLightColor(Vector3 color, int gamepad = -1)
			=> InternalCalls.Input_SetGamepadLightColor(color.X, color.Y, color.Z, gamepad);
		public static void SetGamepadPlayerLights(int player, int gamepad = -1) => InternalCalls.Input_SetGamepadPlayerLights(player, gamepad);
		public static void ResetGamepadLights() => InternalCalls.Input_ResetGamepadLights();

		internal static void DispatchGamepadConnection(int gamepad, int connected)
		{
			Action<int>? handlers = connected != 0 ? GamepadConnected : GamepadDisconnected;
			if (handlers == null)
				return;

			foreach (Action<int> handler in handlers.GetInvocationList())
			{
				try { handler(gamepad); }
				catch (Exception exception)
				{
					using NativeString message = $"Gamepad {(connected != 0 ? "connected" : "disconnected")} handler failed: {exception}";
					InternalCalls.NativeLog(message, 3);
				}
			}
		}

		internal static void ResetGamepadEvents()
		{
			GamepadConnected = null;
			GamepadDisconnected = null;
		}

		// Radial deadzone for all gamepads, as a fraction of full deflection (default 0.15).
		public static float GamepadDeadzone
		{
			get => InternalCalls.Input_GetGamepadDeadzone();
			set => InternalCalls.Input_SetGamepadDeadzone(value);
		}
	}
}
