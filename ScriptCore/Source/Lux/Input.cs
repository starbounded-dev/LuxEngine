// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using System.Runtime.InteropServices;

using Coral.Managed.Interop;

namespace Lux
{
	// ushort-backed to match the native MouseButton (enum class MouseButton : uint16_t).
	/// <summary>A mouse button.</summary>
	public enum MouseButton : ushort
	{
		Left = 0,
		Right = 1,
		Middle = 2
	}

	/// <summary>How the mouse cursor behaves over the game window, for <see cref="Input.SetCursorMode"/>.</summary>
	public enum CursorMode
	{
		/// <summary>Visible and free.</summary>
		Normal = 0,
		/// <summary>Invisible over the window, but free to leave it.</summary>
		Hidden = 1,
		/// <summary>Invisible and captured, for mouse-look: the position keeps changing without limit.</summary>
		Locked = 2
	}

	// Mirrors the native Lux::GamepadButton.
	/// <summary>
	/// A gamepad button in the standard layout, the same on every mapped controller. Face buttons are named by
	/// position: <c>South</c> is Xbox A / PlayStation Cross, <c>East</c> is B / Circle, <c>West</c> is X / Square and
	/// <c>North</c> is Y / Triangle.
	/// </summary>
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

	// Mirrors the native Lux::GamepadAxis.
	/// <summary>
	/// A gamepad axis. Sticks read -1 to 1 with +Y pointing down (pushing a stick forward gives a negative Y);
	/// triggers read 0 to 1. The deadzone is already applied.
	/// </summary>
	public enum GamepadAxis : int
	{
		LeftX = 0,
		LeftY = 1,
		RightX = 2,
		RightY = 3,
		LeftTrigger = 4,
		RightTrigger = 5
	}

	// Mirrors the native Lux::GamepadType.
	/// <summary>Which family a gamepad belongs to, to pick its button prompts.</summary>
	public enum GamepadType : int
	{
		Unknown = 0,
		Xbox = 1,
		PlayStation = 2,
		Nintendo = 3
	}

	/// <summary>The left or right trigger, for adaptive trigger effects.</summary>
	public enum GamepadTrigger : int
	{
		Left = 0,
		Right = 1
	}

	/// <summary>The kind of a <see cref="TriggerEffect"/>.</summary>
	public enum TriggerEffectType : int
	{
		Off = 0,
		Resistance = 1,
		Weapon = 2,
		Vibration = 3
	}

	// Layout mirrors the native Lux::TriggerEffect, field for field.
	/// <summary>
	/// A DualSense adaptive-trigger effect, built with <see cref="Resistance"/>, <see cref="Weapon"/>,
	/// <see cref="Vibration"/> or <see cref="Off"/>. Positions are trigger-travel zones from 0 (at rest) to 9
	/// (fully pulled).
	/// </summary>
	[StructLayout(LayoutKind.Sequential)]
	public struct TriggerEffect
	{
		/// <summary>The kind of effect.</summary>
		public TriggerEffectType Type;
		/// <summary>The zone where the effect starts.</summary>
		public int Start;
		/// <summary>The zone where a weapon effect gives way.</summary>
		public int End;
		/// <summary>Strength (or vibration amplitude), 1 to 8.</summary>
		public int Strength;
		/// <summary>Vibration frequency in Hz, 1 to 255.</summary>
		public int Frequency;

		/// <summary>No effect: the trigger moves freely.</summary>
		public static TriggerEffect Off => new TriggerEffect { Type = TriggerEffectType.Off };

		/// <summary>Constant resistance from <paramref name="start"/> to the end of travel.</summary>
		/// <param name="start">Zone 0 to 9.</param>
		/// <param name="strength">1 to 8.</param>
		public static TriggerEffect Resistance(int start, int strength)
			=> new TriggerEffect { Type = TriggerEffectType.Resistance, Start = start, Strength = strength };

		/// <summary>A gun-trigger feel: resistance that gives way with a click.</summary>
		/// <param name="start">Where the resistance starts, zone 2 to 7.</param>
		/// <param name="end">Where it gives way, <c>start + 1</c> to 8.</param>
		/// <param name="strength">1 to 8.</param>
		public static TriggerEffect Weapon(int start, int end, int strength)
			=> new TriggerEffect { Type = TriggerEffectType.Weapon, Start = start, End = end, Strength = strength };

		/// <summary>Vibrates from <paramref name="start"/> onward.</summary>
		/// <param name="start">Zone 0 to 9.</param>
		/// <param name="amplitude">1 to 8.</param>
		/// <param name="frequency">1 to 255 Hz.</param>
		public static TriggerEffect Vibration(int start, int amplitude, int frequency)
			=> new TriggerEffect { Type = TriggerEffectType.Vibration, Start = start, Strength = amplitude, Frequency = frequency };
	}

	/// <summary>Keyboard, mouse and gamepad input, read from scripts.</summary>
	/// <remarks>
	/// <c>Down</c> is true while a key or button is down; <c>Pressed</c> only on the frame it went down; <c>Held</c>
	/// while it stays down after that frame; <c>Released</c> only on the frame it went up.
	/// </remarks>
	public static unsafe class Input
	{
		/// <summary>Whether the key is down.</summary>
		public static bool IsKeyDown(KeyCode keycode) => InternalCalls.Input_IsKeyDown(keycode);
		/// <summary>Whether the key went down this frame.</summary>
		public static bool IsKeyPressed(KeyCode keycode) => InternalCalls.Input_IsKeyPressed(keycode);
		/// <summary>Whether the key has stayed down since an earlier frame.</summary>
		public static bool IsKeyHeld(KeyCode keycode) => InternalCalls.Input_IsKeyHeld(keycode);
		/// <summary>Whether the key went up this frame.</summary>
		public static bool IsKeyReleased(KeyCode keycode) => InternalCalls.Input_IsKeyReleased(keycode);

		/// <summary>Whether the mouse button is down.</summary>
		public static bool IsMouseButtonDown(MouseButton button) => InternalCalls.Input_IsMouseButtonDown(button);
		/// <summary>Whether the mouse button went down this frame.</summary>
		public static bool IsMouseButtonPressed(MouseButton button) => InternalCalls.Input_IsMouseButtonPressed(button);
		/// <summary>Whether the mouse button has stayed down since an earlier frame.</summary>
		public static bool IsMouseButtonHeld(MouseButton button) => InternalCalls.Input_IsMouseButtonHeld(button);
		/// <summary>Whether the mouse button went up this frame.</summary>
		public static bool IsMouseButtonReleased(MouseButton button) => InternalCalls.Input_IsMouseButtonReleased(button);

		/// <summary>The cursor's X position in pixels from the window's left edge.</summary>
		public static float MouseX => InternalCalls.Input_GetMouseX();
		/// <summary>The cursor's Y position in pixels from the window's top edge.</summary>
		public static float MouseY => InternalCalls.Input_GetMouseY();

		/// <summary>
		/// The cursor's position in pixels from the window's top-left corner (in the editor, the editor window's).
		/// Setting it moves the cursor.
		/// </summary>
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

		/// <summary>Shows, hides or captures the mouse cursor.</summary>
		public static void SetCursorMode(CursorMode mode) => InternalCalls.Input_SetCursorMode(mode);

		/// <summary>Whether a joystick is connected in slot <paramref name="id"/> (raw; prefer the gamepad calls).</summary>
		public static bool IsControllerPresent(int id) => InternalCalls.Input_IsControllerPresent(id);
		/// <summary>
		/// Whether raw button <paramref name="button"/> of joystick <paramref name="id"/> is down. Button numbers differ
		/// between devices; prefer <see cref="IsGamepadButtonDown"/>.
		/// </summary>
		public static bool IsControllerButtonDown(int id, int button) => InternalCalls.Input_IsControllerButtonDown(id, button);
		/// <summary>
		/// Raw axis <paramref name="axis"/> of joystick <paramref name="id"/>. Axis numbers differ between devices;
		/// prefer <see cref="GetGamepadAxis"/>.
		/// </summary>
		public static float GetControllerAxis(int id, int axis) => InternalCalls.Input_GetControllerAxis(id, axis);

		/// <summary>Whether a gamepad is connected.</summary>
		/// <param name="gamepad">The controller slot; -1 means the first connected gamepad.</param>
		public static bool IsGamepadConnected(int gamepad = -1) => InternalCalls.Input_IsGamepadConnected(gamepad);
		/// <summary>The gamepad's device name, or an empty string.</summary>
		/// <param name="gamepad">The controller slot; -1 means the first connected gamepad.</param>
		public static string GetGamepadName(int gamepad = -1) => (string?)InternalCalls.Input_GetGamepadName(gamepad) ?? string.Empty;
		/// <summary>Whether a gamepad button is down.</summary>
		/// <param name="gamepad">The controller slot; -1 means the first connected gamepad.</param>
		public static bool IsGamepadButtonDown(GamepadButton button, int gamepad = -1) => InternalCalls.Input_IsGamepadButtonDown(button, gamepad);
		/// <summary>Whether a gamepad button went down this frame.</summary>
		/// <param name="gamepad">The controller slot; -1 means the first connected gamepad.</param>
		public static bool IsGamepadButtonPressed(GamepadButton button, int gamepad = -1) => InternalCalls.Input_IsGamepadButtonPressed(button, gamepad);
		/// <summary>Whether a gamepad button went up this frame.</summary>
		/// <param name="gamepad">The controller slot; -1 means the first connected gamepad.</param>
		public static bool IsGamepadButtonReleased(GamepadButton button, int gamepad = -1) => InternalCalls.Input_IsGamepadButtonReleased(button, gamepad);
		/// <summary>A gamepad axis, deadzone applied (see <see cref="GamepadAxis"/> for ranges).</summary>
		/// <param name="gamepad">The controller slot; -1 means the first connected gamepad.</param>
		public static float GetGamepadAxis(GamepadAxis axis, int gamepad = -1) => InternalCalls.Input_GetGamepadAxis(axis, gamepad);

		/// <summary>The gamepad's family, to show matching button prompts (Cross instead of A, say).</summary>
		/// <param name="gamepad">The controller slot; -1 means the first connected gamepad.</param>
		public static GamepadType GetGamepadType(int gamepad = -1) => InternalCalls.Input_GetGamepadType(gamepad);

		/// <summary>
		/// Raised during Play when a gamepad is plugged in, with its slot. Pads connected before Play starts don't
		/// raise it. Handlers are cleared when Play stops.
		/// </summary>
		public static event Action<int>? GamepadConnected;
		/// <summary>Raised during Play when a gamepad is removed, with its slot. Handlers are cleared when Play stops.</summary>
		public static event Action<int>? GamepadDisconnected;

		/// <summary>The left stick as a vector, deadzone applied (+Y is down).</summary>
		/// <param name="gamepad">The controller slot; -1 means the first connected gamepad.</param>
		public static Vector2 GetGamepadLeftStick(int gamepad = -1) => new Vector2(GetGamepadAxis(GamepadAxis.LeftX, gamepad), GetGamepadAxis(GamepadAxis.LeftY, gamepad));
		/// <summary>The right stick as a vector, deadzone applied (+Y is down).</summary>
		/// <param name="gamepad">The controller slot; -1 means the first connected gamepad.</param>
		public static Vector2 GetGamepadRightStick(int gamepad = -1) => new Vector2(GetGamepadAxis(GamepadAxis.RightX, gamepad), GetGamepadAxis(GamepadAxis.RightY, gamepad));

		/// <summary>Whether the gamepad has adaptive triggers (a DualSense over USB).</summary>
		/// <param name="gamepad">The controller slot; -1 asks about any connected gamepad.</param>
		public static bool SupportsTriggerEffects(int gamepad = -1) => InternalCalls.Input_SupportsTriggerEffects(gamepad);
		/// <summary>
		/// Sets a DualSense adaptive-trigger effect (USB only; over Bluetooth it logs a warning and does nothing).
		/// Cleared when Play stops.
		/// </summary>
		/// <param name="trigger">Which trigger.</param>
		/// <param name="effect">The effect.</param>
		/// <param name="gamepad">The controller slot; -1 means every connected DualSense. Other pads ignore it.</param>
		public static void SetGamepadTriggerEffect(GamepadTrigger trigger, TriggerEffect effect, int gamepad = -1)
			=> InternalCalls.Input_SetGamepadTriggerEffect(trigger, &effect, gamepad);
		/// <summary>Clears every adaptive-trigger effect on every pad.</summary>
		public static void ResetGamepadTriggerEffects() => InternalCalls.Input_ResetGamepadTriggerEffects();

		/// <summary>Whether the gamepad can rumble.</summary>
		/// <param name="gamepad">The controller slot; -1 asks about any connected gamepad.</param>
		public static bool SupportsRumble(int gamepad = -1) => InternalCalls.Input_SupportsRumble(gamepad);
		/// <summary>
		/// Rumbles a gamepad. Works on Xbox pads, and PlayStation pads over USB on Windows; on Linux on any pad with
		/// force feedback. Other pads ignore it. Stopped when Play stops.
		/// </summary>
		/// <param name="low">The large low-frequency motor (a heavy thud), 0 to 1.</param>
		/// <param name="high">The small high-frequency motor (a fine buzz), 0 to 1.</param>
		/// <param name="duration">Seconds; 0 or less rumbles until <see cref="StopGamepadRumble"/>.</param>
		/// <param name="gamepad">The controller slot; -1 means every connected gamepad.</param>
		public static void RumbleGamepad(float low, float high, float duration, int gamepad = -1)
			=> InternalCalls.Input_RumbleGamepad(low, high, duration, gamepad);
		/// <summary>Stops rumbling.</summary>
		/// <param name="gamepad">The controller slot; -1 means every connected gamepad.</param>
		public static void StopGamepadRumble(int gamepad = -1) => InternalCalls.Input_StopGamepadRumble(gamepad);

		/// <summary>Whether the gamepad has a lightbar (a DualSense or DualShock 4 over USB).</summary>
		/// <param name="gamepad">The controller slot; -1 asks about any connected gamepad.</param>
		public static bool SupportsLightbar(int gamepad = -1) => InternalCalls.Input_SupportsLightbar(gamepad);
		/// <summary>Sets the lightbar color. Restored to the default when Play stops.</summary>
		/// <param name="color">RGB, components from 0 to 1.</param>
		/// <param name="gamepad">The controller slot; -1 means every pad with a lightbar.</param>
		public static void SetGamepadLightColor(Vector3 color, int gamepad = -1)
			=> InternalCalls.Input_SetGamepadLightColor(color.X, color.Y, color.Z, gamepad);
		/// <summary>Sets the DualSense player LEDs. Restored to the default when Play stops.</summary>
		/// <param name="player">0 (off) to 4.</param>
		/// <param name="gamepad">The controller slot; -1 means every DualSense.</param>
		public static void SetGamepadPlayerLights(int player, int gamepad = -1) => InternalCalls.Input_SetGamepadPlayerLights(player, gamepad);
		/// <summary>Restores every pad's lightbar and player LEDs to the default.</summary>
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

		/// <summary>The radial stick deadzone for every gamepad, as a fraction of full deflection (default 0.15).</summary>
		public static float GamepadDeadzone
		{
			get => InternalCalls.Input_GetGamepadDeadzone();
			set => InternalCalls.Input_SetGamepadDeadzone(value);
		}
	}
}
