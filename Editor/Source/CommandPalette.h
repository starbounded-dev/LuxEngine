#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Lux {

	// A fuzzy-searchable command launcher (Ctrl+Shift+P). The editor registers every menu action and
	// panel toggle as a Command; the palette ranks them against the query and runs the chosen one.
	// Pure editor-side UI — it owns no engine state, only callbacks the editor hands it.
	class CommandPalette
	{
	public:
		struct Command
		{
			std::string Name;                 // shown, and matched against
			std::string Category;             // shown dim on the right (e.g. "File", "Panel")
			std::string Shortcut;             // shown dim (e.g. "Ctrl+S"); display only
			std::function<void()> Action;
			std::function<bool()> Enabled;    // optional; null => always enabled
		};

		void Register(Command command) { m_Commands.push_back(std::move(command)); }
		void Clear() { m_Commands.clear(); }

		void Open();
		void Close();
		void Toggle() { m_Open ? Close() : Open(); }
		bool IsOpen() const { return m_Open; }

		// Draw the overlay if open. Call once per frame during the editor's ImGui pass.
		void OnImGuiRender();

	private:
		void RebuildFiltered();

		std::vector<Command> m_Commands;
		std::vector<int> m_Filtered;          // ranked indices into m_Commands
		char m_Query[256] = {};
		std::string m_LastQuery = "\x01";     // sentinel forces an initial rebuild
		int m_Selected = 0;
		bool m_Open = false;
		bool m_FocusInput = false;
		bool m_ScrollToSelected = false;     // only true right after keyboard nav, so wheel scrolling is free
	};

}
