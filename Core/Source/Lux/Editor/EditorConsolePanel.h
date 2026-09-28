// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "EditorPanel.h"

#include "EditorConsole/ConsoleMessage.h"

#include "Lux/Renderer/Texture.h"

#include <imgui/imgui.h>

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

		void Focus();

		// Honours the "Clear on Play" toggle. Called by the editor when a Play session starts.
		void OnScenePlay();

		void SetProgress(const std::string& label, float progress);
		void ClearProgress();
	private:
		void DrainPendingMessages();
		void RebuildVisibleMessages();
		void RenderMenu(const ImVec2& size);
		void RenderConsole(const ImVec2& size);
		const char* GetMessageType(const ConsoleMessage& message) const;
		const ImVec4& GetMessageColor(const ConsoleMessage& message) const;
		ImVec4 GetToolbarButtonColor(const bool value) const;

	private:
		static void PushMessage(const ConsoleMessage& message);

	private:
		const char* m_PanelName = "Log";
		bool m_ClearOnPlay = true;

		// UI thread only; other threads go through PushMessage.
		std::vector<ConsoleMessage> m_MessageBuffer;
		// Indices into m_MessageBuffer that pass the severity, tag and search filters. Rebuilt only when
		// the buffer or a filter changes, since matching the search against every message is not free.
		std::vector<uint32_t> m_VisibleMessages;
		bool m_VisibleMessagesDirty = true;

		std::string m_SearchQuery;
		std::set<std::string> m_KnownTags;  // Every tag seen this session, kept across Clear so hidden tags stay hidden.
		std::set<std::string> m_HiddenTags;

		bool m_EnableScrollToLatest = true;
		bool m_ScrollToLatest = false;
		float m_PreviousScrollY = 0.0f;

		int16_t m_MessageFilters = (int16_t)ConsoleMessageFlags::All;

		bool m_DetailedPanelOpen = false;
		std::string m_DetailedMessage;

		std::string m_ProgressLabel;
		float m_Progress = 0.0f;
	private:
		friend class EditorConsoleSink;
	};

}
