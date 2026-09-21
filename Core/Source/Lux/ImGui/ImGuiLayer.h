// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Layer.h"

#include "ImGuiRenderer.h"

#include <functional>
#include <string>
#include <vector>

namespace Lux {

	// A connected joystick as seen by ImGui gamepad navigation.
	struct ImGuiGamepadInfo
	{
		int JoystickID = -1;     // GLFW joystick slot; not stable across sessions.
		std::string Name;
		std::string GUID;        // SDL-style GUID; stable, but shared by identical models.
		bool HasMapping = false; // Only mapped devices (known gamepad layout) can drive ImGui.
		bool HasInput = false;   // A button or stick is active right now (for telling pads apart).
	};

	class ImGuiLayer : public Layer
	{
	public:
		static ImGuiLayer* Create() { return lnew ImGuiLayer(); }

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		void Begin();
		void End();
		void SubmitDrawData();

		void SetDarkThemeColors();
		void SetDarkThemeV2Colors();

		void AllowInputEvents(bool allowEvents);

		// Joystick that drives ImGui gamepad navigation, or -1 for none (gamepad navigation off).
		// The stock GLFW backend only ever reads GLFW_JOYSTICK_1, so the layer feeds the chosen
		// device itself. Main thread only; takes effect on the next Begin().
		void SetNavGamepad(int joystickID) { m_NavGamepadID = joystickID; }
		int GetNavGamepad() const { return m_NavGamepadID; }
		static std::vector<ImGuiGamepadInfo> GetConnectedGamepads();

		// Whether ImGui clears the main window's swapchain before drawing. The editor's dockspace
		// covers the whole window, so it clears; the runtime has already rendered the game frame
		// into the swapchain and must draw its overlay on top of it.
		void SetClearMainViewport(bool clear) { m_ClearMainViewport = clear; }

		ImGuiRenderer* GetImGuiRenderer();
	public:
		ImGuiLayer() = default;
		virtual ~ImGuiLayer() = default;
	private:
		void InitPlatformInterface();
		void UpdateNavGamepad();
	private:
		std::unique_ptr<ImGuiRenderer> m_ImGuiRenderer;
		std::vector<std::function<void()>> m_PendingRenderTasks;
		bool m_ClearMainViewport = true;
		int m_NavGamepadID = -1;
		bool m_NavGamepadFed = false;
	};



}
