// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "EditorConsolePanel.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/ApplicationSettings.h"
//#include "Lux/Core/Events/SceneEvents.h"
#include "Lux/Editor/EditorResources.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui_internal.h>

#include <format>

namespace Lux {

	static EditorConsolePanel* s_Instance = nullptr;

	// Loggers on any thread only append here; the panel drains it on the UI thread each frame. The
	// panel's own buffer is never touched off the UI thread, so a log call made while the console is
	// drawing cannot deadlock on it.
	static std::mutex s_PendingMessageMutex;
	static std::vector<ConsoleMessage> s_PendingMessages;

	// A script logging every frame would otherwise grow both buffers without bound. Also bounds the
	// pending queue in Lux-Runtime, which links the console sink but never creates the panel.
	static constexpr size_t s_MaxMessages = 10000;

	// The search widget does not report when typing ends, so a query counts as a finished search once
	// it has been left unchanged this long.
	static constexpr size_t s_MaxRecentSearches = 5;
	static constexpr double s_SearchCommitDelay = 1.0;
	static constexpr const char* s_RecentSearchesSettingKey = "Console.RecentSearches";
	static constexpr char s_RecentSearchesSeparator = '|';

	static void TrimToCapacity(std::vector<ConsoleMessage>& messages)
	{
		if (messages.size() > s_MaxMessages)
			messages.erase(messages.begin(), messages.begin() + (messages.size() - s_MaxMessages));
	}

	// Severity tints from the editor concept palette: cool blue info, warm amber warning, red error.
	static const ImVec4 s_InfoTint = ImVec4(0.471f, 0.667f, 1.0f, 1.0f);
	static const ImVec4 s_WarningTint = ImVec4(0.878f, 0.635f, 0.302f, 1.0f);
	static const ImVec4 s_ErrorTint = ImVec4(0.910f, 0.329f, 0.329f, 1.0f);

	EditorConsolePanel::EditorConsolePanel()
	{
		LUX_CORE_ASSERT(s_Instance == nullptr);
		s_Instance = this;

		m_MessageBuffer.reserve(500);
		LoadRecentSearches();
	}

	EditorConsolePanel::~EditorConsolePanel()
	{
		s_Instance = nullptr;
	}

	void EditorConsolePanel::OnEvent(Event& event)
	{
	}

	void EditorConsolePanel::OnScenePlay()
	{
		if (!m_ClearOnPlay)
			return;

		// Drop what was queued before Play too, so the first frame shows only this session.
		{
			std::scoped_lock<std::mutex> lock(s_PendingMessageMutex);
			s_PendingMessages.clear();
		}
		m_MessageBuffer.clear();
		m_VisibleMessagesDirty = true;
	}

	void EditorConsolePanel::DrainPendingMessages()
	{
		{
			std::scoped_lock<std::mutex> lock(s_PendingMessageMutex);
			if (s_PendingMessages.empty())
				return;

			for (const ConsoleMessage& message : s_PendingMessages)
				m_KnownTags.insert(message.Tag);

			m_MessageBuffer.insert(m_MessageBuffer.end(), std::make_move_iterator(s_PendingMessages.begin()), std::make_move_iterator(s_PendingMessages.end()));
			s_PendingMessages.clear();
		}

		TrimToCapacity(m_MessageBuffer);
		m_VisibleMessagesDirty = true;

		if (m_EnableScrollToLatest)
			m_ScrollToLatest = true;
	}

	void EditorConsolePanel::OnImGuiRender(bool& isOpen)
	{
		// Drained even while the panel is closed, so reopening it shows everything logged meanwhile.
		DrainPendingMessages();

		if (ImGui::Begin(m_PanelName, &isOpen))
		{
			ImVec2 consoleSize = ImGui::GetContentRegionAvail();
			consoleSize.y -= 32.0f;

			RenderMenu({ consoleSize.x, 28.0f });
			RenderConsole(consoleSize);
		}
		ImGui::End();
	}

	void EditorConsolePanel::OnProjectChanged(const Ref<Project>& project)
	{
		m_MessageBuffer.clear();
		m_VisibleMessagesDirty = true;
	}

	void EditorConsolePanel::Focus()
	{
		ImGui::SetWindowFocus(m_PanelName);
	}

	void EditorConsolePanel::SetProgress(const std::string& label, float progress)
	{
		m_ProgressLabel = label;
		m_Progress = progress;

		if (m_Progress >= 1.0f)
			ClearProgress();
	}

	void EditorConsolePanel::ClearProgress()
	{
		m_ProgressLabel = std::string();
		m_Progress = 0.0f;
	}

	void EditorConsolePanel::RenderMenu(const ImVec2& size)
	{
		ImGuiEx::ScopedStyleStack frame(ImGuiStyleVar_FrameBorderSize, 0.0f, ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::BeginChild("Toolbar", size);

		const float ToolbarHeight = 28.0f;

		if (ImGui::Button("Clear", { 75.0f, ToolbarHeight }))
		{
			m_MessageBuffer.clear();
			m_VisibleMessagesDirty = true;
		}

		ImGui::SameLine();

		const auto& style = ImGui::GetStyle();
		const std::string clearOnPlayText = std::format("{} Clear on Play", m_ClearOnPlay ? LUX_ICON_CHECK : LUX_ICON_TIMES);
		ImVec4 textColor = m_ClearOnPlay ? style.Colors[ImGuiCol_Text] : style.Colors[ImGuiCol_TextDisabled];
		if (ImGuiEx::ColoredButton(clearOnPlayText.c_str(), GetToolbarButtonColor(m_ClearOnPlay), textColor, ImVec2(110.0f, ToolbarHeight)))
			m_ClearOnPlay = !m_ClearOnPlay;

		{
			// Match the toolbar buttons' height. Popped right after BeginCombo so the dropdown's
			// checkboxes keep normal padding.
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, (ToolbarHeight - ImGui::GetFontSize()) * 0.5f));

			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0f);
			if (ImGuiEx::Widgets::SearchWidget(m_SearchQuery, "Search log..."))
			{
				m_VisibleMessagesDirty = true;
				m_SearchEditTime = ImGui::GetTime();
				m_SearchCommitPending = true;
			}

			if (m_SearchCommitPending && ImGui::GetTime() - m_SearchEditTime >= s_SearchCommitDelay)
			{
				AddRecentSearch(m_SearchQuery);
				m_SearchCommitPending = false;
			}

			ImGui::SameLine();
			if (ImGui::Button(LUX_ICON_HISTORY, ImVec2(ToolbarHeight, ToolbarHeight)))
				ImGui::OpenPopup("RecentLogSearches");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Recent searches");

			ImGui::SameLine();
			ImGui::SetNextItemWidth(150.0f);
			const std::string tagPreview = m_HiddenTags.empty()
				? std::string("All tags")
				: std::format("{} of {} tags", m_KnownTags.size() - m_HiddenTags.size(), m_KnownTags.size());
			const bool tagComboOpen = ImGui::BeginCombo("##LogTags", tagPreview.c_str(), ImGuiComboFlags_HeightLarge);
			ImGui::PopStyleVar();

			if (tagComboOpen)
			{
				if (ImGui::Selectable("Show all", false, ImGuiSelectableFlags_NoAutoClosePopups))
				{
					m_HiddenTags.clear();
					m_VisibleMessagesDirty = true;
				}
				if (ImGui::Selectable("Hide all", false, ImGuiSelectableFlags_NoAutoClosePopups))
				{
					m_HiddenTags = m_KnownTags;
					m_VisibleMessagesDirty = true;
				}
				ImGui::Separator();

				for (const std::string& tag : m_KnownTags)
				{
					bool shown = !m_HiddenTags.contains(tag);
					if (ImGui::Checkbox(tag.c_str(), &shown))
					{
						if (shown)
							m_HiddenTags.erase(tag);
						else
							m_HiddenTags.insert(tag);
						m_VisibleMessagesDirty = true;
					}
				}
				ImGui::EndCombo();
			}
		}

		if (ImGui::BeginPopup("RecentLogSearches"))
		{
			if (m_RecentSearches.empty())
				ImGui::TextDisabled("No recent searches");

			// Copied: picking an entry reorders m_RecentSearches.
			const std::vector<std::string> recentSearches = m_RecentSearches;
			for (const std::string& query : recentSearches)
			{
				if (ImGui::Selectable(query.c_str()))
				{
					m_SearchQuery = query;
					m_SearchCommitPending = false;
					m_VisibleMessagesDirty = true;
					AddRecentSearch(query);
				}
			}

			if (!m_RecentSearches.empty())
			{
				ImGui::Separator();
				if (ImGui::Selectable("Clear history"))
				{
					m_RecentSearches.clear();
					SaveRecentSearches();
				}
			}
			ImGui::EndPopup();
		}

		if (!m_ProgressLabel.empty())
		{
			ImGui::SameLine();
			std::string progressBarText = std::format("{} ({}%)", m_ProgressLabel, (int)(m_Progress * 100 + 0.01f));
			ImGui::ProgressBar(m_Progress, ImVec2(ImGui::GetContentRegionAvail().x / 2.0f, ToolbarHeight), progressBarText.c_str());
		}

		{
			const ImVec2 buttonSize(ToolbarHeight, ToolbarHeight);

			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100.0f, 0.0f);
			textColor = (m_MessageFilters & (int16_t)ConsoleMessageFlags::Info) ? s_InfoTint : style.Colors[ImGuiCol_TextDisabled];
			if (ImGuiEx::ColoredButton(LUX_ICON_INFO_CIRCLE, GetToolbarButtonColor(m_MessageFilters & (int16_t)ConsoleMessageFlags::Info), textColor, buttonSize))
			{
				m_MessageFilters ^= (int16_t)ConsoleMessageFlags::Info;
				m_VisibleMessagesDirty = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Toggle info messages");

			ImGui::SameLine();
			textColor = (m_MessageFilters & (int16_t)ConsoleMessageFlags::Warning) ? s_WarningTint : style.Colors[ImGuiCol_TextDisabled];
			if (ImGuiEx::ColoredButton(LUX_ICON_EXCLAMATION_TRIANGLE, GetToolbarButtonColor(m_MessageFilters & (int16_t)ConsoleMessageFlags::Warning), textColor, buttonSize))
			{
				m_MessageFilters ^= (int16_t)ConsoleMessageFlags::Warning;
				m_VisibleMessagesDirty = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Toggle warning and Vulkan validation warning messages");

			ImGui::SameLine();
			textColor = (m_MessageFilters & (int16_t)ConsoleMessageFlags::Error) ? s_ErrorTint : style.Colors[ImGuiCol_TextDisabled];
			if (ImGuiEx::ColoredButton(LUX_ICON_EXCLAMATION_CIRCLE, GetToolbarButtonColor(m_MessageFilters & (int16_t)ConsoleMessageFlags::Error), textColor, buttonSize))
			{
				m_MessageFilters ^= (int16_t)ConsoleMessageFlags::Error;
				m_VisibleMessagesDirty = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Toggle error, Vulkan validation error, and shader compile error messages");
		}

		ImGui::EndChild();
	}

	void EditorConsolePanel::AddRecentSearch(const std::string& query)
	{
		const std::string trimmed = Utils::String::TrimWhitespace(query);
		if (trimmed.empty() || trimmed.find(s_RecentSearchesSeparator) != std::string::npos)
			return;

		const std::string lowered = Utils::String::ToLowerCopy(trimmed);

		// Refining the latest search ("mip" -> "mips", or back) replaces it instead of filling the list.
		if (!m_RecentSearches.empty())
		{
			const std::string latest = Utils::String::ToLowerCopy(m_RecentSearches.front());
			if (lowered.find(latest) != std::string::npos || latest.find(lowered) != std::string::npos)
				m_RecentSearches.erase(m_RecentSearches.begin());
		}

		std::erase_if(m_RecentSearches, [&](const std::string& entry) { return Utils::String::EqualsIgnoreCase(entry, trimmed); });

		m_RecentSearches.insert(m_RecentSearches.begin(), trimmed);
		if (m_RecentSearches.size() > s_MaxRecentSearches)
			m_RecentSearches.resize(s_MaxRecentSearches);

		SaveRecentSearches();
	}

	void EditorConsolePanel::LoadRecentSearches()
	{
		m_RecentSearches.clear();

		const std::string stored = Application::Get().GetSettings().Get(s_RecentSearchesSettingKey, "");
		size_t start = 0;
		while (start < stored.size() && m_RecentSearches.size() < s_MaxRecentSearches)
		{
			size_t end = stored.find(s_RecentSearchesSeparator, start);
			if (end == std::string::npos)
				end = stored.size();

			if (end > start)
				m_RecentSearches.push_back(stored.substr(start, end - start));
			start = end + 1;
		}
	}

	void EditorConsolePanel::SaveRecentSearches() const
	{
		std::string stored;
		for (const std::string& query : m_RecentSearches)
		{
			if (!stored.empty())
				stored += s_RecentSearchesSeparator;
			stored += query;
		}

		auto& settings = Application::Get().GetSettings();
		settings.Set(s_RecentSearchesSettingKey, stored);
		settings.Serialize();
	}

	void EditorConsolePanel::RebuildVisibleMessages()
	{
		if (!m_VisibleMessagesDirty)
			return;

		m_VisibleMessages.clear();
		for (uint32_t i = 0; i < m_MessageBuffer.size(); i++)
		{
			const ConsoleMessage& message = m_MessageBuffer[i];
			if (!(m_MessageFilters & message.Flags))
				continue;
			if (m_HiddenTags.contains(message.Tag))
				continue;
			if (!ImGuiEx::IsMatchingSearch(message.LongMessage, m_SearchQuery))
				continue;

			m_VisibleMessages.push_back(i);
		}

		m_VisibleMessagesDirty = false;
	}

	void EditorConsolePanel::RenderConsole(const ImVec2& size)
	{
		static const char* s_Columns[] = { "Type", "Timestamp", "Message" };

		// The panel mirrors every engine log line, so the buffer runs to thousands of rows: gather
		// the ones passing the filters, then let the clipper submit only those on screen.
		RebuildVisibleMessages();

		ImGuiEx::Table("Console", s_Columns, 3, size, [&]()
			{
				float scrollY = ImGui::GetScrollY();
				if (scrollY < m_PreviousScrollY)
					m_EnableScrollToLatest = false;

				if (scrollY >= ImGui::GetScrollMaxY())
					m_EnableScrollToLatest = true;

				m_PreviousScrollY = scrollY;

				const float rowHeight = 24.0f;
				bool openDetailedPopup = false;

				ImGuiListClipper clipper;
				clipper.Begin((int)m_VisibleMessages.size(), rowHeight);
				while (clipper.Step())
				{
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
					{
						const auto& msg = m_MessageBuffer[m_VisibleMessages[row]];

						ImGui::PushID(row);

						const bool clicked = ImGuiEx::TableRowClickable(msg.ShortMessage.c_str(), rowHeight);

						ImGuiEx::Separator(ImVec2(4.0f, ImGui::CalcTextSize(msg.ShortMessage.c_str()).y), GetMessageColor(msg));
						ImGui::SameLine();
						ImGui::TextColored(GetMessageColor(msg), "%s", GetMessageType(msg));
						ImGui::TableNextColumn();
						ImGuiEx::ShiftCursorX(4.0f);

						std::stringstream timeString;
						tm* timeBuffer = localtime(&msg.Time);
						timeString << std::put_time(timeBuffer, "%T");

						// Timestamp + message in the mono face, like the concept's log; timestamp dimmed.
						ImGuiEx::Fonts::PushFont("Mono");
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker));
						ImGui::TextUnformatted(timeString.str().c_str());
						ImGui::PopStyleColor();

						ImGui::TableNextColumn();
						ImGuiEx::ShiftCursorX(4.0f);
						ImGui::TextUnformatted(msg.ShortMessage.c_str());
						ImGuiEx::Fonts::PopFont();

						if (clicked)
						{
							// Copied, not indexed: the buffer can be trimmed or cleared while the popup is open.
							m_DetailedMessage = msg.LongMessage;
							openDetailedPopup = true;
						}

						if (ImGui::BeginPopupContextItem("ConsoleMessageContext"))
						{
							if (ImGui::MenuItem("Copy Full Message"))
								ImGui::SetClipboardText(msg.LongMessage.c_str());
							if (ImGui::MenuItem("Copy Short Message"))
								ImGui::SetClipboardText(msg.ShortMessage.c_str());
							ImGui::EndPopup();
						}

						ImGui::PopID();
					}
				}

				if (m_ScrollToLatest)
				{
					ImGui::SetScrollHereY(1.0f);
					m_ScrollToLatest = false;
				}

				if (openDetailedPopup)
				{
					ImGui::OpenPopup("Detailed Message");
					auto [width, height] = Application::Get().GetWindow().GetSize();
					auto [xPos, yPos] = Application::Get().GetWindow().GetWindowPos();
					ImGui::SetNextWindowSize({ (float)width * 0.5f, (float)height * 0.5f });
					ImGui::SetNextWindowPos({ xPos + (float)width / 2.0f, yPos + (float)height / 2.5f }, 0, { 0.5, 0.5 });
					m_DetailedPanelOpen = true;
				}

				if (m_DetailedPanelOpen)
				{
					ImGuiEx::ScopedStyle windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
					ImGuiEx::ScopedStyle framePadding(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 8.0f));

					if (ImGui::BeginPopupModal("Detailed Message", &m_DetailedPanelOpen, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
					{
						ImGui::TextWrapped("%s", m_DetailedMessage.c_str());
						if (ImGui::Button("Copy Full Message", ImVec2(150.0f, 28.0f)))
							ImGui::SetClipboardText(m_DetailedMessage.c_str());
						ImGui::EndPopup();
					}
				}
			});
	}

	const char* EditorConsolePanel::GetMessageType(const ConsoleMessage& message) const
	{
		if (message.Flags & (int16_t)ConsoleMessageFlags::Info) return "Info";
		if (message.Flags & (int16_t)ConsoleMessageFlags::Warning) return "Warning";
		if (message.Flags & (int16_t)ConsoleMessageFlags::Error) return "Error";
		return "Unknown";
	}

	const ImVec4& EditorConsolePanel::GetMessageColor(const ConsoleMessage& message) const
	{
		//if (message.Flags & (int16_t)ConsoleMessageFlags::Info) return s_InfoButtonOnTint;
		if (message.Flags & (int16_t)ConsoleMessageFlags::Warning) return s_WarningTint;
		if (message.Flags & (int16_t)ConsoleMessageFlags::Error) return s_ErrorTint;
		return s_InfoTint;
	}

	ImVec4 EditorConsolePanel::GetToolbarButtonColor(const bool value) const
	{
		const auto& style = ImGui::GetStyle();
		return value ? style.Colors[ImGuiCol_Header] : style.Colors[ImGuiCol_FrameBg];
	}

	void EditorConsolePanel::PushMessage(const ConsoleMessage& message)
	{
		std::scoped_lock<std::mutex> lock(s_PendingMessageMutex);
		s_PendingMessages.push_back(message);
		TrimToCapacity(s_PendingMessages);
	}

}
