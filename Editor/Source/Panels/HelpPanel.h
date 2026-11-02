// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/ImGui/ImGuiMarkdown.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Lux {

	// The editor's offline manual: the Markdown pages under Resources/Docs, read once at startup and
	// shown with a topic list, search over titles and text, links between pages and back/forward.
	// Topics are named by path without ".md" (EditorDocs). Nothing here needs a network.
	class HelpPanel : public EditorPanel
	{
	public:
		static constexpr std::string_view HomeTopic = "index";

		HelpPanel();

		virtual void OnImGuiRender(bool& isOpen) override;

		// Shows `topic` as the newest history entry and brings the window forward on its next draw.
		void OpenTopic(std::string_view topic);

		virtual void SaveState(Yaml::Writer& out) const override;
		virtual void LoadState(const Yaml::Node& state) override;
	private:
		struct Topic
		{
			std::string ID;
			std::string Title;
			std::string Section;
			std::string Text;
			std::string SearchText; // title and text, lower case
		};

		void LoadTopics();
		const Topic* FindTopic(std::string_view id) const;
		// A link as written in the current page, as a topic ID: relative to the page's folder when
		// that names a topic, otherwise from the manual's root.
		std::string ResolveLink(std::string_view link) const;
		void Navigate(std::string topic, bool addToHistory);
		void OnLink(std::string_view link);

		void UI_Toolbar();
		void UI_TopicList();
		void UI_Page();
	private:
		std::filesystem::path m_Root;
		std::vector<Topic> m_Topics; // sorted by section, then title
		bool m_RootMissing = false;

		std::string m_CurrentTopic;
		ImGuiEx::MarkdownDocument m_Document;
		std::vector<std::string> m_History;
		size_t m_HistoryIndex = 0;

		std::string m_Search;
		bool m_ScrollToTop = false;
		bool m_FocusRequested = false;
	};

}
