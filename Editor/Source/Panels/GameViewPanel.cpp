// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "GameViewPanel.h"

#include "Viewport/Viewport.h"

#include "Lux/Debug/Profiler.h"
#include "Lux/Editor/EditorStack.h"
#include "Lux/Editor/SelectionManager.h"
#include "Lux/ImGui/AudioAccessibilityWidgets.h"
#include "Lux/ImGui/ImGuiCore.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/UI/UIEditor.h"

#include <algorithm>

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
		ImGui::SetNextWindowSize({800.0f, 500.0f}, ImGuiCond_FirstUseEver);
		if (m_Viewport->BeginImGui(&isOpen))
		{
			// A scene with only a canvas (a menu) has no camera and still has an image: the interface over an empty frame.
			if (m_Scene && (m_Scene->GetPrimaryCameraEntity() || !m_Scene->GetAllEntitiesWith<UICanvasComponent>().empty()))
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

			if (m_Scene && imageSize.x > 1.0f && imageSize.y > 1.0f)
			{
				if (m_Scene->IsRunning())
					FeedUIInput(imageBounds[0], imageBounds[1]);
				else
					DrawUIEditing(imageBounds[0], imageBounds[1]);
			}
		}

		// The accessibility menu belongs over the game, and only while it runs. The captions are drawn by the game UI.
		if (m_Scene->IsRunning())
		{
			if ((m_Viewport->IsFocused() || m_ShowAccessibilityMenu) && ImGui::IsKeyPressed(ImGuiKey_F10, false))
				m_ShowAccessibilityMenu = !m_ShowAccessibilityMenu;
			ImGuiEx::AudioAccessibilityMenu(m_ShowAccessibilityMenu);
		}
	}

	namespace
	{
		// The game's view is shown stretched over the image: its pixels (origin bottom left) and the image's (top left) differ
		// by a scale and a flip.
		glm::vec2 ToViewPixels(const ImVec2& point, const glm::vec2& imageMin, const glm::vec2& imageMax, const glm::vec2& view)
		{
			const glm::vec2 size = imageMax - imageMin;
			return { (point.x - imageMin.x) / size.x * view.x, (1.0f - (point.y - imageMin.y) / size.y) * view.y };
		}

		ImVec2 ToImage(const glm::vec2& pixel, const glm::vec2& imageMin, const glm::vec2& imageMax, const glm::vec2& view)
		{
			const glm::vec2 size = imageMax - imageMin;
			return { imageMin.x + pixel.x / view.x * size.x, imageMin.y + (1.0f - pixel.y / view.y) * size.y };
		}

		ImGuiMouseCursor CursorFor(UI::DragHandle handle)
		{
			switch (handle)
			{
				case UI::DragHandle::Body: return ImGuiMouseCursor_ResizeAll;
				case UI::DragHandle::Left:
				case UI::DragHandle::Right: return ImGuiMouseCursor_ResizeEW;
				case UI::DragHandle::Bottom:
				case UI::DragHandle::Top: return ImGuiMouseCursor_ResizeNS;
				case UI::DragHandle::BottomLeft:
				case UI::DragHandle::TopRight: return ImGuiMouseCursor_ResizeNESW;
				case UI::DragHandle::BottomRight:
				case UI::DragHandle::TopLeft: return ImGuiMouseCursor_ResizeNWSE;
				default: return ImGuiMouseCursor_Arrow;
			}
		}
	}

	void GameViewPanel::FeedUIInput(const glm::vec2& imageMin, const glm::vec2& imageMax)
	{
		const glm::vec2 view{ (float)m_Scene->GetViewportWidth(), (float)m_Scene->GetViewportHeight() };
		if (view.x < 1.0f || view.y < 1.0f)
			return;

		ImGuiIO& io = ImGui::GetIO();
		const bool inside = m_Viewport->IsHovered() && io.MousePos.x >= imageMin.x && io.MousePos.x <= imageMax.x
			&& io.MousePos.y >= imageMin.y && io.MousePos.y <= imageMax.y;
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && inside)
			m_UIPressedInside = true;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			m_UIPressedInside = false;

		UI::Pointer pointer;
		pointer.Position = ToViewPixels(io.MousePos, imageMin, imageMax, view);
		pointer.Present = inside || m_UIPressedInside; // a drag that left the image still reaches its slider
		pointer.Down = m_UIPressedInside;
		m_Scene->SetUIPointer(pointer);
		if (inside && io.MouseWheel != 0.0f)
			m_Scene->AddUIScroll(io.MouseWheel);

		// Keys go to the game only while this panel has the focus; the inspector's text fields keep theirs.
		const bool keyboard = m_Viewport->IsFocused();
		m_Scene->SetUIKeyboardEnabled(keyboard);
		if (keyboard)
		{
			for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
				m_Scene->AddUITypedCharacter(static_cast<char32_t>(io.InputQueueCharacters[i]));
		}
	}

	void GameViewPanel::DrawUIEditing(const glm::vec2& imageMin, const glm::vec2& imageMax)
	{
		// One undo step per finished drag.
		const auto record = [](const UI::EditOutput& result)
		{
			if (result.Ended)
				EditorStack::Get().MarkEntitiesEdited({ result.EndedEntity }, result.EndedHandle == UI::DragHandle::Body ? "Move UI Element" : "Resize UI Element");
		};

		if (m_Scene->GetAllEntitiesWith<UICanvasComponent>().empty())
		{
			record(m_UIEdit.Finish());
			return;
		}

		ImGui::SetCursorScreenPos(ImVec2{ imageMin.x + 8.0f, imageMin.y + 8.0f });
		ImGui::Checkbox("Edit UI##GameViewEditUI", &m_EditUI);
		const bool overCheckbox = ImGui::IsItemHovered();
		if (!m_EditUI)
		{
			record(m_UIEdit.Finish());
			return;
		}

		const glm::vec2 view{ (float)m_Scene->GetViewportWidth(), (float)m_Scene->GetViewportHeight() };
		if (view.x < 1.0f || view.y < 1.0f)
			return;

		const std::vector<UI::ResolvedCanvas> canvases = UI::ResolveLayout(*m_Scene, view);
		const ImVec2 mouse = ImGui::GetMousePos();

		UI::EditInput input;
		input.Point = ToViewPixels(mouse, imageMin, imageMax, view);
		input.Inside = m_Viewport->IsHovered() && !overCheckbox && mouse.x >= imageMin.x && mouse.x <= imageMax.x
			&& mouse.y >= imageMin.y && mouse.y <= imageMax.y;
		input.Down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		input.Pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		input.Grab = 8.0f * view.x / std::max(imageMax.x - imageMin.x, 1.0f); // 8 image pixels, in view pixels
		for (UUID id : SelectionManager::GetSelections(SelectionContext::Scene))
		{
			input.Selected = id;
			break;
		}

		// Arrow keys nudge the selected element by a unit (ten with Shift) while the panel has the focus.
		if (!m_UIEdit.IsDragging() && input.Selected && m_Viewport->IsFocused())
		{
			const float step = ImGui::GetIO().KeyShift ? 10.0f : 1.0f;
			glm::vec2 nudge{ 0.0f };
			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudge.x -= step;
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudge.x += step;
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudge.y -= step;
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudge.y += step;
			if (nudge != glm::vec2(0.0f) && UI::EditSession::Nudge(m_Scene->GetEntityByUUID(input.Selected), nudge))
				EditorStack::Get().MarkEntitiesEdited({ input.Selected }, "Move UI Element");
		}

		const UI::EditOutput result = m_UIEdit.Update(canvases, input);
		record(result);
		if (result.Select)
		{
			SelectionManager::DeselectAll(SelectionContext::Scene);
			SelectionManager::Select(SelectionContext::Scene, result.Select);
		}
		if (result.Hover != UI::DragHandle::None)
			ImGui::SetMouseCursor(CursorFor(result.Hover));

		// The overlay: the hovered element faintly, the selected one with its handles.
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const auto rectOnScreen = [&](const UI::Rect& rect, ImVec2& topLeft, ImVec2& bottomRight)
		{
			topLeft = ToImage({ rect.Min.x, rect.Max.y }, imageMin, imageMax, view);
			bottomRight = ToImage({ rect.Max.x, rect.Min.y }, imageMin, imageMax, view);
		};
		draw->PushClipRect(ImVec2{ imageMin.x, imageMin.y }, ImVec2{ imageMax.x, imageMax.y }, true);
		if (result.Under && result.Under != result.Selected)
		{
			ImVec2 a, b;
			rectOnScreen(result.Under->ScreenRect, a, b);
			draw->AddRect(a, b, IM_COL32(255, 255, 255, 90), 0.0f, 0, 1.0f);
		}
		if (result.Selected)
		{
			ImVec2 a, b;
			rectOnScreen(result.Selected->ScreenRect, a, b);
			const ImU32 color = result.LayoutControlled ? IM_COL32(255, 180, 60, 255) : IM_COL32(60, 160, 255, 255);
			draw->AddRect(a, b, color, 0.0f, 0, 2.0f);
			if (!result.LayoutControlled)
			{
				const float middleX = (a.x + b.x) * 0.5f, middleY = (a.y + b.y) * 0.5f, half = 3.5f;
				const ImVec2 handles[8] = { { a.x, a.y }, { b.x, a.y }, { a.x, b.y }, { b.x, b.y }, { middleX, a.y }, { middleX, b.y }, { a.x, middleY }, { b.x, middleY } };
				for (const ImVec2& handle : handles)
				{
					draw->AddRectFilled({ handle.x - half, handle.y - half }, { handle.x + half, handle.y + half }, IM_COL32(255, 255, 255, 255));
					draw->AddRect({ handle.x - half, handle.y - half }, { handle.x + half, handle.y + half }, color);
				}
			}
			else if (input.Inside)
			{
				draw->AddText(ImVec2{ a.x, std::max(imageMin.y, a.y - ImGui::GetTextLineHeight() - 2.0f) }, color, "Placed by the parent's layout");
			}
		}
		draw->PopClipRect();
	}

	void GameViewPanel::DrawNoCameraMessage() const
	{
		const glm::vec2* bounds = m_Viewport->GetBounds();
		const float padding = ImGui::GetFontSize();
		const float width = std::max(1.0f, bounds[1].x - bounds[0].x - padding * 2.0f);
		const ImVec2 titleSize = ImGui::CalcTextSize(kNoCameraTitle, nullptr, false, width);
		const ImVec2 hintSize = ImGui::CalcTextSize(kNoCameraHint, nullptr, false, width);
		const float spacing = ImGui::GetStyle().ItemSpacing.y;

		const float centerX = (bounds[0].x + bounds[1].x) * 0.5f;
		const float top = std::max(bounds[0].y + padding, (bounds[0].y + bounds[1].y) * 0.5f -
															  (titleSize.y + spacing + hintSize.y) * 0.5f);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(centerX - titleSize.x * 0.5f, top),
						  Colors::Theme::textBrighter, kNoCameraTitle, nullptr, width);
		drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
						  ImVec2(centerX - hintSize.x * 0.5f, top + titleSize.y + spacing),
						  Colors::Theme::textCaption, kNoCameraHint, nullptr, width);
	}

}
