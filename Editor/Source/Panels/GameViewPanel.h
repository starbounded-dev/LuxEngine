// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Project/Project.h"

#include <glm/glm.hpp>

namespace Lux {

	class Viewport;

	// The scene through its primary camera, as the player sees it - the counterpart of the Scene
	// View, which always shows the editor camera. It renders with a SceneRenderer of its own, created
	// the first time the panel is shown, and renders only while it is the visible tab.
	//
	// Main thread. OnImGuiRender runs in the UI pass; EditorLayer::OnUpdate calls PrepareFrame before
	// the scene update and Render after it, so the view shows this frame's state.
	class GameViewPanel : public EditorPanel
	{
	public:
		GameViewPanel();
		~GameViewPanel() override;

		void OnImGuiRender(bool& isOpen) override;
		void SetSceneContext(const Ref<Scene>& context) override;

		// Creates the renderer on first use, follows the project's renderer settings and resizes to
		// the panel. Returns false while the panel is closed or hidden behind another tab.
		bool PrepareFrame();
		// Renders the scene through its primary camera. Only after PrepareFrame returned true.
		void Render();

		// Render size of the open Game View, which the scene cameras take their aspect from; zero
		// while the panel is closed or has never been shown.
		glm::uvec2 GetCameraViewportSize() const;

		// Brings the panel's tab to the front on its next draw.
		void RequestFocus() { m_FocusRequested = true; }

	private:
		void InitRenderer();
		void DrawGameImage();
		void DrawNoCameraMessage() const;

	private:
		Ref<Scene> m_Scene;
		Ref<Viewport> m_Viewport;
		bool m_RendererCreated = false;
		ProjectSceneRendererSettings m_AppliedRendererSettings;

		// OnImGuiRender only runs while the panel is open, so "open" is "drawn this frame".
		bool m_DrawnThisFrame = false;
		bool m_Open = false;
		bool m_FocusRequested = false;
		bool m_ShowAccessibilityMenu = false;
	};

}
