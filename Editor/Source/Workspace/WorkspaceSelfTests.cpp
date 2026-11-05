// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "EditorWorkspaceManager.h"

#ifndef LUX_DIST
#include "Lux/Core/Log.h"
#include "Lux/Editor/PanelManager.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/ImGuiScaling.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/FileSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace Lux
{
	namespace
	{
		class WorkspaceTestPanel : public EditorPanel
		{
		public:
			explicit WorkspaceTestPanel(const char* name) : Name(name)
			{
			}
			void OnImGuiRender(bool& open) override
			{
				ImGui::Begin(GetWindowName(), &open);
				ImGui::TextUnformatted("Workspace test");
				ImGui::End();
			}
			const char* GetWindowName() const override
			{
				return std::string_view(Name) == "Profiler" ? LUX_ICON_TACHOMETER "  Profiler" : Name;
			}
			void OnClose() override
			{
				++Closed;
			}
			void SaveState(Yaml::Writer& out) const override
			{
				out << Yaml::Key << "Section" << Yaml::Value << Section;
			}
			void LoadState(const Yaml::Node& node) override
			{
				Section = node["Section"].as<int>(0);
			}
			const char* Name;
			int Section = 0;
			int DirtyDocument = 0;
			int Closed = 0;
		};

		void Require(bool condition, const char* message)
		{
			if (!condition)
				throw std::runtime_error(message);
		}

		void Frame(EditorWorkspaceManager& manager, PanelManager& panels, ImVec2 size = {1920, 1080})
		{
			manager.BeforeFrame();
			ImGui::GetIO().DisplaySize = size;
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({0, 0});
			ImGui::SetNextWindowSize(size);
			ImGui::Begin("Lux Editor", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
			manager.DrawPopups();
			manager.DrawDockSpace(ImGui::GetID("MyDockSpace"), ImGui::GetContentRegionAvail());
			panels.OnImGuiRender();
			for (const auto& [id, panel] : panels.GetPanels(PanelCategory::View))
			{
				if (!panel.IsOpen)
					continue;
				const auto* window = ImGui::FindWindowByName(panel.GetWindowName());
				Require(window && window->DockId != 0 && window->DockNode,
						"Workspace restore lost a docked window");
			}
			ImGui::End();
			ImGui::Render();
		}

		void WaitForStore(WorkspaceStore& store, uint64_t revision)
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (store.GetSavedRevision() < revision && store.GetError().empty() &&
				   std::chrono::steady_clock::now() < deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			Require(store.GetSavedRevision() >= revision,
					"Workspace writer did not publish its requested revision");
		}
	} // namespace

	bool EditorWorkspaceManager::RunSelfTests(const std::filesystem::path& directory,
											  std::vector<std::string>& failures)
	{
		ImGuiContext* previous = ImGui::GetCurrentContext();
#ifdef _MSC_VER
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
		ImGuiContext* context = ImGui::CreateContext();
		ImGui::SetCurrentContext(context);
		try
		{
			Require(!FileSystem::Exists(directory),
					"Self-test directory must be new; existing data is never overwritten");
			Require(FileSystem::CreateDirectory(directory), "Cannot create workspace test directory");
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.Fonts->AddFontDefault();
			unsigned char* pixels = nullptr;
			int width = 0, height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
			io.DisplaySize = {1920, 1080};
			PanelManager panels;
			for (const char* name :
				 {"Scene View", "Game View", "Scene Hierarchy", "Properties", "Content Browser", "Log",
				  "Material Editor", "Audio Mixer", "Sound Bank Editor", "Sound Editor", "Beam", "Profiler",
				  "Renderer Debugger", "Scene Renderer", "Light Settings"})
			{
				const char* id = std::string_view(name) == "Scene View"	 ? "SceneViewPanel"
								 : std::string_view(name) == "Game View" ? "GameViewPanel"
																		 : name;
				panels.AddPanel<WorkspaceTestPanel>(PanelCategory::View, id, name, true, name);
			}
			auto document = panels.GetPanel<WorkspaceTestPanel>("Beam");
			std::string personal;
			{
				EditorWorkspaceManager manager(directory);
				LUX_CORE_INFO_TAG("Editor", "Workspace test: initialize defaults");
				manager.Initialize(panels, true);
				manager.SetEnabled(true);
				Frame(manager, panels);
				Frame(manager, panels);
				Require(!manager.RequestShortcut(0) && !manager.RequestShortcut(10),
						"Workspace shortcuts accepted a slot outside Ctrl+1 through Ctrl+9");
				manager.SetEnabled(false);
				Require(!manager.RequestShortcut(2), "Disabled workspaces accepted a shortcut");
				manager.SetEnabled(true);
				size_t shortcut = 0;
				for (const char* id : {"default.layout", "default.shading", "default.audio",
									   "default.scripting", "default.debugging"})
				{
					Require(manager.RequestShortcut(++shortcut), "A default workspace shortcut was missing");
					Frame(manager, panels);
					Frame(manager, panels);
					// New dock tabs apply NextSelectedTabId on the following frame.
					Frame(manager, panels);
					Require(manager.m_Active == id, "Workspace shortcut order differed from the menu");
				}
				Require(!manager.RequestShortcut(6),
						"An empty shortcut selected a nonexistent personal layout");
				manager.RequestShortcut(1);
				Frame(manager, panels);
				LUX_CORE_INFO_TAG("Editor", "Workspace test: copy default");
				Require(manager.m_Active == "default.layout", "Clean startup did not select Layout");
				manager.m_Request = Request{Action::Copy, manager.m_Active, "Personal Test"};
				Frame(manager, panels);
				LUX_CORE_INFO_TAG("Editor", "Workspace test: copy rendered");
				personal = manager.m_Active;
				Require(!manager.Find(personal)->Factory, "Default copy did not become personal");
				const std::string savedName = manager.Find(personal)->Name;
				manager.Find(personal)->Name = "Personal ## literal 100% " + std::string(300, 'L');
				for (int frame = 0; frame < 3; ++frame)
				{
					ImGui::NewFrame();
					ImGui::Begin("Workspace menu sizing probe", nullptr,
								 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
					manager.DrawMenu();
					ImGui::End();
					ImGui::Render();
				}
				Require(ImGui::FindWindowByName("Workspace menu sizing probe")->ContentSize.x <=
							ImGui::GetFontSize() * 40.0f,
						"A long personal name made the workspace menu unbounded");
				manager.Find(personal)->Name = savedName;
				const ImGuiStyle originalStyle = ImGui::GetStyle();
				ImGuiEx::DpiStyle dpi;
				dpi.Capture(originalStyle);
				Layout choice = *manager.Find(personal);
				choice.ID = "personal.menu-probe";
				ImGuiID choiceID = 0;
				ImRect choiceRect;
				int choiceVertices = 0;
				auto choiceFrame = [&](float width, Action action = Action::Switch)
				{
					ImGui::NewFrame();
					ImGui::SetNextWindowPos({0.0f, 0.0f});
					ImGui::SetNextWindowSize({width, 260.0f});
					ImGui::Begin("Layout choice probe", nullptr, ImGuiWindowFlags_NoSavedSettings);
					const int before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
					manager.DrawLayoutMenuItem(choice, action);
					choiceVertices = ImGui::GetWindowDrawList()->VtxBuffer.Size - before;
					choiceID = ImGui::GetItemID();
					choiceRect = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
					ImGui::End();
					ImGui::Render();
				};
				for (float scale : {1.0f, 1.5f})
				{
					dpi.Apply(ImGui::GetStyle(), scale);
					ImGui::GetStyle().FontScaleMain = scale;
					io.AddMousePosEvent(-1000.0f, -1000.0f);
					choice.Name = "Personal ## literal 100%";
					choiceFrame(650.0f);
					choiceFrame(650.0f);
					const int visibleCharacters =
						static_cast<int>(std::count_if(choice.Name.begin(), choice.Name.end(),
													   [](char character) { return character != ' '; }));
					Require(choiceVertices >= visibleCharacters * 4,
							"The layout row hid literal text after ##");
					const ImGuiID stableID = choiceID;
					choice.Name = std::string(300, 'L') + " ## renamed";
					choiceFrame(260.0f);
					choiceFrame(260.0f);
					Require(choiceID == stableID, "Renaming a layout changed its menu widget identity");
					const auto* window = ImGui::FindWindowByName("Layout choice probe");
					Require(window->ContentSize.x <= window->Size.x,
							"A long layout row overflowed its available width");
					io.AddMousePosEvent(choiceRect.GetCenter().x, choiceRect.GetCenter().y);
					choiceFrame(260.0f);
					io.AddMouseButtonEvent(0, true);
					choiceFrame(260.0f);
					io.AddMouseButtonEvent(0, false);
					choiceFrame(260.0f);
					Require(manager.m_Request && manager.m_Request->Type == Action::Switch &&
								manager.m_Request->ID == choice.ID,
							"A layout row click selected the wrong stable record");
					manager.m_Request.reset();
					choiceFrame(260.0f, Action::Restore);
					io.AddMouseButtonEvent(0, true);
					choiceFrame(260.0f, Action::Restore);
					io.AddMouseButtonEvent(0, false);
					choiceFrame(260.0f, Action::Restore);
					Require(manager.m_Request && manager.m_Request->Type == Action::Restore &&
								manager.m_Request->ID == choice.ID,
							"A deleted-layout row queued the wrong action");
					manager.m_Request.reset();
				}
				ImGui::GetStyle() = originalStyle;
				std::snprintf(manager.m_Search, sizeof(manager.m_Search), "%s", "no-matching-panel");
				ImRect clearRect;
				float searchHeight = 0.0f;
				auto searchFrame = [&]()
				{
					ImGui::NewFrame();
					ImGui::SetNextWindowPos({0.0f, 400.0f});
					ImGui::SetNextWindowSize({650.0f, 300.0f});
					ImGui::Begin("Panel search probe", nullptr, ImGuiWindowFlags_NoSavedSettings);
					const float before = ImGui::GetCursorPosY();
					manager.DrawPanelsMenu();
					searchHeight = ImGui::GetCursorPosY() - before;
					clearRect = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
					ImGui::End();
					ImGui::Render();
				};
				io.AddMousePosEvent(-1000.0f, -1000.0f);
				searchFrame();
				searchFrame();
				Require(searchHeight > ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeight(),
						"An unmatched panel search did not draw feedback below the input");
				io.AddMousePosEvent(clearRect.GetCenter().x, clearRect.GetCenter().y);
				searchFrame();
				io.AddMouseButtonEvent(0, true);
				searchFrame();
				io.AddMouseButtonEvent(0, false);
				searchFrame();
				Require(!manager.m_Search[0], "Clearing an unmatched panel search retained the query");
				io.AddMousePosEvent(-1000.0f, -1000.0f);
				document->DirtyDocument = 42;
				document->Section = 7;
				Require(manager.RequestShortcut(2), "Shading shortcut was unavailable");
				Frame(manager, panels);
				Frame(manager, panels);
				Require(document->DirtyDocument == 42 &&
						panels.GetPanel<WorkspaceTestPanel>("Beam").Raw() == document.Raw(),
						"Workspace shortcut discarded an unsaved document");
				Require(manager.RequestShortcut(6),
						"Personal layout did not follow defaults in shortcut order");
				Frame(manager, panels);
				Frame(manager, panels);
				Require(manager.m_Active == personal, "Personal layout shortcut selected the wrong record");
				const size_t savedCount = manager.m_Layouts.size();
				for (size_t slot = 7; slot <= 10; ++slot)
				{
					Layout extra = *manager.Find(personal);
					extra.ID = "personal.shortcut." + std::to_string(slot);
					manager.m_Layouts.push_back(std::move(extra));
				}
				for (size_t slot = 7; slot <= 9; ++slot)
				{
					Require(manager.RequestShortcut(slot), "A personal shortcut through Ctrl+9 was missing");
					Require(manager.m_Request->ID == "personal.shortcut." + std::to_string(slot),
							"A personal shortcut selected the wrong saved record");
				}
				Require(!manager.RequestShortcut(10), "A tenth layout acquired an unsupported shortcut");
				manager.m_Request.reset();
				manager.m_Layouts.resize(savedCount);
				panels.SetOpen(Hash::GenerateFNVHash("Properties"), false);
				Frame(manager, panels);
				for (const char* id : {"default.shading", "default.audio", "default.scripting",
									   "default.debugging", "default.layout"})
				{
					manager.m_Request = Request{Action::Switch, id, {}};
					Frame(manager, panels);
					Frame(manager, panels);
					Frame(manager, panels);
					Require(manager.m_Active == id, "Factory workspace did not activate");
					if (std::string_view(id) == "default.shading")
					{
						const auto* scene = ImGui::FindWindowByName("Scene View");
						Require(scene->DockNode->SelectedTabId == scene->TabId,
								"Shading workspace did not select its scene preview");
					}
					if (std::string_view(id) == "default.debugging")
					{
						const char* title = panels.GetPanelData(Hash::GenerateFNVHash("Profiler"))->GetWindowName();
						const auto* profiler = ImGui::FindWindowByName(title);
						Require(profiler && profiler->DockNode && profiler->DockNode->SelectedTabId == profiler->TabId,
							"Debugging did not dock and select the Profiler's actual window title");
						Layout legacy = *manager.Find(manager.m_Active);
						legacy.Ini = manager.CaptureIni();
						const std::string actualHeader = "[Window][" + std::string(title) + "]";
						const size_t header = legacy.Ini.find(actualHeader);
						Require(header != std::string::npos, "Workspace capture omitted the Profiler's actual title");
						legacy.Ini.replace(header, actualHeader.size(), "[Window][Profiler]");
						manager.Apply(legacy);
						Frame(manager, panels);
						Frame(manager, panels);
						Require(ImGui::FindWindowByName(title)->DockNode != nullptr,
							"A legacy display-name placeholder did not recover the actual Profiler dock");
					}
					Require(document->DirtyDocument == 42, "Workspace switch discarded a dirty document");
				}
				manager.m_Request = Request{Action::Switch, personal, {}};
				Frame(manager, panels);
				Require(!panels.GetPanelData(Hash::GenerateFNVHash("Properties"))->IsOpen,
						"Personal visibility leaked between workspaces");
				Require(document->Section == 7, "Personal presentation state was not restored");
				Require(panels.GetPanel<WorkspaceTestPanel>("Beam").Raw() == document.Raw(),
						"Workspace switching recreated a panel");
				manager.m_Request = Request{Action::Rename, personal, "Renamed / Unicode \xC3\xA9"};
				Frame(manager, panels);
				Require(manager.Find(personal)->Name == "Renamed / Unicode \xC3\xA9",
						"Rename changed identity or rejected a data-only name");
				for (int cycle = 0; cycle < 20; ++cycle)
				{
					Frame(manager, panels, {3840, 2160});
					Frame(manager, panels, {1920, 1080});
				}
				manager.m_Request = Request{Action::Reset, personal, {}};
				Frame(manager, panels);
				manager.m_Request = Request{Action::UndoReset, personal, {}};
				Frame(manager, panels);
				Require(!panels.GetPanelData(Hash::GenerateFNVHash("Properties"))->IsOpen,
						"Undo reset did not recover the personal arrangement");
				manager.m_Request = Request{Action::Delete, personal, {}};
				Frame(manager, panels);
				Require(manager.m_Active == "default.layout" && manager.Find(personal)->Deleted,
						"Delete left no reachable workspace");
				Require(!manager.RequestShortcut(6), "Deleted layouts still occupied a workspace shortcut");
				manager.m_Request = Request{Action::Restore, personal, {}};
				Frame(manager, panels);
				manager.BeginPlay();
				panels.SetOpen(Hash::GenerateFNVHash("GameViewPanel"), true);
				Frame(manager, panels);
				manager.EndPlay();
				manager.Shutdown();
			}
			const auto saved = WorkspaceStore::Read(directory / "Workspaces.yaml");
			Require(!saved.empty(), "Shutdown did not flush the personal layout");
			{
				EditorWorkspaceManager manager(directory);
				Require(manager.m_Loaded && !manager.m_ReadOnly && manager.m_Active == personal,
						"Restart did not load the active personal layout");
				Require(manager.Find("default.layout")->Ini.empty(),
						"Temporary factory edits were persisted");
				manager.Initialize(panels, true);
				manager.SetEnabled(true);
				Frame(manager, panels);
				Require(!panels.GetPanelData(Hash::GenerateFNVHash("Properties"))->IsOpen,
						"Restart lost personal visibility");
				manager.Shutdown();
			}
			const auto conflictPath = directory / "conflict.yaml";
			{
				WorkspaceStore writer(conflictPath, "");
				writer.Queue("first", 1);
				WaitForStore(writer, 1);
				Require(FileSystem::WriteFileAtomically(conflictPath, "external", true),
						"Cannot stage external modification");
				writer.Queue("this session", 2);
			}
			Require(WorkspaceStore::Read(conflictPath) == "external",
					"Concurrent save overwrote another editor's change");
			const auto invalid = directory / "future";
			Require(FileSystem::CreateDirectory(invalid), "Cannot create future-schema fixture");
			Require(FileSystem::WriteFileAtomically(invalid / "Workspaces.yaml",
													"Version: 999\nLayouts: []\n", true),
					"Cannot stage future schema");
			{
				EditorWorkspaceManager manager(invalid);
				Require(manager.m_ReadOnly && !manager.m_Store, "Future schema was not protected");
			}
			Require(WorkspaceStore::Read(invalid / "Workspaces.yaml") == "Version: 999\nLayouts: []\n",
					"Future schema was overwritten");
			Require(!WorkspaceDocking::ValidateIni(
						"[Docking][Data]\nDockSpace ID=0x01 Split=X\nDockNode ID=0x02 Parent=0x02\n"),
					"Invalid dock parent graph was accepted");
			const auto corrupt = directory / "corrupt";
			Require(FileSystem::CreateDirectory(corrupt), "Cannot create recovery fixture");
			const std::string rejected = "broken: [yaml\n";
			Require(FileSystem::WriteFileAtomically(corrupt / "Workspaces.yaml", rejected, true),
					"Cannot stage corrupt store");
			Require(FileSystem::WriteFileAtomically(corrupt / "Workspaces.yaml.previous", saved, true),
					"Cannot stage recovery store");
			{
				EditorWorkspaceManager manager(corrupt);
				Require(manager.m_Loaded && manager.m_ReadOnly && manager.Find(personal),
						"Last-good recovery was not loaded safely");
				manager.Initialize(panels, true);
				manager.SetEnabled(true);
				Frame(manager, panels);
				Require(WorkspaceStore::Read(corrupt / "Workspaces.yaml") == rejected,
						"Recovery silently overwrote the original");
				manager.m_Request = Request{Action::Recover, manager.m_Active, {}};
				Frame(manager, panels);
				manager.Shutdown();
			}
			Require(Yaml::Load(WorkspaceStore::Read(corrupt / "Workspaces.yaml"))["Version"].as<int>() == 1,
					"Explicit recovery did not publish a valid store");
			bool preserved = false;
			for (const auto& entry : std::filesystem::directory_iterator(corrupt))
				if (entry.path().filename().string().find(".rejected.") != std::string::npos)
					preserved |= WorkspaceStore::Read(entry.path()) == rejected;
			Require(preserved, "Recovery did not preserve the rejected original");
			const auto legacy = directory / "legacy";
			Require(FileSystem::CreateDirectory(legacy / "Layouts" / "Old Layout"),
					"Cannot create migration fixture");
			std::string legacyIni, legacyPanels;
			for (const auto& node : Yaml::Load(saved)["Layouts"])
				if (node["ID"].as<std::string>() == personal)
				{
					legacyIni = node["Ini"].as<std::string>();
					legacyPanels = node["Panels"].as<std::string>();
				}
			const auto invalidPanels = directory / "invalid-preserved-panels";
			Require(FileSystem::CreateDirectory(invalidPanels), "Cannot create panel recovery fixture");
			Yaml::Writer invalidStore;
			invalidStore << Yaml::BeginMap << Yaml::Key << "Version" << Yaml::Value << 1;
			invalidStore << Yaml::Key << "Active" << Yaml::Value << personal;
			invalidStore << Yaml::Key << "Layouts" << Yaml::Value << Yaml::BeginSeq << Yaml::BeginMap;
			invalidStore << Yaml::Key << "ID" << Yaml::Value << personal;
			invalidStore << Yaml::Key << "Name" << Yaml::Value << "Damaged panel state";
			invalidStore << Yaml::Key << "Ini" << Yaml::Value << legacyIni;
			invalidStore << Yaml::Key << "Panels" << Yaml::Value << legacyPanels;
			invalidStore << Yaml::Key << "PreservedPanels" << Yaml::Value << "broken: [yaml";
			invalidStore << Yaml::EndMap << Yaml::EndSeq << Yaml::EndMap;
			const auto invalidText = invalidStore.str();
			Require(FileSystem::WriteFileAtomically(invalidPanels / "Workspaces.yaml", invalidText, true),
					"Cannot stage damaged preserved panel state");
			{
				EditorWorkspaceManager manager(invalidPanels);
				Require(manager.m_ReadOnly && !manager.m_Loaded,
						"Damaged preserved panel state was accepted for startup");
				manager.Initialize(panels, true);
				manager.SetEnabled(true);
				Frame(manager, panels);
				manager.Shutdown();
			}
			Require(WorkspaceStore::Read(invalidPanels / "Workspaces.yaml") == invalidText,
					"Rejected panel state was overwritten");
			Require(FileSystem::WriteFileAtomically(legacy / "imgui.ini", legacyIni, true),
					"Cannot stage legacy ini");
			Require(FileSystem::WriteFileAtomically(legacy / "EditorLayout.yaml", legacyPanels, true),
					"Cannot stage legacy panel state");
			Require(FileSystem::WriteFileAtomically(legacy / "Layouts/Old Layout/imgui.ini", legacyIni, true),
					"Cannot stage named layout ini");
			Require(FileSystem::WriteFileAtomically(legacy / "Layouts/Old Layout/Panels.yaml", legacyPanels,
													true),
					"Cannot stage named layout panels");
			{
				EditorWorkspaceManager manager(legacy);
				manager.Initialize(panels, true);
				manager.SetEnabled(true);
				Frame(manager, panels);
				Require(manager.m_MigrationComplete && manager.m_Layouts.size() == 7,
						"Legacy layouts were not imported completely");
				manager.Shutdown();
			}
			{
				EditorWorkspaceManager manager(legacy);
				manager.Initialize(panels, true);
				manager.SetEnabled(true);
				Frame(manager, panels);
				Require(manager.m_Layouts.size() == 7, "Restart duplicated migrated layouts");
				manager.Shutdown();
			}
			Require(WorkspaceStore::Read(legacy / "imgui.ini") == legacyIni,
					"Migration altered legacy source files");
			const auto partial = directory / "partial-legacy";
			Require(FileSystem::CreateDirectory(partial / "Layouts/Valid"), "Cannot stage partial migration");
			Require(FileSystem::WriteFileAtomically(partial / "imgui.ini", "invalid", true),
					"Cannot stage bad legacy ini");
			Require(FileSystem::WriteFileAtomically(partial / "Layouts/Valid/imgui.ini", legacyIni, true),
					"Cannot stage valid legacy ini");
			{
				EditorWorkspaceManager manager(partial);
				manager.Initialize(panels, true);
				manager.SetEnabled(true);
				Frame(manager, panels);
				Require(!manager.m_MigrationComplete && manager.m_Layouts.size() == 6,
						"A damaged legacy layout blocked valid imports or was marked complete");
				manager.Shutdown();
			}
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);
		return failures.empty();
	}
} // namespace Lux
#endif
