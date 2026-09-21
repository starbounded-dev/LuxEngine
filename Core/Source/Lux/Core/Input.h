// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "KeyCodes.h"

#include <array>
#include <chrono>
#include <map>
#include <string_view>
#include <vector>

namespace Lux {

	struct ControllerButtonData
	{
		int Button;
		KeyState State = KeyState::None;
		KeyState OldState = KeyState::None;
	};

	// Standard gamepad layout (SDL mapping), identical on every mapped controller. Face buttons are
	// named by position: South is Xbox A / PlayStation Cross. Values match GLFW_GAMEPAD_BUTTON_*
	// and the C# Lux.GamepadButton enum.
	enum class GamepadButton : int32_t
	{
		South = 0, East, West, North,
		LeftBumper, RightBumper,
		Back, Start, Guide,
		LeftStick, RightStick,
		DPadUp, DPadRight, DPadDown, DPadLeft,

		Count
	};

	// Sticks are -1..1 (Y is +1 down, as GLFW reports it); triggers are 0..1. Values match
	// GLFW_GAMEPAD_AXIS_* and the C# Lux.GamepadAxis enum.
	enum class GamepadAxis : int32_t
	{
		LeftX = 0, LeftY, RightX, RightY,
		LeftTrigger, RightTrigger,

		Count
	};

	enum class GamepadTrigger : int32_t { Left = 0, Right = 1 };

	enum class TriggerEffectType : int32_t { Off = 0, Resistance, Weapon, Vibration };

	// DualSense adaptive-trigger effect. Positions are trigger-travel zones from 0 (at rest) to 9
	// (fully pulled). Mirrors the C# Lux.TriggerEffect struct field for field.
	//   Resistance: constant resistance from Start onward, at Strength (1..8).
	//   Weapon:     resistance from Start (2..7) that gives way with a click at End (Start+1..8).
	//   Vibration:  vibrates from Start onward at Strength (amplitude 1..8) and Frequency (1..255 Hz).
	struct TriggerEffect
	{
		TriggerEffectType Type = TriggerEffectType::Off;
		int32_t Start = 0;
		int32_t End = 0;
		int32_t Strength = 0;
		int32_t Frequency = 0;

		bool operator==(const TriggerEffect&) const = default;
	};

	struct GamepadState
	{
		std::array<bool, (size_t)GamepadButton::Count> ButtonDown{};
		std::array<bool, (size_t)GamepadButton::Count> PreviousButtonDown{};
		std::array<float, (size_t)GamepadAxis::Count> Axes{}; // Deadzone already applied.
	};

	// Controller families with engine output support (rumble / adaptive triggers), from the GUID.
	enum class GamepadFamily : uint8_t { Other, DualSense, DualShock4, Xbox };

	struct Controller
	{
		int ID;
		std::string Name;
		std::map<int, bool> ButtonDown;
		std::map<int, ControllerButtonData> ButtonStates;
		std::map<int, float> AxisStates;
		std::map<int, float> DeadZones;
		std::map<int, uint8_t> HatStates;

		// Standard-layout view, only valid when IsGamepad (the device has a gamepad mapping).
		bool IsGamepad = false;
		GamepadState Gamepad;

		std::string GUID; // SDL-style GUID as GLFW reports it.
		GamepadFamily Family = GamepadFamily::Other;
	};

	struct KeyData
	{
		KeyCode Key;
		KeyState State = KeyState::None;
		KeyState OldState = KeyState::None;
	};

	struct ButtonData
	{
		MouseButton Button;
		KeyState State = KeyState::None;
		KeyState OldState = KeyState::None;
	};


	class Input
	{
	public:
		static void Update();

		static bool IsKeyPressed(KeyCode keycode);
		static bool IsKeyHeld(KeyCode keycode);
		static bool IsKeyDown(KeyCode keycode);
		static bool IsKeyReleased(KeyCode keycode);

		static bool IsMouseButtonPressed(MouseButton button);
		static bool IsMouseButtonHeld(MouseButton button);
		static bool IsMouseButtonDown(MouseButton button);
		static bool IsMouseButtonReleased(MouseButton button);
		static float GetMouseX();
		static float GetMouseY();
		static std::pair<float, float> GetMousePosition();
		static void SetMousePosition(float x, float y);

		static void SetCursorMode(CursorMode mode);
		static CursorMode GetCursorMode();

		// Controllers
		static bool IsControllerPresent(int id);
		static std::vector<int> GetConnectedControllerIDs();
		static const Controller* GetController(int id);
		static std::string_view GetControllerName(int id);

		static bool IsControllerButtonPressed(int controllerID, int button);
		static bool IsControllerButtonHeld(int controllerID, int button);
		static bool IsControllerButtonDown(int controllerID, int button);
		static bool IsControllerButtonReleased(int controllerID, int button);

		static float GetControllerAxis(int controllerID, int axis);
		static uint8_t GetControllerHat(int controllerID, int hat);

		static float GetControllerDeadzone(int controllerID, int axis);
		static void SetControllerDeadzone(int controllerID, int axis, float deadzone);

		static const std::map<int, Controller>& GetControllers() { return s_Controllers; }

		// Gamepads: the standard layout above, the same on every mapped controller. An id < 0 means
		// the first connected gamepad. Raw GetController* calls above expose device-specific indices.
		static bool IsGamepadConnected(int id = -1);
		static std::string_view GetGamepadName(int id = -1);
		static bool IsGamepadButtonDown(GamepadButton button, int id = -1);
		static bool IsGamepadButtonPressed(GamepadButton button, int id = -1);  // Went down this frame.
		static bool IsGamepadButtonReleased(GamepadButton button, int id = -1); // Went up this frame.
		static float GetGamepadAxis(GamepadAxis axis, int id = -1);

		// Radial deadzone for sticks and triggers, as a fraction of full deflection (default 0.15).
		static float GetGamepadDeadzone() { return s_GamepadDeadzone; }
		static void SetGamepadDeadzone(float deadzone);

		// DualSense adaptive triggers over USB (see DualSense.h). id < 0 targets every connected
		// DualSense; id >= 0 is a no-op unless that slot is a DualSense. Effects are sent on the
		// next Input::Update and cleared when a scene stops playing.
		static bool SupportsTriggerEffects(int id = -1);
		static void SetGamepadTriggerEffect(GamepadTrigger trigger, const TriggerEffect& effect, int id = -1);
		static void ResetGamepadTriggerEffects();

		// Rumble. low = large (low-frequency) motor, high = small (high-frequency) motor, both 0..1.
		// durationSeconds <= 0 rumbles until StopGamepadRumble. id < 0 targets every connected
		// gamepad. Backends: DualSense / DualShock 4 over USB HID and Xbox via XInput on Windows;
		// evdev force feedback for any supporting pad on Linux. Unsupported pads ignore it.
		static bool SupportsRumble(int id = -1);
		static void RumbleGamepad(float low, float high, float durationSeconds, int id = -1);
		static void StopGamepadRumble(int id = -1);

		// Stops rumble and trigger effects on the hardware; call once before exit.
		static void ShutdownGamepadOutput();

		// Internal use only...
		static void TransitionPressedKeys();
		static void TransitionPressedButtons();
		static void UpdateKeyState(KeyCode key, KeyState newState);
		static void UpdateButtonState(MouseButton button, KeyState newState);
		static void UpdateControllerButtonState(int controller, int button, KeyState newState);
		static void ClearReleasedKeys();
	private:
		inline static std::map<KeyCode, KeyData> s_KeyData;
		inline static std::map<MouseButton, ButtonData> s_MouseData;
		inline static std::map<int, Controller> s_Controllers;
		inline static float s_GamepadDeadzone = 0.15f;

		struct RumbleState
		{
			float Low = 0.0f;
			float High = 0.0f;
			std::chrono::steady_clock::time_point End;
		};
		inline static std::map<int, RumbleState> s_Rumble;                       // Requested, per slot.
		inline static std::map<int, std::pair<float, float>> s_AppliedRumble;    // Last sent to PlatformRumble.
		inline static uint32_t s_ConnectedControllerMask = 0;

		static const Controller* FindGamepad(int id);
		// Expires rumble, routes it to each backend, and flushes DualSense / DualShock 4 output.
		static void UpdateGamepadOutput();
	};

}
