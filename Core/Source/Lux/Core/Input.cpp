// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Core/Input.h"
#include "Window.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/DualSense.h"
#include "Lux/Core/DualShock4.h"
#include "Lux/Core/GamepadRumble.h"

#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
//#include "Lux/ImGui/PropertyGrid.h"

#include <GLFW/glfw3.h>
#include <imgui/imgui_internal.h>

namespace Lux {

	static_assert((int)GamepadButton::South == GLFW_GAMEPAD_BUTTON_A && (int)GamepadButton::DPadLeft == GLFW_GAMEPAD_BUTTON_DPAD_LEFT
		&& (int)GamepadButton::Count == GLFW_GAMEPAD_BUTTON_LAST + 1, "GamepadButton must mirror GLFW_GAMEPAD_BUTTON_*");
	static_assert((int)GamepadAxis::LeftX == GLFW_GAMEPAD_AXIS_LEFT_X && (int)GamepadAxis::RightTrigger == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER
		&& (int)GamepadAxis::Count == GLFW_GAMEPAD_AXIS_LAST + 1, "GamepadAxis must mirror GLFW_GAMEPAD_AXIS_*");

	namespace {

		// Scaled radial deadzone: the stick reads 0 inside the deadzone and ramps smoothly from 0 to
		// 1 outside it, without the axis-snapping a per-axis deadzone causes on diagonals.
		void ApplyStickDeadzone(float& x, float& y, float deadzone)
		{
			const float magnitude = std::sqrt(x * x + y * y);
			if (magnitude <= deadzone)
			{
				x = y = 0.0f;
				return;
			}

			const float scale = std::min((magnitude - deadzone) / (1.0f - deadzone), 1.0f) / magnitude;
			x *= scale;
			y *= scale;
		}

		// GLFW triggers rest at -1 and reach +1 fully pulled; remap to 0..1 and apply the deadzone.
		float RemapTrigger(float value, float deadzone)
		{
			const float pulled = (value + 1.0f) * 0.5f;
			return pulled <= deadzone ? 0.0f : std::min((pulled - deadzone) / (1.0f - deadzone), 1.0f);
		}

		void UpdateGamepadState(Controller& controller, float deadzone)
		{
			GamepadState& gamepad = controller.Gamepad;
			gamepad.PreviousButtonDown = gamepad.ButtonDown;

			GLFWgamepadstate state;
			controller.IsGamepad = glfwJoystickIsGamepad(controller.ID) == GLFW_TRUE && glfwGetGamepadState(controller.ID, &state) == GLFW_TRUE;
			if (!controller.IsGamepad)
			{
				gamepad = {};
				return;
			}

			for (size_t i = 0; i < gamepad.ButtonDown.size(); i++)
				gamepad.ButtonDown[i] = state.buttons[i] == GLFW_PRESS;

			float leftX = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X], leftY = state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
			float rightX = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], rightY = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
			ApplyStickDeadzone(leftX, leftY, deadzone);
			ApplyStickDeadzone(rightX, rightY, deadzone);

			gamepad.Axes[(size_t)GamepadAxis::LeftX] = leftX;
			gamepad.Axes[(size_t)GamepadAxis::LeftY] = leftY;
			gamepad.Axes[(size_t)GamepadAxis::RightX] = rightX;
			gamepad.Axes[(size_t)GamepadAxis::RightY] = rightY;
			gamepad.Axes[(size_t)GamepadAxis::LeftTrigger] = RemapTrigger(state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER], deadzone);
			gamepad.Axes[(size_t)GamepadAxis::RightTrigger] = RemapTrigger(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER], deadzone);
		}

		GamepadFamily DetectGamepadFamily(std::string_view guid)
		{
			// GLFW's Windows XInput GUIDs start with "xinput" in hex; everything else is SDL-style,
			// with the little-endian vendor at hex chars 8-11 and product at 16-19.
			if (guid.starts_with("78696e707574"))
				return GamepadFamily::Xbox;

			if (guid.size() < 20)
				return GamepadFamily::Other;

			const std::string_view vendor = guid.substr(8, 4);
			const std::string_view product = guid.substr(16, 4);
			if (vendor == "4c05")
			{
				if (product == "e60c" || product == "f20d")
					return GamepadFamily::DualSense;
				if (product == "c405" || product == "cc09" || product == "a00b")
					return GamepadFamily::DualShock4;
			}
			if (vendor == "5e04")
				return GamepadFamily::Xbox;

			return GamepadFamily::Other;
		}

		GamepadType DetectGamepadType(const Controller& controller)
		{
			switch (controller.Family)
			{
				case GamepadFamily::Xbox:       return GamepadType::Xbox;
				case GamepadFamily::DualSense:
				case GamepadFamily::DualShock4: return GamepadType::PlayStation;
				case GamepadFamily::Other:      break;
			}

			if (controller.GUID.size() >= 20)
			{
				const std::string_view vendor = std::string_view(controller.GUID).substr(8, 4);
				if (vendor == "4c05") return GamepadType::PlayStation; // Sony
				if (vendor == "7e05") return GamepadType::Nintendo;    // Nintendo
			}

			// Third-party pads: the mapping name (or device name) usually says which layout they copy.
			const char* mappingName = controller.IsGamepad ? glfwGetGamepadName(controller.ID) : nullptr;
			std::string name = mappingName ? mappingName : controller.Name;
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });

			auto contains = [&name](std::string_view word) { return name.find(word) != std::string::npos; };
			if (contains("xbox") || contains("xinput"))
				return GamepadType::Xbox;
			if (contains("ps3") || contains("ps4") || contains("ps5") || contains("playstation") || contains("dualshock") || contains("dualsense"))
				return GamepadType::PlayStation;
			if (contains("nintendo") || contains("switch") || contains("joy-con") || contains("joycon"))
				return GamepadType::Nintendo;

			return GamepadType::Unknown;
		}

	}

	void Input::Update()
	{
		// Cleanup disconnected controller
		for (auto it = s_Controllers.begin(); it != s_Controllers.end(); )
		{
			int id = it->first;
			if (glfwJoystickPresent(id) != GLFW_TRUE)
				it = s_Controllers.erase(it);
			else
				it++;
		}

		// Update controllers
		for (int id = GLFW_JOYSTICK_1; id <= GLFW_JOYSTICK_LAST; id++)
		{
			if (glfwJoystickPresent(id) == GLFW_TRUE)
			{
				Controller& controller = s_Controllers[id];
				controller.ID = id;
				controller.Name = glfwGetJoystickName(id);

				int buttonCount;
				const unsigned char* buttons = glfwGetJoystickButtons(id, &buttonCount);
				for (int i = 0; i < buttonCount; i++)
				{
					if (buttons[i] == GLFW_PRESS && !controller.ButtonDown[i])
						controller.ButtonStates[i].State = KeyState::Pressed;
					else if (buttons[i] == GLFW_RELEASE && controller.ButtonDown[i])
						controller.ButtonStates[i].State = KeyState::Released;

					controller.ButtonDown[i] = buttons[i] == GLFW_PRESS;
				}

				int axisCount;
				const float* axes = glfwGetJoystickAxes(id, &axisCount);
				for (int i = 0; i < axisCount; i++)
					controller.AxisStates[i] = abs(axes[i]) > controller.DeadZones[i] ? axes[i] : 0.0f;

				int hatCount;
				const unsigned char* hats = glfwGetJoystickHats(id, &hatCount);
				for (int i = 0; i < hatCount; i++)
					controller.HatStates[i] = hats[i];

				UpdateGamepadState(controller, s_GamepadDeadzone);

				const char* guid = glfwGetJoystickGUID(id);
				controller.GUID = guid ? guid : "";
				controller.Family = DetectGamepadFamily(controller.GUID);
				controller.Type = DetectGamepadType(controller);
			}
		}

		UpdateGamepadOutput();
	}

	void Input::UpdateGamepadOutput()
	{
		uint32_t connectedMask = 0;
		for (const auto& [id, controller] : s_Controllers)
			connectedMask |= 1u << id;

		if (connectedMask != s_ConnectedControllerMask)
		{
			s_ConnectedControllerMask = connectedMask;
			PlatformRumble::OnControllersChanged();
			s_AppliedRumble.clear(); // Re-send everything to the (possibly new) devices.
		}

		// Expire timed rumble and forget disconnected slots.
		const auto now = std::chrono::steady_clock::now();
		std::erase_if(s_Rumble, [now](const auto& entry)
		{
			return entry.second.End <= now || !IsControllerPresent(entry.first);
		});

		float dualSenseLow = 0.0f, dualSenseHigh = 0.0f;
		float dualShock4Low = 0.0f, dualShock4High = 0.0f;
		uint32_t dualSenseCount = 0, dualShock4Count = 0;

		for (const auto& [id, controller] : s_Controllers)
		{
			const auto rumble = s_Rumble.find(id);
			const float low = rumble != s_Rumble.end() ? rumble->second.Low : 0.0f;
			const float high = rumble != s_Rumble.end() ? rumble->second.High : 0.0f;

			if (controller.Family == GamepadFamily::DualSense)
				dualSenseCount++;
			else if (controller.Family == GamepadFamily::DualShock4)
				dualShock4Count++;

#ifdef LUX_PLATFORM_WINDOWS
			// PlayStation pads rumble through their HID report, which reaches every pad of that
			// model at once, so the strongest request among them wins.
			if (controller.Family == GamepadFamily::DualSense)
			{
				dualSenseLow = std::max(dualSenseLow, low);
				dualSenseHigh = std::max(dualSenseHigh, high);
				continue;
			}
			if (controller.Family == GamepadFamily::DualShock4)
			{
				dualShock4Low = std::max(dualShock4Low, low);
				dualShock4High = std::max(dualShock4High, high);
				continue;
			}
#endif

			const auto applied = s_AppliedRumble.find(id);
			if (applied != s_AppliedRumble.end() && applied->second.first == low && applied->second.second == high)
				continue;

			// Nothing to stop on a pad that never rumbled; skip the backend call.
			if (applied == s_AppliedRumble.end() && low == 0.0f && high == 0.0f)
				continue;

			PlatformRumble::Set(controller, low, high);
			s_AppliedRumble[id] = { low, high };
		}

		std::erase_if(s_AppliedRumble, [](const auto& entry) { return !IsControllerPresent(entry.first); });

		DualSense::SetRumble(dualSenseLow, dualSenseHigh);
		DualShock4::SetRumble(dualShock4Low, dualShock4High);
		DualSense::Update(dualSenseCount);
		DualShock4::Update(dualShock4Count);
	}

	bool Input::IsKeyPressed(KeyCode key)
	{
		return s_KeyData.find(key) != s_KeyData.end() && s_KeyData[key].State == KeyState::Pressed;
	}

	bool Input::IsKeyHeld(KeyCode key)
	{
		return s_KeyData.find(key) != s_KeyData.end() && s_KeyData[key].State == KeyState::Held;
	}

	bool Input::IsKeyDown(KeyCode keycode)
	{
		bool enableImGui = Application::Get().GetSpecification().EnableImGui;
		if (!enableImGui)
		{
			auto& window = static_cast<Window&>(Application::Get().GetWindow());
			auto state = glfwGetKey(static_cast<GLFWwindow*>(window.GetNativeWindow()), static_cast<int32_t>(keycode));
			return state == GLFW_PRESS || state == GLFW_REPEAT;
		}

		auto& window = static_cast<Window&>(Application::Get().GetWindow());
		GLFWwindow* win = static_cast<GLFWwindow*>(window.GetNativeWindow());
		ImGuiContext* context = ImGui::GetCurrentContext();
		bool pressed = false;
		for (ImGuiViewport* viewport : context->Viewports)
		{
			if (!viewport->PlatformUserData)
				continue;

			GLFWwindow* windowHandle = *(GLFWwindow**)viewport->PlatformUserData; // First member is GLFWwindow
			if (!windowHandle)
				continue;
			auto state = glfwGetKey(windowHandle, static_cast<int32_t>(keycode));
			if (state == GLFW_PRESS || state == GLFW_REPEAT)
			{
				pressed = true;
				break;
			}
		}

		return pressed;
	}

	bool Input::IsKeyReleased(KeyCode key)
	{
		return s_KeyData.find(key) != s_KeyData.end() && s_KeyData[key].State == KeyState::Released;
	}

	bool Input::IsMouseButtonPressed(MouseButton button)
	{
		return s_MouseData.find(button) != s_MouseData.end() && s_MouseData[button].State == KeyState::Pressed;
	}

	bool Input::IsMouseButtonHeld(MouseButton button)
	{
		return s_MouseData.find(button) != s_MouseData.end() && s_MouseData[button].State == KeyState::Held;
	}

	bool Input::IsMouseButtonDown(MouseButton button)
	{
		bool enableImGui = Application::Get().GetSpecification().EnableImGui;
		if (!enableImGui)
		{
			auto& window = static_cast<Window&>(Application::Get().GetWindow());
			auto state = glfwGetMouseButton(static_cast<GLFWwindow*>(window.GetNativeWindow()), static_cast<int32_t>(button));
			return state == GLFW_PRESS;
		}

		ImGuiContext* context = ImGui::GetCurrentContext();
		bool pressed = false;
		for (ImGuiViewport* viewport : context->Viewports)
		{
			if (!viewport->PlatformUserData)
				continue;

			GLFWwindow* windowHandle = *(GLFWwindow**)viewport->PlatformUserData; // First member is GLFWwindow
			if (!windowHandle)
				continue;

			auto state = glfwGetMouseButton(static_cast<GLFWwindow*>(windowHandle), static_cast<int32_t>(button));
			if (state == GLFW_PRESS || state == GLFW_REPEAT)
			{
				pressed = true;
				break;
			}
		}
		return pressed;
	}

	bool Input::IsMouseButtonReleased(MouseButton button)
	{
		return s_MouseData.find(button) != s_MouseData.end() && s_MouseData[button].State == KeyState::Released;
	}

	float Input::GetMouseX()
	{
		auto [x, y] = GetMousePosition();
		return (float)x;
	}

	float Input::GetMouseY()
	{
		auto [x, y] = GetMousePosition();
		return (float)y;
	}

	std::pair<float, float> Input::GetMousePosition()
	{
		auto& window = static_cast<Window&>(Application::Get().GetWindow());

		double x, y;
		glfwGetCursorPos(static_cast<GLFWwindow*>(window.GetNativeWindow()), &x, &y);
		return { (float)x, (float)y };
	}

	void Input::SetMousePosition(float x, float y)
	{
		auto& window = static_cast<Window&>(Application::Get().GetWindow());
		glfwSetCursorPos(static_cast<GLFWwindow*>(window.GetNativeWindow()), x, y);
	}

	void Input::SetCursorMode(CursorMode mode)
	{
		auto& window = static_cast<Window&>(Application::Get().GetWindow());
		glfwSetInputMode(static_cast<GLFWwindow*>(window.GetNativeWindow()), GLFW_CURSOR, GLFW_CURSOR_NORMAL + (int)mode);

		// TODO: Re-enable when UI namespace is available
		// if (Application::Get().GetSpecification().EnableImGui)
		// 	UI::SetInputEnabled(mode == CursorMode::Normal);
	}

	CursorMode Input::GetCursorMode()
	{
		auto& window = static_cast<Window&>(Application::Get().GetWindow());
		return (CursorMode)(glfwGetInputMode(static_cast<GLFWwindow*>(window.GetNativeWindow()), GLFW_CURSOR) - GLFW_CURSOR_NORMAL);
	}

	bool Input::IsControllerPresent(int id)
	{
		return s_Controllers.find(id) != s_Controllers.end();
	}

	std::vector<int> Input::GetConnectedControllerIDs()
	{
		std::vector<int> ids;
		ids.reserve(s_Controllers.size());
		for (auto [id, controller] : s_Controllers)
			ids.emplace_back(id);

		return ids;
	}

	const Controller* Input::GetController(int id)
	{
		if (!Input::IsControllerPresent(id))
			return nullptr;

		return &s_Controllers.at(id);
	}

	std::string_view Input::GetControllerName(int id)
	{
		if (!Input::IsControllerPresent(id))
			return {};

		return s_Controllers.at(id).Name;
	}

	bool Input::IsControllerButtonPressed(int controllerID, int button)
	{
		if (!Input::IsControllerPresent(controllerID))
			return false;

		auto& contoller = s_Controllers.at(controllerID);
		return contoller.ButtonStates.find(button) != contoller.ButtonStates.end() && contoller.ButtonStates[button].State == KeyState::Pressed;
	}

	bool Input::IsControllerButtonHeld(int controllerID, int button)
	{
		if (!Input::IsControllerPresent(controllerID))
			return false;

		auto& contoller = s_Controllers.at(controllerID);
		return contoller.ButtonStates.find(button) != contoller.ButtonStates.end() && contoller.ButtonStates[button].State == KeyState::Held;
	}

	bool Input::IsControllerButtonDown(int controllerID, int button)
	{
		if (!Input::IsControllerPresent(controllerID))
			return false;

		const Controller& controller = s_Controllers.at(controllerID);
		if (controller.ButtonDown.find(button) == controller.ButtonDown.end())
			return false;

		return controller.ButtonDown.at(button);
	}

	bool Input::IsControllerButtonReleased(int controllerID, int button)
	{
		if (!Input::IsControllerPresent(controllerID))
			return true;

		auto& contoller = s_Controllers.at(controllerID);
		return contoller.ButtonStates.find(button) != contoller.ButtonStates.end() && contoller.ButtonStates[button].State == KeyState::Released;
	}

	float Input::GetControllerAxis(int controllerID, int axis)
	{
		if (!Input::IsControllerPresent(controllerID))
			return 0.0f;

		const Controller& controller = s_Controllers.at(controllerID);
		if (controller.AxisStates.find(axis) == controller.AxisStates.end())
			return 0.0f;

		return controller.AxisStates.at(axis);
	}

	uint8_t Input::GetControllerHat(int controllerID, int hat)
	{
		if (!Input::IsControllerPresent(controllerID))
			return 0;

		const Controller& controller = s_Controllers.at(controllerID);
		if (controller.HatStates.find(hat) == controller.HatStates.end())
			return 0;

		return controller.HatStates.at(hat);
	}

	float Input::GetControllerDeadzone(int controllerID, int axis)
	{
		if (!Input::IsControllerPresent(controllerID))
			return 0.0f;

		const Controller& controller = s_Controllers.at(controllerID);
		return controller.DeadZones.at(axis);
	}

	void Input::SetControllerDeadzone(int controllerID, int axis, float deadzone)
	{
		if (!Input::IsControllerPresent(controllerID))
			return;

		Controller& controller = s_Controllers.at(controllerID);
		controller.DeadZones[axis] = deadzone;
	}

	const Controller* Input::FindGamepad(int id)
	{
		if (id >= 0)
		{
			const Controller* controller = GetController(id);
			return controller && controller->IsGamepad ? controller : nullptr;
		}

		for (const auto& [_, controller] : s_Controllers)
		{
			if (controller.IsGamepad)
				return &controller;
		}
		return nullptr;
	}

	bool Input::IsGamepadConnected(int id)
	{
		return FindGamepad(id) != nullptr;
	}

	std::string_view Input::GetGamepadName(int id)
	{
		const Controller* controller = FindGamepad(id);
		return controller ? std::string_view(controller->Name) : std::string_view{};
	}

	bool Input::IsGamepadButtonDown(GamepadButton button, int id)
	{
		const Controller* controller = FindGamepad(id);
		if (!controller || button < GamepadButton::South || button >= GamepadButton::Count)
			return false;

		return controller->Gamepad.ButtonDown[(size_t)button];
	}

	bool Input::IsGamepadButtonPressed(GamepadButton button, int id)
	{
		const Controller* controller = FindGamepad(id);
		if (!controller || button < GamepadButton::South || button >= GamepadButton::Count)
			return false;

		return controller->Gamepad.ButtonDown[(size_t)button] && !controller->Gamepad.PreviousButtonDown[(size_t)button];
	}

	bool Input::IsGamepadButtonReleased(GamepadButton button, int id)
	{
		const Controller* controller = FindGamepad(id);
		if (!controller || button < GamepadButton::South || button >= GamepadButton::Count)
			return false;

		return !controller->Gamepad.ButtonDown[(size_t)button] && controller->Gamepad.PreviousButtonDown[(size_t)button];
	}

	float Input::GetGamepadAxis(GamepadAxis axis, int id)
	{
		const Controller* controller = FindGamepad(id);
		if (!controller || axis < GamepadAxis::LeftX || axis >= GamepadAxis::Count)
			return 0.0f;

		return controller->Gamepad.Axes[(size_t)axis];
	}

	GamepadType Input::GetGamepadType(int id)
	{
		const Controller* controller = FindGamepad(id);
		return controller ? controller->Type : GamepadType::Unknown;
	}

	bool Input::LoadGamepadMappings(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
			return false;

		std::stringstream contents;
		contents << stream.rdbuf();
		if (glfwUpdateGamepadMappings(contents.str().c_str()) != GLFW_TRUE)
		{
			LUX_CORE_WARN("Could not load gamepad mappings from '{}' (GLFW rejected the file).", path.string());
			return false;
		}

		LUX_CORE_INFO("Loaded gamepad mappings from '{}'.", path.string());
		return true;
	}

	void Input::SetGamepadDeadzone(float deadzone)
	{
		s_GamepadDeadzone = std::clamp(deadzone, 0.0f, 0.95f);
	}

	bool Input::SupportsTriggerEffects(int id)
	{
		if (id >= 0)
		{
			const Controller* controller = GetController(id);
			return controller && controller->Family == GamepadFamily::DualSense;
		}

		return std::any_of(s_Controllers.begin(), s_Controllers.end(),
			[](const auto& entry) { return entry.second.Family == GamepadFamily::DualSense; });
	}

	void Input::SetGamepadTriggerEffect(GamepadTrigger trigger, const TriggerEffect& effect, int id)
	{
		// Only a DualSense slot (or "any") is accepted; the effect then reaches every connected
		// DualSense, because GLFW slots cannot be matched to HID devices (see DualSense.h).
		if (id >= 0 && !SupportsTriggerEffects(id))
			return;

		DualSense::SetTriggerEffect(trigger, effect);
	}

	void Input::ResetGamepadTriggerEffects()
	{
		DualSense::ResetTriggerEffects();
	}

	bool Input::SupportsRumble(int id)
	{
		auto supports = [](const Controller& controller)
		{
#ifdef LUX_PLATFORM_WINDOWS
			if (controller.Family == GamepadFamily::DualSense || controller.Family == GamepadFamily::DualShock4)
				return true;
#endif
			return PlatformRumble::Supports(controller);
		};

		if (id >= 0)
		{
			const Controller* controller = GetController(id);
			return controller && supports(*controller);
		}

		return std::any_of(s_Controllers.begin(), s_Controllers.end(), [&supports](const auto& entry) { return supports(entry.second); });
	}

	void Input::RumbleGamepad(float low, float high, float durationSeconds, int id)
	{
		RumbleState state;
		state.Low = std::clamp(low, 0.0f, 1.0f);
		state.High = std::clamp(high, 0.0f, 1.0f);
		state.End = durationSeconds > 0.0f
			? std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(durationSeconds))
			: std::chrono::steady_clock::time_point::max();

		if (state.Low == 0.0f && state.High == 0.0f)
		{
			StopGamepadRumble(id);
			return;
		}

		if (id >= 0)
		{
			if (IsControllerPresent(id))
				s_Rumble[id] = state;
			return;
		}

		for (const auto& [controllerID, controller] : s_Controllers)
			s_Rumble[controllerID] = state;
	}

	void Input::StopGamepadRumble(int id)
	{
		if (id >= 0)
			s_Rumble.erase(id);
		else
			s_Rumble.clear();
	}

	bool Input::SupportsLightbar(int id)
	{
		auto supports = [](const Controller& controller)
		{
			return controller.Family == GamepadFamily::DualSense || controller.Family == GamepadFamily::DualShock4;
		};

		if (id >= 0)
		{
			const Controller* controller = GetController(id);
			return controller && supports(*controller);
		}

		return std::any_of(s_Controllers.begin(), s_Controllers.end(), [&supports](const auto& entry) { return supports(entry.second); });
	}

	void Input::SetGamepadLightColor(float red, float green, float blue, int id)
	{
		// HID output reaches every pad of a model, so a slot only selects which model(s) to target.
		const Controller* controller = id >= 0 ? GetController(id) : nullptr;
		if (id >= 0 && !controller)
			return;

		if (!controller || controller->Family == GamepadFamily::DualSense)
			DualSense::SetLightColor(red, green, blue);
		if (!controller || controller->Family == GamepadFamily::DualShock4)
			DualShock4::SetLightColor(red, green, blue);
	}

	void Input::SetGamepadPlayerLights(int player, int id)
	{
		if (id >= 0 && !SupportsTriggerEffects(id)) // Player LEDs are DualSense-only, like triggers.
			return;

		DualSense::SetPlayerLights(player);
	}

	void Input::ResetGamepadLights()
	{
		DualSense::ResetLights();
		DualShock4::ResetLights();
	}

	void Input::ShutdownGamepadOutput()
	{
		s_Rumble.clear();
		s_AppliedRumble.clear();
		PlatformRumble::Shutdown();
		DualSense::Shutdown();
		DualShock4::Shutdown();
	}

	void Input::TransitionPressedKeys()
	{
		for (const auto& [key, keyData] : s_KeyData)
		{
			if (keyData.State == KeyState::Pressed)
				UpdateKeyState(key, KeyState::Held);
		}

	}

	void Input::TransitionPressedButtons()
	{
		for (const auto& [button, buttonData] : s_MouseData)
		{
			if (buttonData.State == KeyState::Pressed)
				UpdateButtonState(button, KeyState::Held);
		}

		for (const auto& [id, controller] : s_Controllers)
		{
			for (const auto& [button, buttonStates] : controller.ButtonStates)
			{
				if (buttonStates.State == KeyState::Pressed)
					UpdateControllerButtonState(id, button, KeyState::Held);
			}
		}
	}

	void Input::UpdateKeyState(KeyCode key, KeyState newState)
	{
		auto& keyData = s_KeyData[key];
		keyData.Key = key;
		keyData.OldState = keyData.State;
		keyData.State = newState;
	}

	void Input::UpdateButtonState(MouseButton button, KeyState newState)
	{
		auto& mouseData = s_MouseData[button];
		mouseData.Button = button;
		mouseData.OldState = mouseData.State;
		mouseData.State = newState;
	}

	void Input::UpdateControllerButtonState(int controllerID, int button, KeyState newState)
	{
		auto& controllerButtonData = s_Controllers.at(controllerID).ButtonStates.at(button);
		controllerButtonData.Button = button;
		controllerButtonData.OldState = controllerButtonData.State;
		controllerButtonData.State = newState;
	}

	void Input::ClearReleasedKeys()
	{
		for (const auto& [key, keyData] : s_KeyData)
		{
			if (keyData.State == KeyState::Released)
				UpdateKeyState(key, KeyState::None);
		}

		for (const auto& [button, buttonData] : s_MouseData)
		{
			if (buttonData.State == KeyState::Released)
				UpdateButtonState(button, KeyState::None);
		}

		for (const auto& [id, controller] : s_Controllers)
		{
			for (const auto& [button, buttonStates] : controller.ButtonStates)
			{
				if (buttonStates.State == KeyState::Released)
					UpdateControllerButtonState(id, button, KeyState::None);
			}
		}
	}
}
