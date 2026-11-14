// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "EditorConsolePanel.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/ApplicationSettings.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Input.h"
#include "Lux/ImGui/PanelControls.h"
//#include "Lux/Core/Events/SceneEvents.h"
#include "Lux/Editor/EditorResources.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui_internal.h>

#include <format>
#include <imgui/misc/cpp/imgui_stdlib.h>

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
		if (!m_IsFocused)
			return;
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(
			[this](KeyPressedEvent& key)
			{
				if (ImGui::GetIO().WantTextInput || GImGui->OpenPopupStack.Size > 0)
					return true;
				const bool control =
					Input::IsKeyDown(Key::LeftControl) || Input::IsKeyDown(Key::RightControl);
				if (key.GetKeyCode() == Key::F && control && !key.GetRepeatCount())
				{
					m_FocusSearch = true;
					return true;
				}
				return false;
			});
	}

	void EditorConsolePanel::CloseDetails()
	{
		m_DetailedPanelOpen = false;
		ImGuiEx::ClosePopupByID(m_DetailsPopupID);
		m_DetailsPopupID = 0;
	}

	void EditorConsolePanel::OnClose()
	{
		m_IsFocused = false;
		m_FocusSearch = false;
		CloseDetails();
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
		m_DrainBuffer.clear();
		{
			std::scoped_lock<std::mutex> lock(s_PendingMessageMutex);
			if (s_PendingMessages.empty())
				return;

			m_DrainBuffer.swap(s_PendingMessages);
		}
		for (auto& message : m_DrainBuffer)
			AppendMessage(std::move(message));
		m_DrainBuffer.clear();
		if (m_MessageBuffer.size() > s_MaxMessages)
			m_MessageBuffer.erase(m_MessageBuffer.begin(),
								  m_MessageBuffer.begin() + (m_MessageBuffer.size() - s_MaxMessages));
		m_VisibleMessagesDirty = true;
	}

	void EditorConsolePanel::AppendMessage(ConsoleMessage message)
	{
		std::tm time{};
#ifdef LUX_PLATFORM_WINDOWS
		const bool validTime = localtime_s(&time, &message.Time) == 0;
#else
		const bool validTime = localtime_r(&message.Time, &time) != nullptr;
#endif
		char timestamp[16]{};
		if (!validTime || !std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &time))
			std::strcpy(timestamp, "Invalid time");
		std::string caption = std::format("{} | {} | {}", GetMessageType(message), timestamp, message.Tag);
		m_KnownTags.insert(message.Tag);
		m_MessageBuffer.push_back({std::move(message), m_NextMessageID++, timestamp, std::move(caption)});
		m_VisibleMessagesDirty = true;
	}

	void EditorConsolePanel::OnImGuiRender(bool& isOpen)
	{
		m_IsFocused = false;
		// Closed panels retain the bounded pending queue until PanelManager renders them again.
		DrainPendingMessages();
		if (ImGui::Begin(m_PanelName, &isOpen))
		{
			m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			RenderMenu();
			RenderConsole(ImGui::GetContentRegionAvail());
			RenderDetails();
		}
		ImGui::End();
	}

	void EditorConsolePanel::OnProjectChanged(const Ref<Project>& project)
	{
		OnClose();
		m_MessageBuffer.clear();
		m_VisibleMessages.clear();
		m_KnownTags.clear();
		m_HiddenTags.clear();
		m_SearchQuery.clear();
		m_SearchCommitPending = false;
		m_DetailedMessage.clear();
		m_DetailedCaption.clear();
		ClearProgress();
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

	void EditorConsolePanel::RenderMenu()
	{
		enum Action
		{
			Clear,
			Follow,
			ClearOnPlay,
			Info,
			Warning,
			Error,
			Tags,
			Recent
		};
		const ImGuiEx::PanelAction actions[] = {
			{Clear, "Clear", LUX_ICON_TRASH, nullptr, true, false, true},
			{Follow, "Follow latest", LUX_ICON_ARROW_DOWN, nullptr, true, m_EnableScrollToLatest},
			{Info, "Info", LUX_ICON_INFO_CIRCLE, nullptr, true,
			 (m_MessageFilters & static_cast<int16_t>(ConsoleMessageFlags::Info)) != 0},
			{Warning, "Warnings", LUX_ICON_EXCLAMATION_TRIANGLE, nullptr, true,
			 (m_MessageFilters & static_cast<int16_t>(ConsoleMessageFlags::Warning)) != 0},
			{Error, "Errors", LUX_ICON_EXCLAMATION_CIRCLE, nullptr, true,
			 (m_MessageFilters & static_cast<int16_t>(ConsoleMessageFlags::Error)) != 0},
			{Tags, "Tags", LUX_ICON_TAG},
			{Recent, "Recent searches", LUX_ICON_HISTORY},
			{ClearOnPlay, "Clear on Play", nullptr, nullptr, true, m_ClearOnPlay, false, true}};
		const int action = ImGuiEx::PanelActionBar("##log_actions", actions);
		switch (action)
		{
		case Clear:
			m_MessageBuffer.clear();
			m_VisibleMessagesDirty = true;
			break;
		case Follow:
			m_EnableScrollToLatest = !m_EnableScrollToLatest;
			break;
		case ClearOnPlay:
			m_ClearOnPlay = !m_ClearOnPlay;
			break;
		case Info:
		case Warning:
		case Error:
			m_MessageFilters ^= static_cast<int16_t>(1u << (action - Info));
			m_VisibleMessagesDirty = true;
			break;
		case Tags:
			ImGui::OpenPopup("LogTags");
			break;
		case Recent:
			ImGui::OpenPopup("RecentLogSearches");
			break;
		default:
			break;
		}
		if (m_FocusSearch)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusSearch = false;
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputTextWithHint("##log_search", "Search log... (Ctrl+F)", &m_SearchQuery))
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
		if (ImGui::BeginPopup("LogTags"))
		{
			if (ImGui::MenuItem("Show all"))
			{
				m_HiddenTags.clear();
				m_VisibleMessagesDirty = true;
			}
			if (ImGui::MenuItem("Hide all"))
			{
				m_HiddenTags = m_KnownTags;
				m_VisibleMessagesDirty = true;
			}
			ImGui::Separator();
			for (const auto& tag : m_KnownTags)
			{
				ImGuiEx::ScopedID id(tag.c_str());
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
			if (m_KnownTags.empty())
				ImGui::TextUnformatted("No tags have been logged yet.");
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopup("RecentLogSearches"))
		{
			if (m_RecentSearches.empty())
				ImGui::TextUnformatted("No recent searches");
			const auto searches = m_RecentSearches;
			for (const auto& query : searches)
				if (ImGui::Selectable(query.c_str()))
				{
					m_SearchQuery = query;
					m_SearchCommitPending = false;
					m_VisibleMessagesDirty = true;
					AddRecentSearch(query);
				}
			if (!m_RecentSearches.empty() && ImGui::MenuItem("Clear history"))
			{
				m_RecentSearches.clear();
				SaveRecentSearches();
			}
			ImGui::EndPopup();
		}
		if (!m_ProgressLabel.empty())
		{
			ImGui::ProgressBar(m_Progress, {-FLT_MIN, ImGui::GetFrameHeight()}, m_ProgressLabel.c_str());
			ImGui::SetItemTooltip("%s (%.0f%%)", m_ProgressLabel.c_str(), m_Progress * 100.0f);
		}
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
			const ConsoleMessage& message = m_MessageBuffer[i].Message;
			if (!(m_MessageFilters & message.Flags))
				continue;
			if (m_HiddenTags.contains(message.Tag))
				continue;
			if (!ImGuiEx::IsMatchingSearch(message.LongMessage, m_SearchQuery) &&
				!ImGuiEx::IsMatchingSearch(message.Tag, m_SearchQuery))
				continue;

			m_VisibleMessages.push_back(i);
		}

		m_VisibleMessagesDirty = false;
	}

	void EditorConsolePanel::RenderConsole(const ImVec2& size)
	{
		RebuildVisibleMessages();
		const bool compact = size.x < ImGui::GetFontSize() * 35.0f;
		const float lineHeight = ImGui::GetTextLineHeight();
		const float cellHeight =
			compact ? lineHeight * 2.0f + ImGui::GetStyle().ItemInnerSpacing.y : lineHeight;
		const float rowHeight = cellHeight + ImGui::GetStyle().CellPadding.y * 2.0f;
		const float bodyHeight = std::max(lineHeight, size.y - lineHeight - ImGui::GetStyle().ItemSpacing.y);
		bool openDetails = false;
		const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
									  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
		const char* tableName = compact ? "##console_compact" : "Console";
		if (ImGui::BeginTable(tableName, compact ? 1 : 3, flags, {0.0f, bodyHeight}))
		{
			if (!compact)
			{
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed,
										ImGui::GetFontSize() * 5.0f);
				ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed,
										ImGui::GetFontSize() * 6.0f);
			}
			ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();
			auto* window = ImGui::GetCurrentWindow();
			const bool scrollDrag = GImGui->ActiveId == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y);
			const bool keyboardInspect = ImGui::IsWindowFocused() && (ImGui::IsKeyPressed(ImGuiKey_PageUp) ||
																	  ImGui::IsKeyPressed(ImGuiKey_Home));
			if (scrollDrag || (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0.0f) ||
				keyboardInspect)
				m_EnableScrollToLatest = false;
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(m_VisibleMessages.size()), rowHeight);
			while (clipper.Step())
				for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
				{
					const auto& row = m_MessageBuffer[m_VisibleMessages[static_cast<size_t>(index)]];
					const auto& message = row.Message;
					ImGuiEx::ScopedID id(reinterpret_cast<const void*>(row.ID));
					ImGui::TableNextRow(0, rowHeight);
					ImGui::TableNextColumn();
					if (!compact)
					{
						ImGui::TextColored(GetMessageColor(message), "%s", GetMessageType(message));
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(row.Time.c_str());
						ImGui::TableNextColumn();
					}
					const ImVec2 textMin = ImGui::GetCursorScreenPos();
					const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
					if (ImGui::Selectable("##message", false, ImGuiSelectableFlags_SpanAllColumns,
										  {0.0f, cellHeight}))
					{
						m_DetailedMessage = message.LongMessage;
						m_DetailedCaption = row.Caption;
						openDetails = true;
					}
					if (ImGui::BeginPopupContextItem("ConsoleMessageContext"))
					{
						if (ImGui::MenuItem("Copy Full Message"))
							ImGui::SetClipboardText(message.LongMessage.c_str());
						if (ImGui::MenuItem("Copy Short Message"))
							ImGui::SetClipboardText(message.ShortMessage.c_str());
						ImGui::EndPopup();
					}
					ImVec2 messageMin = textMin;
					if (compact)
					{
						ImGuiEx::ScopedColour severity(ImGuiCol_Text, GetMessageColor(message));
						ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), textMin,
												  {textMin.x + width, textMin.y + lineHeight},
												  textMin.x + width, row.Caption.c_str(),
												  row.Caption.c_str() + row.Caption.size(), nullptr);
						messageMin.y += lineHeight + ImGui::GetStyle().ItemInnerSpacing.y;
					}
					ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), messageMin,
											  {messageMin.x + width, messageMin.y + lineHeight},
											  messageMin.x + width, message.ShortMessage.c_str(),
											  message.ShortMessage.c_str() + message.ShortMessage.size(),
											  nullptr);
				}
			if (m_EnableScrollToLatest)
			{
				// The clipper preserves the content-end position for this scrolling helper.
				ImGui::SetScrollHereY(1.0f);
			}
			ImGui::EndTable();
		}
		char count[64];
		std::snprintf(count, sizeof(count), "%zu / %zu messages", m_VisibleMessages.size(),
					  m_MessageBuffer.size());
		const ImVec2 minimum = ImGui::GetCursorScreenPos();
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		ImGui::Dummy({width, lineHeight});
		ImGui::SetItemTooltip(
			"%zu shown / %zu retained. Scroll up to inspect; Follow latest resumes live following.",
			m_VisibleMessages.size(), m_MessageBuffer.size());
		ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), minimum,
								  {minimum.x + width, minimum.y + lineHeight}, minimum.x + width, count,
								  nullptr, nullptr);
		if (openDetails)
		{
			m_DetailsPopupID = ImGui::GetID("Detailed Message");
			m_DetailedPanelOpen = true;
			ImGui::OpenPopup("Detailed Message");
		}
	}

	void EditorConsolePanel::RenderDetails()
	{
		const auto* viewport = ImGui::GetWindowViewport();
		const float font = ImGui::GetFontSize();
		ImGui::SetNextWindowSize({std::min(font * 44.0f, viewport->WorkSize.x * 0.9f),
								  std::min(font * 24.0f, viewport->WorkSize.y * 0.85f)},
								 ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
		if (ImGui::BeginPopupModal("Detailed Message", &m_DetailedPanelOpen))
		{
			const ImGuiEx::PanelAction actions[] = {
				{0, "Copy full message", LUX_ICON_FILES_O},
				{1, "Close", LUX_ICON_TIMES, "Escape", true, false, true}};
			const int action = ImGuiEx::PanelActionBar("##message_actions", actions);
			if (action == 0)
				ImGui::SetClipboardText(m_DetailedMessage.c_str());
			if (action == 1 || ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				ImGui::CloseCurrentPopup();
				m_DetailedPanelOpen = false;
			}
			ImGui::TextWrapped("%s", m_DetailedCaption.c_str());
			ImGui::Separator();
			ImGui::BeginChild("##message_text", {0.0f, 0.0f});
			ImGui::TextWrapped("%s", m_DetailedMessage.c_str());
			ImGui::EndChild();
			ImGui::EndPopup();
		}
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

	void EditorConsolePanel::PushMessage(const ConsoleMessage& message)
	{
		std::scoped_lock<std::mutex> lock(s_PendingMessageMutex);
		s_PendingMessages.push_back(message);
		TrimToCapacity(s_PendingMessages);
	}

}
