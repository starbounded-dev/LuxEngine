// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "EditorDocs.h"

#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace Lux {

	namespace {

		std::function<void(std::string_view)> s_OpenTopicHandler;

	}

	void EditorDocs::SetOpenTopicHandler(std::function<void(std::string_view topic)> handler)
	{
		s_OpenTopicHandler = std::move(handler);
	}

	bool EditorDocs::OpenTopic(std::string_view topic)
	{
		if (!s_OpenTopicHandler)
			return false;

		s_OpenTopicHandler(topic);
		return true;
	}

	bool ImGuiEx::DocsButton(const char* strID, std::string_view topic, std::string_view title, const ImRect& rect)
	{
		if (!s_OpenTopicHandler)
			return false;

		ImGui::SetCursorScreenPos(rect.Min);
		const bool clicked = ImGui::InvisibleButton(strID, rect.GetSize());
		const bool hovered = ImGui::IsItemHovered();

		const ImVec2 iconSize = ImGui::CalcTextSize(LUX_ICON_QUESTION_CIRCLE);
		const ImVec2 iconPosition = ImVec2(rect.Min.x + (rect.GetWidth() - iconSize.x) * 0.5f, rect.Min.y + (rect.GetHeight() - iconSize.y) * 0.5f);
		ImGui::GetWindowDrawList()->AddText(iconPosition, hovered ? Colors::Theme::textBrighter : Colors::Theme::textDarker, LUX_ICON_QUESTION_CIRCLE);

		if (hovered)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			ImGui::SetTooltip("Help: %.*s", static_cast<int>(title.size()), title.data());
		}

		if (clicked)
			EditorDocs::OpenTopic(topic);
		return clicked;
	}

}
