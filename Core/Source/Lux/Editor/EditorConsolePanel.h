// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "EditorPanel.h"

#include "EditorConsole/ConsoleMessage.h"

#include "Lux/Renderer/Texture.h"

#include <imgui/imgui.h>

#include <cstdint>
#include <vector>
#include <set>
#include <string>

namespace Lux {
	class EditorConsolePanel : public EditorPanel
	{
	public:
		EditorConsolePanel();
		~EditorConsolePanel();

		virtual void OnEvent(Event& e) override;
		virtual void OnImGuiRender(bool& isOpen) override;
		virtual void OnProjectChanged(const Ref<Project>& project) override;
		void OnClose() override;
#ifndef LUX_DIST
		bool RunSelfTests(std::vector<std::string>& failures);
#endif

		void Focus();

		// Honours the "Clear on Play" toggle. Called by the editor when a Play session starts.
		void OnScenePlay();

		void SetProgress(const std::string& label, float progress);
		void ClearProgress();
	private:
		void DrainPendingMessages();
		void RebuildVisibleMessages();
		void AddRecentSearch(const std::string& query);
		void LoadRecentSearches();
		void SaveRecentSearches() const;
		void RenderMenu();
		void RenderConsole(const ImVec2& size);
		void RenderDetails();
		void CloseDetails();
		void AppendMessage(ConsoleMessage message);
		const char* GetMessageType(const ConsoleMessage& message) const;
		const ImVec4& GetMessageColor(const ConsoleMessage& message) const;

	private:
		static void PushMessage(const ConsoleMessage& message);

	private:
		const char* m_PanelName = "Log";
		bool m_ClearOnPlay = true;

		// UI thread only; other threads go through PushMessage.
		struct MessageRow
		{
			ConsoleMessage Message;
			uint64_t ID;
			std::string Time;
			std::string Caption;
		};
		std::vector<MessageRow> m_MessageBuffer;
		std::vector<ConsoleMessage> m_DrainBuffer;
		uint64_t m_NextMessageID = 1;
		// Indices into m_MessageBuffer that pass the severity, tag and search filters. Rebuilt only when
		// the buffer or a filter changes, since matching the search against every message is not free.
		std::vector<uint32_t> m_VisibleMessages;
		bool m_VisibleMessagesDirty = true;

		std::string m_SearchQuery;
		std::vector<std::string> m_RecentSearches; // Newest first, persisted in the application settings.
		double m_SearchEditTime = 0.0;
		bool m_SearchCommitPending = false;
		std::set<std::string> m_KnownTags;  // Every tag seen this session, kept across Clear so hidden tags stay hidden.
		std::set<std::string> m_HiddenTags;

		bool m_EnableScrollToLatest = true;

		int16_t m_MessageFilters = (int16_t)ConsoleMessageFlags::All;

		bool m_DetailedPanelOpen = false;
		std::string m_DetailedMessage;
		std::string m_DetailedCaption;
		ImGuiID m_DetailsPopupID = 0;
		bool m_IsFocused = false;
		bool m_FocusSearch = false;

		std::string m_ProgressLabel;
		float m_Progress = 0.0f;
	private:
		friend class EditorConsoleSink;
	};

}
