// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <functional>
#include <string_view>

struct ImRect;

namespace Lux {

	// The editor's offline manual (Editor/Resources/Docs, shown by the Help panel). Topics are named
	// by their path under that folder without ".md" ("components/rigid-body"). Engine-side editor UI
	// (the inspector) asks for a topic here; the editor application, which owns the Help panel,
	// installs the handler. Main thread only.
	namespace EditorDocs {

		void SetOpenTopicHandler(std::function<void(std::string_view topic)> handler);
		// False when no handler is installed (Lux-Runtime, or before the editor sets one).
		bool OpenTopic(std::string_view topic);

	}

	namespace ImGuiEx {

		// A small help icon over `rect` that opens `topic` in the Help panel; hovering names the page.
		// Nothing is drawn when no Help panel is installed. True when clicked.
		bool DocsButton(const char* strID, std::string_view topic, std::string_view title, const ImRect& rect);

	}

}
