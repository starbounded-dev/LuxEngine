// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Editor/FontAwesome.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace Lux {

	// Dockable view of the undo/redo history. It reads labels and drives undo/redo through a set of
	// bindings, so it stays decoupled from EditorLayer's internals. Header-only on purpose: no new
	// .cpp means no project regeneration (and no Windows LNK2001 for a forgotten file).
	//
	// Layout is Photoshop-style: oldest state at the top, the current position in the middle, and the
	// redoable (future) states below, dimmed. Clicking a row jumps straight to that state.
	class UndoHistoryPanel : public EditorPanel
	{
	public:
		struct Bindings
		{
			std::function<std::vector<std::string>()> UndoLabels;  // oldest applied edit first
			std::function<std::vector<std::string>()> RedoLabels;  // next edit to redo first
			std::function<void(int steps)> Undo;
			std::function<void(int steps)> Redo;
			std::function<bool()> IsAvailable;                     // false during Play/Simulate
		};

		explicit UndoHistoryPanel(Bindings bindings)
			: m_Bindings(std::move(bindings))
		{
		}
#ifndef LUX_DIST
		static bool RunSelfTests(std::vector<std::string>& failures);
#endif

		virtual void OnImGuiRender(bool& isOpen) override
		{
			if (!isOpen)
				return;
			if (!ImGui::Begin("History", &isOpen))
			{
				ImGui::End();
				return;
			}
			const bool available = !m_Bindings.IsAvailable || m_Bindings.IsAvailable();
			const auto undo =
				available && m_Bindings.UndoLabels ? m_Bindings.UndoLabels() : std::vector<std::string>{};
			const auto redo =
				available && m_Bindings.RedoLabels ? m_Bindings.RedoLabels() : std::vector<std::string>{};
			const ImGuiEx::PanelAction actions[] = {
				{0, "Undo", LUX_ICON_UNDO, "Ctrl+Z",
				 available && !undo.empty() && static_cast<bool>(m_Bindings.Undo), false, true},
				{1, "Redo", LUX_ICON_REPEAT, "Ctrl+Shift+Z",
				 available && !redo.empty() && static_cast<bool>(m_Bindings.Redo), false, true},
				{2, "Current state", LUX_ICON_CROSSHAIRS, nullptr,
				 available && (!undo.empty() || !redo.empty())}};
			const int action = ImGuiEx::PanelActionBar("##history_actions", actions);
			int undoSteps = action == 0 ? 1 : 0;
			int redoSteps = action == 1 ? 1 : 0;
			if (!available)
				ImGuiEx::PanelEmptyState("##history_unavailable", "Edit mode only",
										 "Stop Play or Simulate to use scene history.");
			else if (undo.empty() && redo.empty())
				ImGuiEx::PanelEmptyState(
					"##history_empty", "No edits yet",
					"Scene edits appear here. Select an earlier or later state to move through history.");
			else
			{
				ImGui::BeginChild("##history_states");
				const float rowHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
				if (action == 2)
					ImGui::SetScrollY(std::max(0.0f, rowHeight * static_cast<float>(undo.size()) -
														 ImGui::GetWindowHeight() * 0.5f));
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(undo.size() + redo.size() + 1), rowHeight);
				while (clipper.Step())
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
					{
						ImGuiEx::ScopedID id(row);
						const size_t index = static_cast<size_t>(row);
						const bool current = index == undo.size();
						const bool future = index > undo.size();
						const char* label = current	 ? "Current state"
											: future ? redo[index - undo.size() - 1].c_str()
													 : undo[index].c_str();
						const ImVec2 minimum = ImGui::GetCursorScreenPos();
						const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
						if (ImGui::Selectable("##state", current, 0, {width, ImGui::GetFrameHeight()}))
						{
							if (index < undo.size())
								undoSteps = static_cast<int>(undo.size() - index);
							else if (future)
								redoSteps = static_cast<int>(index - undo.size());
						}
						const bool tooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
						{
							ImGuiEx::ScopedColour color(ImGuiCol_Text, current	? Colors::Theme::accent
																	   : future ? Colors::Theme::textDarker
																				: Colors::Theme::text);
							ImGui::RenderTextEllipsis(
								ImGui::GetWindowDrawList(), minimum,
								{minimum.x + width, minimum.y + ImGui::GetFrameHeight()}, minimum.x + width,
								label, label + std::strlen(label), nullptr);
						}
						if (tooltip && ImGui::BeginTooltip())
						{
							ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
							ImGui::TextUnformatted(label);
							ImGui::PopTextWrapPos();
							ImGui::EndTooltip();
						}
					}
				ImGui::EndChild();
			}
			ImGui::End();
			// Scene replacement runs after every ImGui scope using the label snapshots has closed.
			if (undoSteps > 0 && m_Bindings.Undo)
				m_Bindings.Undo(undoSteps);
			else if (redoSteps > 0 && m_Bindings.Redo)
				m_Bindings.Redo(redoSteps);
		}

	private:
		Bindings m_Bindings;
	};

}
