#include "lpch.h"
#include "CommandPalette.h"

#include "Lux/ImGui/Colors.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>

#include <algorithm>

namespace Lux {

	namespace {

		// Subsequence fuzzy match with contiguity and word-start bonuses. Returns false when the pattern
		// is not a subsequence of the haystack; otherwise fills `outScore` (higher is better).
		bool FuzzyMatch(const std::string& pattern, const std::string& haystack, int& outScore)
		{
			if (pattern.empty())
			{
				outScore = 0;
				return true;
			}

			int score = 0;
			int patternIndex = 0;
			int lastMatch = -2;
			for (int i = 0; i < static_cast<int>(haystack.size()) && patternIndex < static_cast<int>(pattern.size()); i++)
			{
				if (haystack[i] != pattern[patternIndex])
					continue;

				score += (i == lastMatch + 1) ? 6 : 1;                        // contiguous run
				if (i == 0 || haystack[i - 1] == ' ' || haystack[i - 1] == '/')
					score += 4;                                              // start of a word
				lastMatch = i;
				patternIndex++;
			}

			if (patternIndex != static_cast<int>(pattern.size()))
				return false;

			outScore = score - static_cast<int>(haystack.size()) / 8;                     // slight bias toward shorter names
			return true;
		}

	}

	void CommandPalette::Open()
	{
		m_Open = true;
		m_FocusInput = true;
		m_Query[0] = 0;
		m_LastQuery = "\x01";
		m_Selected = 0;
	}

	void CommandPalette::Close()
	{
		m_Open = false;
	}

	void CommandPalette::RebuildFiltered()
	{
		m_Filtered.clear();

		const std::string query = Utils::String::ToLowerCopy(std::string(m_Query));

		std::vector<std::pair<int, int>> scored;  // (score, command index)
		scored.reserve(m_Commands.size());
		for (int i = 0; i < static_cast<int>(m_Commands.size()); i++)
		{
			const std::string haystack = Utils::String::ToLowerCopy(m_Commands[i].Category + " " + m_Commands[i].Name);
			int score = 0;
			if (FuzzyMatch(query, haystack, score))
				scored.emplace_back(score, i);
		}

		std::stable_sort(scored.begin(), scored.end(), [&](const auto& a, const auto& b)
		{
			if (a.first != b.first)
				return a.first > b.first;
			return m_Commands[a.second].Name < m_Commands[b.second].Name;
		});

		for (const auto& [score, index] : scored)
			m_Filtered.push_back(index);

		if (m_Selected >= static_cast<int>(m_Filtered.size()))
			m_Selected = std::max(0, static_cast<int>(m_Filtered.size()) - 1);
	}

	void CommandPalette::OnImGuiRender()
	{
		if (!m_Open)
			return;

		ImGuiViewport* viewport = ImGui::GetMainViewport();

		// Dimming backdrop that also closes the palette when clicked.
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowBgAlpha(0.45f);
		ImGuiWindowFlags backdropFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));
		if (ImGui::Begin("##command_palette_backdrop", nullptr, backdropFlags))
		{
			if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				Close();
		}
		ImGui::End();
		ImGui::PopStyleColor();

		// Centered palette window.
		const float width = std::min(620.0f, viewport->Size.x * 0.7f);
		ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->Pos.y + viewport->Size.y * 0.22f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
		ImGui::SetNextWindowFocus();

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(Colors::Theme::backgroundPopup));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

		if (!ImGui::Begin("##command_palette", nullptr, flags))
		{
			ImGui::End();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();
			return;
		}

		// Esc closes from anywhere in the palette.
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			Close();

		// ---- Search input ----
		if (m_FocusInput)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusInput = false;
		}
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(Colors::Theme::backgroundDark));
		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool submitted = ImGui::InputTextWithHint("##palette_query", LUX_ICON_SEARCH "  Type a command…", m_Query, sizeof(m_Query), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::PopStyleColor();

		if (m_LastQuery != m_Query)
		{
			RebuildFiltered();
			m_LastQuery = m_Query;
			m_Selected = 0;
			m_ScrollToSelected = true;
		}

		// ---- Keyboard navigation (single-line input ignores Up/Down, so we own them) ----
		// Selection changes here request an auto-scroll; mouse-wheel scrolling does not, so the list
		// stays where the user put it instead of snapping back to the selected row every frame.
		const int resultCount = static_cast<int>(m_Filtered.size());
		if (resultCount > 0)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			{
				m_Selected = (m_Selected + 1) % resultCount;
				m_ScrollToSelected = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			{
				m_Selected = (m_Selected + resultCount - 1) % resultCount;
				m_ScrollToSelected = true;
			}
		}

		auto execute = [&](int filteredIndex)
		{
			if (filteredIndex < 0 || filteredIndex >= static_cast<int>(m_Filtered.size()))
				return;
			const Command& command = m_Commands[m_Filtered[filteredIndex]];
			if (command.Enabled && !command.Enabled())
				return;
			Close();
			if (command.Action)
				command.Action();
		};

		if (submitted)
			execute(m_Selected);

		ImGui::Spacing();

		// ---- Results ----
		const float rowHeight = ImGui::GetFrameHeight() + 4.0f;
		const float listHeight = std::min(resultCount, 10) * rowHeight + 4.0f;
		if (ImGui::BeginChild("##palette_results", ImVec2(0.0f, listHeight), false, ImGuiWindowFlags_NoNav))
		{
			for (int i = 0; i < resultCount; i++)
			{
				const Command& command = m_Commands[m_Filtered[i]];
				const bool enabled = !command.Enabled || command.Enabled();
				const bool selected = i == m_Selected;

				ImGui::PushID(i);

				const ImVec2 rowMin = ImGui::GetCursorScreenPos();
				const float rowWidth = ImGui::GetContentRegionAvail().x;
				if (ImGui::Selectable("##palette_row", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(rowWidth, rowHeight)))
				{
					m_Selected = i;
					execute(i);
				}
				// Follow the cursor, but only when the mouse actually moves — so wheel-scrolling past a
				// row doesn't hijack the selection.
				const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
				if (ImGui::IsItemHovered() && (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f))
					m_Selected = i;
				if (selected && m_ScrollToSelected)
					ImGui::SetScrollHereY(0.5f);

				ImDrawList* dl = ImGui::GetWindowDrawList();
				if (selected)
					dl->AddRectFilled(rowMin, ImVec2(rowMin.x + rowWidth, rowMin.y + rowHeight), Colors::Theme::selectionMuted, 4.0f);

				const float textY = rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
				const ImU32 nameColor = !enabled ? Colors::Theme::muted : (selected ? Colors::Theme::textBrighter : Colors::Theme::text);
				dl->AddText(ImVec2(rowMin.x + 10.0f, textY), nameColor, command.Name.c_str());

				// Right side: shortcut then category, both dim.
				float rightX = rowMin.x + rowWidth - 10.0f;
				if (!command.Category.empty())
				{
					const float w = ImGui::CalcTextSize(command.Category.c_str()).x;
					rightX -= w;
					dl->AddText(ImVec2(rightX, textY), Colors::Theme::textDarker, command.Category.c_str());
					rightX -= 12.0f;
				}
				if (!command.Shortcut.empty())
				{
					const float w = ImGui::CalcTextSize(command.Shortcut.c_str()).x;
					rightX -= w;
					dl->AddText(ImVec2(rightX, textY), Colors::Theme::muted, command.Shortcut.c_str());
				}

				ImGui::PopID();
			}

			if (resultCount == 0)
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker), "  No matching commands");
		}
		ImGui::EndChild();

		m_ScrollToSelected = false;   // consumed for this frame

		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
	}

}
