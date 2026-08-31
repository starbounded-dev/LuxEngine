#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"

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

		virtual void OnImGuiRender(bool& isOpen) override
		{
			if (!ImGui::Begin("History", &isOpen))
			{
				ImGui::End();
				return;
			}

			if (m_Bindings.IsAvailable && !m_Bindings.IsAvailable())
			{
				ImGui::TextDisabled("History is available in Edit mode.");
				ImGui::End();
				return;
			}

			const std::vector<std::string> undo = m_Bindings.UndoLabels ? m_Bindings.UndoLabels() : std::vector<std::string>{};
			const std::vector<std::string> redo = m_Bindings.RedoLabels ? m_Bindings.RedoLabels() : std::vector<std::string>{};

			int undoSteps = 0;   // >0 => undo this many steps after the loop
			int redoSteps = 0;

			ImGuiEx::Fonts::PushFont("Mono");

			// Past states, oldest at the top. Clicking row k undoes back to just before that edit.
			for (size_t k = 0; k < undo.size(); k++)
			{
				ImGui::PushID((int)k);
				if (ImGui::Selectable(undo[k].c_str()))
					undoSteps = (int)(undo.size() - k);
				ImGui::PopID();
			}

			// Current position.
			{
				ImGuiEx::ScopedColour accent(ImGuiCol_Text, Colors::Theme::accent);
				ImGui::TextUnformatted(undo.empty() && redo.empty() ? "\xE2\x80\x94 nothing to undo \xE2\x80\x94" : "\xE2\x96\xB6 current");
			}

			// Future states (redoable), dimmed. Row j redoes forward j+1 steps. Scoped so the pushed
			// colour is popped before ImGui::End() rather than at function exit.
			{
				ImGuiEx::ScopedColour dim(ImGuiCol_Text, Colors::Theme::textDarker);
				for (size_t j = 0; j < redo.size(); j++)
				{
					ImGui::PushID(1000 + (int)j);
					if (ImGui::Selectable(redo[j].c_str()))
						redoSteps = (int)(j + 1);
					ImGui::PopID();
				}
			}

			ImGuiEx::Fonts::PopFont();

			// Apply after rendering so we don't mutate the stacks mid-list.
			if (undoSteps > 0 && m_Bindings.Undo)
				m_Bindings.Undo(undoSteps);
			else if (redoSteps > 0 && m_Bindings.Redo)
				m_Bindings.Redo(redoSteps);

			ImGui::End();
		}

	private:
		Bindings m_Bindings;
	};

}
