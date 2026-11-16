// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ContentBrowserPanel.h"

#ifndef LUX_DIST
#include "Lux/Asset/AssetManager.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/ImGui/ImGuiScaling.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui_internal.h>
#include <stdexcept>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace Lux
{
	namespace
	{
		void Require(bool condition, const char* message)
		{
			if (!condition)
				throw std::runtime_error(message);
		}
		ImGuiWindow* Child(const char* label)
		{
			for (auto* window : GImGui->Windows)
				if (window->Active && std::strstr(window->Name, label))
					return window;
			return nullptr;
		}
	} // namespace

	bool ContentBrowserPanel::RunSelfTests(const std::filesystem::path& directory,
										   std::vector<std::string>& failures)
	{
#ifdef _MSC_VER
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext();
		ImGui::SetCurrentContext(context);
		auto& panel = Get();
		const bool thumbnailsPaused = s_ThumbnailWorkPaused;
		s_ThumbnailWorkPaused = true;
		try
		{
			Require(Project::GetActive() && Project::GetActive()->GetProjectDirectory() == directory,
					"Browser tests require the disposable runner project");
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = {1920.0f, 1080.0f};
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigDebugHighlightIdConflicts = true;
			io.Fonts->AddFontDefault();
			unsigned char* pixels = nullptr;
			int atlasWidth = 0, atlasHeight = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
			auto manager = Project::GetEditorAssetManager();
			const AssetHandle first = AssetManager::ImportAsset("One/Duplicate.cs");
			const AssetHandle second = AssetManager::ImportAsset("Two/Duplicate.cs");
			Require(first && second && first != second,
					"Duplicate basename fixture did not retain distinct handles");
			panel.Refresh();
			panel.ChangeDirectory(panel.GetDirectory("One"));
			panel.OnBrowseBack();
			Require(panel.m_CurrentDirectory == panel.m_BaseDirectory && panel.m_NextDirectory,
					"Back lost its forward destination");
			panel.OnBrowseForward();
			Require(panel.m_CurrentDirectory->FilePath == "One", "Forward did not return to its destination");

			auto frame = [&](float width)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({width, 600.0f});
				bool open = true;
				panel.OnImGuiRender(open);
				ImGui::Render();
			};
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (const float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				panel.m_FolderFraction = 0.32f;
				for (const float width : {1100.0f, 280.0f, 1100.0f})
				{
					frame(width);
					frame(width);
					const auto* items = Child("##content_browser_items");
					const auto* footer = Child("##content_browser_bottombar");
					Require(items && footer && items->Size.y > ImGui::GetFontSize() &&
								footer->ClipRect.GetHeight() >= ImGui::GetFrameHeight() * 0.8f &&
								footer->Pos.y >= items->Pos.y + items->Size.y,
							"The browser item region consumed or overlapped its footer");
					const auto* browser = ImGui::FindWindowByName("Content Browser");
					Require(browser->ContentSize.x <= browser->Size.x && panel.m_FolderFraction == 0.32f,
							"Narrow reflow overflowed the toolbar or overwrote the folder proportion");
					Require((Child("##content_browser_folders") != nullptr) == (width > 1000.0f),
							"The folder sidebar did not adapt to available space");
				}
			}
			panel.m_ViewMode = ViewMode::List;
			frame(280.0f);
			std::strcpy(panel.m_SearchBuffer, "unmatched-query");
			panel.RebuildItems();
			frame(280.0f);
			Require(panel.m_VisibleItems == 0 && panel.GetSelectionCount() == 0,
					"Empty search retained stale selection/results");
			panel.m_SearchBuffer[0] = '\0';
			panel.RebuildItems();
			panel.m_IsContentBrowserFocused = true;
			io.WantTextInput = true;
			KeyPressedEvent typedDelete(Key::Delete, 0);
			Require(panel.OnKeyPressedEvent(typedDelete) && !panel.m_OpenDeletePopup,
					"Editing browser text allowed an asset/scene command");
			io.WantTextInput = false;
			panel.LoadState(Yaml::Load("FolderFraction: 0.9\n"));
			Require(panel.m_FolderFraction == 0.9f,
					"Restoring a valid folder proportion changed user intent");
			panel.LoadState(Yaml::Load("ShowFolders: false\nFolderFraction: .nan\n"));
			Yaml::Writer state;
			state << Yaml::BeginMap;
			panel.SaveState(state);
			state << Yaml::EndMap;
			Require(!Yaml::Load(state.str())["ShowFolders"].as<bool>() && panel.m_FolderFraction == 0.2f,
					"Browser view choices failed to round-trip or accepted invalid sizing");

			AssetHandle picked = 0;
			bool clear = false, changed = false;
			auto pickerFrame = [&](bool open = false)
			{
				ImGui::NewFrame();
				ImGui::Begin("Browser picker test");
				if (open)
					ImGui::OpenPopup("Picker");
				changed = ImGuiEx::Widgets::AssetSearchPopup("Picker", AssetType::ScriptFile, picked, &clear,
															 "Search", {360.0f, 200.0f});
				ImGui::End();
				ImGui::Render();
			};
			pickerFrame(true);
			pickerFrame();
			io.AddInputCharactersUTF8("Two/");
			pickerFrame();
			io.AddKeyEvent(ImGuiKey_Enter, true);
			pickerFrame();
			Require(changed && picked == second, "Path search did not distinguish duplicate asset basenames");
			io.AddKeyEvent(ImGuiKey_Enter, false);
			pickerFrame();
			std::vector<std::function<void()>> undo;
			panel.SetUndoPush([&](const std::string&, std::function<void()> before, std::function<void()>)
							  { undo.push_back(std::move(before)); });
			pickerFrame(true);
			pickerFrame();
			const auto revision = manager->GetAssetRegistry().GetRevision();
			Require(panel.RenameAsset(second, "Renamed") && undo.size() == 1 &&
						manager->GetAssetRegistry().GetRevision() > revision,
					"Asset rename did not preserve its identity and publish metadata invalidation");
			io.AddInputCharactersUTF8("Renamed");
			pickerFrame();
			io.AddKeyEvent(ImGuiKey_Enter, true);
			pickerFrame();
			Require(changed && picked == second,
					"An open picker retained stale labels after an asset rename");
			io.AddKeyEvent(ImGuiKey_Enter, false);
			pickerFrame();
			undo.back()();
			Require(manager->GetMetadata(second).FilePath == "Two/Duplicate.cs",
					"Browser rename undo lost the original path or asset identity");
			panel.SetUndoPush({});
			Require(Utils::PathToUTF8(std::filesystem::path(u8"Folder/\u00e9\u754c.txt")) ==
						"Folder/\xc3\xa9\xe7\x95\x8c.txt",
					"Portable UI path labels did not preserve UTF-8");
			panel.m_CopiedAssets.Select(second);
			panel.OnProjectChanged(nullptr);
			Require(panel.m_CurrentItems.Items.empty() && panel.m_FolderRoots.empty() &&
						panel.m_LocationLabel.empty() && !panel.m_NextDirectory &&
						!panel.GetSelectionCount() && panel.m_CopiedAssets.SelectionCount() == 0,
					"Project change retained browser context from the previous project");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		panel.SetUndoPush({});
		s_ThumbnailWorkPaused = thumbnailsPaused;
		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);
		return failures.empty();
	}
} // namespace Lux
#endif
