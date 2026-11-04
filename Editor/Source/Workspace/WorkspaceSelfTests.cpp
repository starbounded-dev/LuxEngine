// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "EditorWorkspaceManager.h"

#ifndef LUX_DIST
#include "Lux/Core/Log.h"
#include "Lux/Editor/PanelManager.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/FileSystem.h"

#include <chrono>
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
				ImGui::Begin(Name, &open);
				ImGui::TextUnformatted("Workspace test");
				ImGui::End();
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
			manager.DrawTabs();
			manager.DrawDockSpace(ImGui::GetID("MyDockSpace"), ImGui::GetContentRegionAvail());
			panels.OnImGuiRender();
			for (const auto& [id, panel] : panels.GetPanels(PanelCategory::View))
			{
				if (!panel.IsOpen)
					continue;
				const auto* window = ImGui::FindWindowByName(panel.Name);
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
				LUX_CORE_INFO_TAG("Editor", "Workspace test: copy default");
				Require(manager.m_Active == "default.layout", "Clean startup did not select Layout");
				manager.m_Request = Request{Action::Copy, manager.m_Active, "Personal Test"};
				Frame(manager, panels);
				LUX_CORE_INFO_TAG("Editor", "Workspace test: copy rendered");
				personal = manager.m_Active;
				Require(!manager.Find(personal)->Factory, "Default copy did not become personal");
				document->DirtyDocument = 42;
				document->Section = 7;
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
