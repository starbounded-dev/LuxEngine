// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SplashScreen.h"

#include "Lux/Core/Version.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiFonts.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Utilities/FileDialogs.h"
#include "Lux/Utilities/FileSystem.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <format>

namespace Lux {

	namespace {

		constexpr float k_Width = 620.0f;
		constexpr float k_BannerHeight = 180.0f;
		constexpr float k_Padding = 18.0f;
		constexpr float k_Rounding = 6.0f;
		constexpr float k_AccentStripeHeight = 3.0f;
		constexpr float k_LabelWidth = 90.0f;
		constexpr float k_TitleFontSize = 40.0f;

		// The name becomes the C# namespace and assembly name (ProjectConfig::DefaultNamespace /
		// ScriptModulePath), so it has to be a valid identifier.
		bool IsValidProjectName(const char* name)
		{
			if (!name || !name[0])
				return false;
			if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_')
				return false;

			for (const char* c = name; *c; c++)
			{
				if (!std::isalnum(static_cast<unsigned char>(*c)) && *c != '_')
					return false;
			}
			return true;
		}

		std::string FormatLastOpened(time_t lastOpened)
		{
			if (lastOpened <= 0)
				return {};

			const auto seconds = static_cast<int64_t>(std::difftime(std::time(nullptr), lastOpened));
			if (seconds < 60)
				return "just now";
			if (seconds < 60 * 60)
				return std::format("{} min ago", seconds / 60);
			if (seconds < 60 * 60 * 24)
				return std::format("{} h ago", seconds / (60 * 60));

			const int64_t days = seconds / (60 * 60 * 24);
			if (days == 1)
				return "yesterday";
			if (days < 60)
				return std::format("{} days ago", days);
			if (days < 365)
				return std::format("{} months ago", days / 30);
			return std::format("{} years ago", days / 365);
		}

		void CopyToBuffer(char* buffer, size_t bufferSize, const std::string& value)
		{
			std::memset(buffer, 0, bufferSize);
			std::memcpy(buffer, value.data(), std::min(value.size(), bufferSize - 1));
		}

		void SectionHeader(const char* text)
		{
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker), "%s", text);
			ImGui::Spacing();
		}

	}

	SplashScreen::SplashScreen(Callbacks callbacks)
		: m_Callbacks(std::move(callbacks))
	{
	}

	void SplashScreen::Open()
	{
		if (m_Open)
			return;

		m_Open = true;
		m_OpenedFrame = ImGui::GetFrameCount();
		m_ShowCreateForm = false;
		m_CreateFailed = false;
	}

	void SplashScreen::Close()
	{
		m_Open = false;
		m_ShowCreateForm = false;
	}

	void SplashScreen::ShowCreateForm()
	{
		Open();
		m_ShowCreateForm = true;
		m_FocusNameInput = true;
		m_CreateFailed = false;

		if (m_NewProjectLocation[0])
			return;

		// Default the location to the folder that holds the most recent project, since that is where
		// people tend to keep their projects; otherwise the editor's working directory.
		std::filesystem::path location = FileSystem::GetWorkingDirectory();
		if (m_Callbacks.GetRecentProjects)
		{
			const std::vector<RecentProject> recentProjects = m_Callbacks.GetRecentProjects();
			if (!recentProjects.empty())
			{
				const std::filesystem::path projectsFolder = std::filesystem::path(recentProjects.front().FilePath).parent_path().parent_path();
				if (!projectsFolder.empty() && FileSystem::IsDirectory(projectsFolder))
					location = projectsFolder;
			}
		}

		CopyToBuffer(m_NewProjectLocation, sizeof(m_NewProjectLocation), location.lexically_normal().generic_string());
	}

	void SplashScreen::OnImGuiRender()
	{
		const bool projectOpen = !m_Callbacks.IsProjectOpen || m_Callbacks.IsProjectOpen();
		if (!projectOpen)
			Open();   // nothing to go back to without a project
		if (!m_Open)
			return;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(k_Width, 0.0f), ImVec2(k_Width, FLT_MAX));
		if (m_OpenedFrame == ImGui::GetFrameCount())
			ImGui::SetNextWindowFocus();

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;

		// Padding is zero so the banner runs edge to edge; it is popped straight after Begin so the
		// recent-project context menu keeps normal popup padding.
		ImGuiEx::ScopedColour windowColour(ImGuiCol_WindowBg, Colors::Theme::backgroundPopup);
		ImGuiEx::ScopedColour borderColour(ImGuiCol_Border, Colors::Theme::muted);
		ImGuiEx::ScopedStyle rounding(ImGuiStyleVar_WindowRounding, k_Rounding);
		ImGuiEx::ScopedStyle border(ImGuiStyleVar_WindowBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool visible = ImGui::Begin("##lux_splash", nullptr, flags);
		ImGui::PopStyleVar();

		if (visible)
		{
			DrawBanner(k_Width);

			ImGui::Dummy(ImVec2(0.0f, k_Padding));
			ImGui::Indent(k_Padding);
			if (m_ShowCreateForm)
				DrawCreateForm();
			else
				DrawHome();
			ImGui::Unindent(k_Padding);
			ImGui::Dummy(ImVec2(0.0f, k_Padding));

			// Like Blender's splash: Esc or a click anywhere else dismisses it. Esc on the form steps
			// back to the home page first. Neither applies while no project is open.
			if (m_Open && m_OpenedFrame != ImGui::GetFrameCount())
			{
				const bool clickedOutside = (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					&& !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);

				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && m_ShowCreateForm)
					m_ShowCreateForm = false;
				else if (projectOpen && (clickedOutside || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
					Close();
			}
		}
		ImGui::End();
	}

	void SplashScreen::DrawBanner(float width)
	{
		const ImVec2 bannerMin = ImGui::GetCursorScreenPos();
		const ImVec2 bannerMax(bannerMin.x + width, bannerMin.y + k_BannerHeight);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(bannerMin, bannerMax, Colors::Theme::titlebar, k_Rounding, ImDrawFlags_RoundCornersTop);
		drawList->AddRectFilled(ImVec2(bannerMin.x, bannerMax.y - k_AccentStripeHeight), bannerMax, Colors::Theme::accent);

		// Version in the top-right corner, as on Blender's splash image.
		const char* version = LUX_VERSION;
		const float versionWidth = ImGui::CalcTextSize(version).x;
		drawList->AddText(ImVec2(bannerMax.x - k_Padding - versionWidth, bannerMin.y + k_Padding), Colors::Theme::textDarker, version);

		{
			ImGuiEx::ScopedFont font(ImGuiEx::Fonts::Get("Display"), k_TitleFontSize);
			const float titleHeight = ImGui::GetTextLineHeight();
			ImGui::SetCursorScreenPos(ImVec2(bannerMin.x + k_Padding, bannerMax.y - k_AccentStripeHeight - k_Padding - titleHeight));
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textBrighter), "LuxEngine");
		}

		ImGui::SetCursorScreenPos(bannerMin);
		ImGui::Dummy(ImVec2(width, k_BannerHeight));
	}

	void SplashScreen::DrawHome()
	{
		const float contentWidth = k_Width - k_Padding * 2.0f;
		if (!ImGui::BeginTable("##splash_columns", 2, ImGuiTableFlags_None, ImVec2(contentWidth, 0.0f)))
			return;

		ImGui::TableSetupColumn("##splash_actions", ImGuiTableColumnFlags_WidthFixed, contentWidth * 0.36f);
		ImGui::TableSetupColumn("##splash_recent", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();

		// ---- Left: New / Open ----
		ImGui::TableSetColumnIndex(0);
		SectionHeader("New");
		if (ImGui::Selectable(LUX_ICON_PLUS "  New Project...##splash_new"))
			ShowCreateForm();

		ImGui::Dummy(ImVec2(0.0f, 10.0f));
		SectionHeader("Open");
		if (ImGui::Selectable(LUX_ICON_FOLDER_OPEN "  Open Project...##splash_open") && m_Callbacks.BrowseForProject)
		{
			if (m_Callbacks.BrowseForProject())
				Close();
		}

		// ---- Right: Recent ----
		ImGui::TableSetColumnIndex(1);
		SectionHeader("Recent Projects");

		const std::vector<RecentProject> recentProjects = m_Callbacks.GetRecentProjects ? m_Callbacks.GetRecentProjects() : std::vector<RecentProject>{};
		if (recentProjects.empty())
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::muted), "No recent projects");

		// Opening or removing a project changes the recent list, so both are applied after the loop.
		std::filesystem::path pendingOpen;
		std::filesystem::path pendingRemove;

		for (const RecentProject& recentProject : recentProjects)
		{
			const std::filesystem::path projectPath(recentProject.FilePath);
			const bool exists = FileSystem::Exists(projectPath);
			const std::string displayName = recentProject.Name.empty() ? projectPath.stem().string() : recentProject.Name;

			ImGuiEx::ScopedID rowID(recentProject.FilePath.c_str());
			{
				ImGuiEx::ScopedColour textColour(ImGuiCol_Text, exists ? Colors::Theme::text : Colors::Theme::muted);
				const std::string label = std::format("{}  {}##recent_project", LUX_ICON_FILE, displayName);
				if (ImGui::Selectable(label.c_str()) && exists)
					pendingOpen = projectPath;
			}

			if (ImGui::BeginItemTooltip())
			{
				ImGui::TextUnformatted(recentProject.FilePath.c_str());
				if (!exists)
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textError), "Project file not found");
				else if (const std::string lastOpened = FormatLastOpened(recentProject.LastOpened); !lastOpened.empty())
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker), "Opened %s", lastOpened.c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::BeginPopupContextItem("##recent_project_context"))
			{
				if (ImGui::MenuItem("Open", nullptr, false, exists))
					pendingOpen = projectPath;
				if (ImGui::MenuItem("Show in Explorer", nullptr, false, exists))
					FileSystem::ShowFileInExplorer(projectPath);
				ImGui::Separator();
				if (ImGui::MenuItem("Remove from List"))
					pendingRemove = projectPath;
				ImGui::EndPopup();
			}
		}

		ImGui::EndTable();

		if (!pendingRemove.empty() && m_Callbacks.RemoveRecentProject)
			m_Callbacks.RemoveRecentProject(pendingRemove);
		if (!pendingOpen.empty() && m_Callbacks.OpenProject)
		{
			Close();
			m_Callbacks.OpenProject(pendingOpen);
		}
	}

	std::string SplashScreen::ValidateCreate() const
	{
		if (!m_NewProjectName[0])
			return "Enter a project name.";
		if (!IsValidProjectName(m_NewProjectName))
			return "Use letters, digits and underscores, starting with a letter. The name is also the C# namespace.";
		if (!m_NewProjectLocation[0])
			return "Choose a location.";

		const std::filesystem::path location(m_NewProjectLocation);
		if (!FileSystem::IsDirectory(location))
			return "The location folder does not exist.";

		const std::filesystem::path projectDirectory = location / m_NewProjectName;
		if (FileSystem::Exists(projectDirectory))
		{
			std::error_code ec;
			if (!FileSystem::IsDirectory(projectDirectory) || !std::filesystem::is_empty(projectDirectory, ec) || ec)
				return std::format("'{}' already exists in that location and is not an empty folder.", m_NewProjectName);
		}

		return {};
	}

	void SplashScreen::DrawCreateForm()
	{
		const float contentWidth = k_Width - k_Padding * 2.0f;
		ImGuiEx::ScopedColour frameColour(ImGuiCol_FrameBg, Colors::Theme::propertyField);

		SectionHeader("New Project");

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Name");
		ImGui::SameLine(k_Padding + k_LabelWidth);
		if (m_FocusNameInput)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusNameInput = false;
		}
		ImGui::SetNextItemWidth(contentWidth - k_LabelWidth);
		const bool submitted = ImGui::InputTextWithHint("##new_project_name", "MyGame", m_NewProjectName, sizeof(m_NewProjectName), ImGuiInputTextFlags_EnterReturnsTrue);

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Location");
		ImGui::SameLine(k_Padding + k_LabelWidth);
		const float browseWidth = ImGui::CalcTextSize(LUX_ICON_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetNextItemWidth(contentWidth - k_LabelWidth - browseWidth - ImGui::GetStyle().ItemSpacing.x);
		ImGui::InputText("##new_project_location", m_NewProjectLocation, sizeof(m_NewProjectLocation));
		ImGui::SameLine();
		if (ImGui::Button(LUX_ICON_FOLDER_OPEN "##new_project_browse"))
		{
			const std::string folder = FileDialogs::OpenFolder();
			if (!folder.empty())
				CopyToBuffer(m_NewProjectLocation, sizeof(m_NewProjectLocation), std::filesystem::path(folder).lexically_normal().generic_string());
		}
		ImGui::SetItemTooltip("Browse for a folder");

		const std::string error = ValidateCreate();
		ImGui::Dummy(ImVec2(0.0f, 2.0f));
		ImGui::SetCursorPosX(k_Padding + k_LabelWidth);
		ImGui::PushTextWrapPos(k_Width - k_Padding);
		if (!error.empty())
		{
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(m_NewProjectName[0] ? Colors::Theme::textError : Colors::Theme::textDarker), "%s", error.c_str());
		}
		else
		{
			const std::filesystem::path projectFile = std::filesystem::path(m_NewProjectLocation) / m_NewProjectName / (std::string(m_NewProjectName) + ".luxproj");
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker), "Creates %s", projectFile.lexically_normal().generic_string().c_str());
		}
		if (m_CreateFailed)
		{
			ImGui::SetCursorPosX(k_Padding + k_LabelWidth);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textError), "The project could not be created. See the editor log for details.");
		}
		ImGui::PopTextWrapPos();

		ImGui::Dummy(ImVec2(0.0f, 6.0f));
		ImGui::SetCursorPosX(k_Padding + k_LabelWidth);

		bool create = false;
		{
			ImGuiEx::ScopedDisable disableWhenInvalid(!error.empty());
			create = ImGui::Button("Create##new_project", ImVec2(110.0f, 0.0f));
		}
		ImGui::SameLine();
		if (ImGui::Button("Back##new_project", ImVec2(110.0f, 0.0f)))
			m_ShowCreateForm = false;

		if ((create || submitted) && error.empty() && m_Callbacks.CreateProject)
		{
			m_CreateFailed = !m_Callbacks.CreateProject(m_NewProjectName, std::filesystem::path(m_NewProjectLocation));
			if (!m_CreateFailed)
			{
				m_NewProjectName[0] = 0;
				Close();
			}
		}
	}

}
