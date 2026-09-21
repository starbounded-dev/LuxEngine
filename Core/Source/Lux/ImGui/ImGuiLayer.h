// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Layer.h"

#include "ImGuiRenderer.h"

#include <functional>

namespace Lux {

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
	private:
		std::unique_ptr<ImGuiRenderer> m_ImGuiRenderer;
		std::vector<std::function<void()>> m_PendingRenderTasks;
		bool m_ClearMainViewport = true;
	};



}
