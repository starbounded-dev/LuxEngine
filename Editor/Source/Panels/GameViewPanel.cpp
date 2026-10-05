// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "GameViewPanel.h"

#include "Viewport/Viewport.h"

#include "Lux/Debug/Profiler.h"
#include "Lux/ImGui/AudioAccessibilityWidgets.h"
#include "Lux/ImGui/ImGuiCore.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Scene/Scene.h"

namespace Lux {

	namespace
	{
		constexpr const char* kNoCameraTitle = "No Camera";
		constexpr const char* kNoCameraHint = "Add a Camera component to an entity and tick Primary.";
	}

	GameViewPanel::GameViewPanel()
	{
		m_Viewport = Ref<Viewport>::Create("Game View");
	}

	GameViewPanel::~GameViewPanel()
	{
		if (m_Viewport)
			m_Viewport->Shutdown();
	}

	void GameViewPanel::SetSceneContext(const Ref<Scene>& context)
	{
		m_Scene = context;
		if (m_RendererCreated)
			m_Viewport->GetSceneRenderer()->SetScene(context);
	}

	void GameViewPanel::InitRenderer()
	{
		const glm::uvec2 size = m_Viewport->GetOutputSize();

		SceneRendererSpecification rendererSpec;
		rendererSpec.ViewportWidth = size.x;
		rendererSpec.ViewportHeight = size.y;
		// The player sees no selection outline, wireframe or debug view, so skip their targets.
		rendererSpec.EnableEditorRenderTargets = false;

		m_Viewport->InitRenderer(m_Scene, rendererSpec);
		Ref<SceneRenderer> renderer = m_Viewport->GetSceneRenderer();
		renderer->GetOptions().ShowGrid = false;
		renderer->SetDebugViewMode(SceneRenderer::DebugViewMode::Final);
		m_RendererCreated = true;

		if (Ref<Project> project = Project::GetActive())
		{
			m_AppliedRendererSettings = project->GetConfig().SceneRenderer;
			renderer->ApplyProjectSettings(m_AppliedRendererSettings);
		}
	}

	bool GameViewPanel::PrepareFrame()
	{
		m_Open = m_DrawnThisFrame;
		m_DrawnThisFrame = false;

		if (!m_Open || !m_Viewport->IsVisible() || !m_Scene)
			return false;

		// Before the first layout the panel has no size, and the renderer is sized from it.
		const glm::vec2& panelSize = m_Viewport->GetSize();
		if (panelSize.x <= 1.0f || panelSize.y <= 1.0f)
			return false;

		if (!m_RendererCreated)
			InitRenderer();

		// A preview of the shipped game, so it follows the project's renderer settings - what the
		// runtime applies - and not the Scene View's debug state. Applied on change only: applying
		// sets global shader macros.
		if (Ref<Project> project = Project::GetActive())
		{
			const ProjectSceneRendererSettings& settings = project->GetConfig().SceneRenderer;
			if (settings != m_AppliedRendererSettings)
			{
				m_AppliedRendererSettings = settings;
				m_Viewport->GetSceneRenderer()->ApplyProjectSettings(settings);
			}
		}

		return m_Viewport->SyncSize();
	}

	void GameViewPanel::Render()
	{
		LUX_PROFILE_FUNCTION("GameViewPanel::Render");

		Ref<SceneRenderer> renderer = m_Viewport->GetSceneRenderer();
		if (!m_Scene || !renderer || !renderer->IsReady())
			return;

		// Renders nothing when the scene has no primary camera; OnImGuiRender says so instead.
		m_Scene->OnRenderRuntime(renderer);
	}

	glm::uvec2 GameViewPanel::GetCameraViewportSize() const
	{
		if (!m_Open || !m_RendererCreated)
			return { 0, 0 };

		return m_Viewport->GetRenderSize();
	}

	void GameViewPanel::OnImGuiRender(bool& isOpen)
	{
		m_DrawnThisFrame = true;

		if (m_FocusRequested)
		{
			ImGui::SetNextWindowFocus();
			m_FocusRequested = false;
		}

		ImGuiEx::ScopedStyle windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		if (m_Viewport->BeginImGui(&isOpen))
		{
			if (m_Scene && m_Scene->GetPrimaryCameraEntity())
				DrawGameImage();
			else
				DrawNoCameraMessage();
		}
		m_Viewport->EndImGui();
	}

	void GameViewPanel::DrawGameImage()
	{
		Ref<Image2D> image = m_RendererCreated ? m_Viewport->GetDisplayImage() : nullptr;
		if (image)
		{
			// Letterboxed like the Scene View, so a fixed-resolution render keeps its framing.
			const glm::vec2* bounds = m_Viewport->GetBounds();
			const glm::vec2* imageBounds = m_Viewport->GetImageBounds();
			const glm::vec2 imageSize = m_Viewport->GetImageSize();
			const ImVec2 cursor = ImGui::GetCursorPos();
			ImGui::SetCursorPos(ImVec2{
				cursor.x + (imageBounds[0].x - bounds[0].x),
				cursor.y + (imageBounds[0].y - bounds[0].y) });
			ImGuiEx::Image(image, ImVec2{ imageSize.x, imageSize.y });
		}

		// Captions and the accessibility menu belong over the game, and only while it runs.
		if (m_Scene->IsRunning())
		{
			const glm::vec2* imageBounds = m_Viewport->GetImageBounds();
			ImGuiEx::AudioAccessibilityOverlay(imageBounds[0], imageBounds[1]);
			if ((m_Viewport->IsFocused() || m_ShowAccessibilityMenu) && ImGui::IsKeyPressed(ImGuiKey_F10, false))
				m_ShowAccessibilityMenu = !m_ShowAccessibilityMenu;
			ImGuiEx::AudioAccessibilityMenu(m_ShowAccessibilityMenu);
		}
	}

	void GameViewPanel::DrawNoCameraMessage() const
	{
		const glm::vec2* bounds = m_Viewport->GetBounds();
		const ImVec2 titleSize = ImGui::CalcTextSize(kNoCameraTitle);
		const ImVec2 hintSize = ImGui::CalcTextSize(kNoCameraHint);
		const float spacing = ImGui::GetStyle().ItemSpacing.y;

		const float centerX = (bounds[0].x + bounds[1].x) * 0.5f;
		const float top = (bounds[0].y + bounds[1].y) * 0.5f - (titleSize.y + spacing + hintSize.y) * 0.5f;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddText(ImVec2(centerX - titleSize.x * 0.5f, top), Colors::Theme::textBrighter, kNoCameraTitle);
		drawList->AddText(ImVec2(centerX - hintSize.x * 0.5f, top + titleSize.y + spacing), Colors::Theme::textCaption, kNoCameraHint);
	}

}
