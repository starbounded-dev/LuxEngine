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

		// Frosted-glass backdrops: popups, menus and tooltips, and the windows that ask for one
		// (RequestGlassBackdrop), draw their translucent background over a blur of what is behind
		// them; popup backgrounds are made translucent for it. Main thread; takes effect on the next
		// Begin(). Off unless an application turns it on (the editor preference "Glass Overlays").
		void SetGlassBackdrops(bool enabled) { m_GlassBackdrops = enabled; }
		bool GetGlassBackdrops() const { return m_GlassBackdrops; }
		// Main thread, between a window's ImGui::Begin and End: a glass backdrop for that window this
		// frame, when they are on. For overlays with a translucent background.
		static void RequestGlassBackdrop();
		// Main thread: replaces the window and docking settings with `ini` (imgui.ini text) just
		// before the next frame starts, which is when ImGui can rebuild the dock tree. Windows the
		// text does not mention keep their place.
		static void QueueIniSettingsLoad(std::string ini);

		ImGuiRenderer* GetImGuiRenderer();
	public:
		ImGuiLayer() = default;
		virtual ~ImGuiLayer() = default;
	private:
		void InitPlatformInterface();
		void UpdateNavGamepad();
		// After ImGui::EndFrame, before ImGui::Render: the glass backdrop command first in each
		// qualifying window's draw list.
		void InsertGlassBackdrops();
	private:
		std::unique_ptr<ImGuiRenderer> m_ImGuiRenderer;
		std::vector<std::function<void()>> m_PendingRenderTasks;
		bool m_ClearMainViewport = true;
		bool m_GlassBackdrops = false;
		// The popup background alpha the frame replaced for glass, restored after it, and what it
		// replaced it with (a theme change in between is kept).
		bool m_PopupBgDimmed = false;
		float m_PopupBgAlpha = 1.0f;
		float m_DimmedPopupBgAlpha = 1.0f;
		int m_NavGamepadID = -1;
		bool m_NavGamepadFed = false;
	};



}
