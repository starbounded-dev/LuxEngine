// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "WorkspaceDocking.h"
#include "WorkspaceStore.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Lux
{
	class PanelManager;
	struct PanelData;

	class EditorWorkspaceManager
	{
	public:
		explicit EditorWorkspaceManager(const std::filesystem::path& directory);
		~EditorWorkspaceManager();
		void PrimeImGui();
		void Initialize(PanelManager& panels, bool simple);
		void BeforeFrame();
		void DrawDockSpace(ImGuiID root, ImVec2 size);
		void DrawPopups();
		bool RequestShortcut(size_t slot);
		void DrawMenu();
		void DrawPanelsMenu();
		void Shutdown();
		void RequestReset();
		void RequestDefault(bool simple);
		void SetEnabled(bool enabled)
		{
			m_Enabled = enabled;
		}
		void BeginPlay();
		bool EndPlay();
#ifndef LUX_DIST
		static bool RunSelfTests(const std::filesystem::path& directory, std::vector<std::string>& failures);
#endif

	private:
		struct Layout
		{
			std::string ID;
			std::string Name;
			std::string Origin;
			std::string Ini;
			std::string Panels;
			std::vector<WorkspaceDocking::SplitRatio> Ratios;
			bool Factory = false;
			bool Modified = false;
			bool Deleted = false;
			std::string PreservedPanels;
			std::string LegacyPath;
		};
		enum class Action
		{
			Switch,
			Copy,
			Rename,
			Delete,
			Reset,
			Restore,
			UndoReset,
			Recover
		};
		struct Request
		{
			Action Type;
			std::string ID;
			std::string Name;
		};
		Layout* Find(const std::string& id);
		void Load(const std::string& text);
		void Capture();
		void Apply(Layout& layout);
		void BuildDefault(const std::string& id, ImGuiID root, ImVec2 size);
		void Save();
		void RequestName(Action action, const Layout& layout);
		void DrawNamePopup();
		void DrawLayoutMenuItem(const Layout& layout, Action action, const char* shortcut = nullptr);
		std::string CaptureIni() const;
		std::filesystem::path m_Directory;
		std::string m_DiskText;
		std::vector<Layout> m_Layouts;
		std::string m_Active = "default.layout";
		std::optional<Request> m_Request;
		std::optional<Layout> m_ResetUndo;
		Scope<WorkspaceStore> m_Store;
		PanelManager* m_Panels = nullptr;
		WorkspaceDocking m_Docking;
		std::vector<std::pair<ImGuiID, ImGuiID>> m_PendingDockTabs;
		std::vector<PanelData*> m_PanelMenu;
		std::string m_Error;
		char m_Name[256]{};
		char m_Search[128]{};
		std::string m_EditID;
		Action m_NameAction = Action::Copy;
		bool m_OpenName = false;
		bool m_Enabled = false;
		bool m_Loaded = false;
		bool m_Applied = false;
		bool m_MigrationComplete = false;
		bool m_ReadOnly = false;
		bool m_FutureVersion = false;
		bool m_Rebuild = false;
		bool m_HasDrawn = false;
		bool m_Playing = false;
		bool m_GameWasOpen = false;
		bool m_SceneWasOpen = true;
		uint64_t m_Revision = 0;
		uint64_t m_PanelRevision = 0;
	};
} // namespace Lux
