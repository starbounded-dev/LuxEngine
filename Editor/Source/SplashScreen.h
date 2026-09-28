// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Project/UserPreferences.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Lux {

	// Blender-style splash: a centered card over the (dimmed) editor with a banner, New/Open on the
	// left and recent projects on the right, plus the startup preferences in a footer. It shows at
	// startup (UserPreferences::ShowSplashScreen) and from Help → Splash Screen; clicking outside it
	// or pressing Esc dismisses it. With no project open it cannot be dismissed — it is the only way
	// forward. It replaces Hazel's separate launcher executable.
	// Pure editor-side UI — it owns no engine state, only callbacks the editor hands it.
	class SplashScreen
	{
	public:
		struct Callbacks
		{
			std::function<bool()> IsProjectOpen;
			std::function<std::vector<RecentProject>()> GetRecentProjects;
			std::function<void(const std::filesystem::path&)> OpenProject;
			std::function<bool()> BrowseForProject;   // false when the dialog was cancelled
			// Creates <location>/<name>/<name>.luxproj and opens it. Returns false on failure (logged).
			std::function<bool(const std::string& name, const std::filesystem::path& location)> CreateProject;
			std::function<void(const std::filesystem::path&)> RemoveRecentProject;
			std::function<bool()> GetShowOnStartup;
			std::function<void(bool)> SetShowOnStartup;
			std::function<bool()> GetReopenLastProject;
			std::function<void(bool)> SetReopenLastProject;
		};

		explicit SplashScreen(Callbacks callbacks);

		void Open();
		void Close();
		bool IsOpen() const { return m_Open; }

		// Opens the splash on its "New Project" form (File → New Project routes here).
		void ShowCreateForm();

		// Draws the splash if it is open, or unconditionally while no project is open. Call once per
		// frame on the main thread during the editor's ImGui pass, after the workspace. `topInset`
		// is the editor titlebar height: the dim backdrop starts below it so the menus stay readable.
		void OnImGuiRender(float topInset = 0.0f);

	private:
		void DrawHome(bool projectOpen);
		void DrawRecentProjects(float columnWidth, const std::vector<RecentProject>& recentProjects);
		void DrawFooter(bool projectOpen);
		void DrawCreateForm();
		std::string ValidateCreate() const;   // empty = valid; otherwise the reason it is not

	private:
		Callbacks m_Callbacks;

		float m_PopupItemSpacing[2] = { 8.0f, 4.0f };   // the style's spacing, restored inside context menus
		bool m_Open = false;
		int m_OpenedFrame = -1;               // the click that opened it must not also dismiss it
		bool m_ShowCreateForm = false;
		bool m_FocusNameInput = false;
		bool m_CreateFailed = false;
		char m_NewProjectName[128] = {};
		char m_NewProjectLocation[512] = {};
	};

}
