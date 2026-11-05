// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "EditorWorkspaceManager.h"

#include "Lux/Core/Hash.h"
#include "Lux/Core/Log.h"
#include "Lux/Core/UUID.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Editor/PanelManager.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/FileSystem.h"

#include <cstdio>
#include <sstream>
#include <unordered_set>

namespace Lux
{
	EditorWorkspaceManager::EditorWorkspaceManager(const std::filesystem::path& directory)
		: m_Directory(directory)
	{
		for (const auto& [id, name] : {std::pair{"layout", "Layout"},
									   {"shading", "Shading"},
									   {"audio", "Audio"},
									   {"scripting", "Scripting"},
									   {"debugging", "Debugging"}})
			m_Layouts.push_back(
				{"default." + std::string(id), name, "default." + std::string(id), {}, {}, {}, true});
		try
		{
			m_DiskText = WorkspaceStore::Read(directory / "Workspaces.yaml");
			if (m_DiskText.empty() && FileSystem::Exists(directory / "Workspaces.yaml"))
				throw std::runtime_error("The workspace file is empty.");
			if (!m_DiskText.empty())
				Load(m_DiskText);
		}
		catch (const std::exception& error)
		{
			// Preserve malformed/future data rather than autosaving defaults over it.
			m_ReadOnly = true;
			m_Error = error.what();
			LUX_CORE_ERROR_TAG("Editor", "Workspace load: {}", m_Error);
			if (!m_FutureVersion)
			{
				try
				{
					const auto previous = WorkspaceStore::Read(directory / "Workspaces.yaml.previous");
					if (!previous.empty())
					{
						Load(previous);
						m_Error += " Previous layouts were recovered in memory. Use Recover Layout File to "
								   "keep them.";
					}
				}
				catch (const std::exception& backupError)
				{
					m_Error += " Recovery file: " + std::string(backupError.what());
				}
			}
		}
		if (!m_ReadOnly)
			m_Store = CreateScope<WorkspaceStore>(directory / "Workspaces.yaml", m_DiskText);
	}

	EditorWorkspaceManager::~EditorWorkspaceManager() = default;

	EditorWorkspaceManager::Layout* EditorWorkspaceManager::Find(const std::string& id)
	{
		for (auto& layout : m_Layouts)
			if (layout.ID == id)
				return &layout;
		return nullptr;
	}

	void EditorWorkspaceManager::Load(const std::string& text)
	{
		const auto data = Yaml::Load(text);
		if (data["Version"].as<uint32_t>(0) != 1)
		{
			m_FutureVersion = data["Version"].as<uint32_t>(0) > 1;
			throw std::runtime_error("Unsupported workspace file version. The file has been preserved.");
		}
		const auto layouts = data["Layouts"];
		if (!layouts.IsSequence() || layouts.size() > 256)
			throw std::runtime_error("Invalid workspace list (maximum 256 stored records).");
		std::vector<Layout> loaded;
		std::unordered_set<std::string> seen;
		for (const auto& node : layouts)
		{
			const auto id = node["ID"].as<std::string>();
			if (id.empty() || id.size() > 128 || !seen.insert(id).second)
				throw std::runtime_error("Invalid or duplicate workspace identity.");
			if (Layout* factory = Find(id); factory && factory->Factory)
			{
				loaded.push_back(*factory);
				continue;
			}
			Layout layout;
			layout.ID = id;
			layout.Name = node["Name"].as<std::string>();
			layout.Origin = node["Origin"].as<std::string>("default.layout");
			layout.Ini = node["Ini"].as<std::string>();
			layout.Panels = node["Panels"].as<std::string>();
			layout.Deleted = node["Deleted"].as<bool>(false);
			layout.PreservedPanels = node["PreservedPanels"].as<std::string>(layout.Panels);
			layout.LegacyPath = node["LegacyPath"].as<std::string>("");
			if (layout.Name.empty() || layout.Name.size() > 255 || layout.Ini.size() > 1024 * 1024 ||
				layout.Ini.find('\0') != std::string::npos)
				throw std::runtime_error("Invalid personal layout name or window data.");
			const bool resetToDefault = layout.Ini.empty() && layout.Panels.empty() && Find(layout.Origin) &&
										Find(layout.Origin)->Factory;
			if (!resetToDefault && (!WorkspaceDocking::ValidateIni(layout.Ini) ||
									!Yaml::Load(layout.Panels)["Panels"].IsSequence()))
				throw std::runtime_error("Personal layout is missing docking or panel data.");
			if (!layout.PreservedPanels.empty() && !Yaml::Load(layout.PreservedPanels)["Panels"].IsSequence())
				throw std::runtime_error("Personal layout has invalid preserved panel data.");
			const auto ratios = node["Ratios"];
			if (ratios.size() > 1024)
				throw std::runtime_error("Too many workspace split nodes.");
			for (const auto& ratio : ratios)
			{
				const float fraction = ratio["Ratio"].as<float>();
				if (!std::isfinite(fraction) || fraction <= 0.0f || fraction >= 1.0f)
					throw std::runtime_error("Invalid workspace split ratio.");
				layout.Ratios.push_back({ratio["Node"].as<uint32_t>(), ratio["First"].as<uint32_t>(),
										 ratio["Second"].as<uint32_t>(), fraction});
			}
			loaded.push_back(std::move(layout));
		}
		for (const auto& factory : m_Layouts)
			if (factory.Factory && !seen.contains(factory.ID))
				loaded.push_back(factory);
		const auto active = data["Active"].as<std::string>("default.layout");
		m_Layouts = std::move(loaded);
		if (Layout* layout = Find(active); layout && !layout->Deleted)
			m_Active = active;
		m_Loaded = true;
		m_MigrationComplete = data["MigrationComplete"].as<bool>(false);
	}

	void EditorWorkspaceManager::PrimeImGui()
	{
		if (m_Loaded || m_ReadOnly)
		{
			ImGui::GetIO().IniFilename = nullptr;
			if (Layout* layout = Find(m_Active); layout && !layout->Ini.empty())
				ImGui::LoadIniSettingsFromMemory(layout->Ini.data(), layout->Ini.size());
		}
	}

	void EditorWorkspaceManager::Initialize(PanelManager& panels, bool simple)
	{
		m_Panels = &panels;
		const std::string registered = panels.CaptureState();
		if (!m_MigrationComplete && !m_ReadOnly)
		{
			bool complete = true;
			// Import known files only; names stay data and are never used as new paths.
			auto import = [this, &registered, &complete](const std::filesystem::path& ini,
														 const std::filesystem::path& states,
														 const std::string& name)
			{
				try
				{
					for (const auto& existing : m_Layouts)
						if (existing.LegacyPath == ini.string())
							return existing.ID;
					if (std::ranges::count_if(m_Layouts,
											  [](const Layout& layout) { return !layout.Deleted; }) >= 128)
						throw std::runtime_error(
							"Too many legacy layouts to import; originals were preserved.");
					Layout layout;
					layout.ID = "personal." + std::to_string(static_cast<uint64_t>(UUID()));
					layout.Name = name;
					layout.LegacyPath = ini.string();
					layout.Origin = "default.layout";
					layout.Ini = WorkspaceStore::Read(ini);
					layout.Panels = FileSystem::Exists(states) ? WorkspaceStore::Read(states) : registered;
					if (!WorkspaceDocking::ValidateIni(layout.Ini) ||
						!Yaml::Load(layout.Panels)["Panels"].IsSequence())
						throw std::runtime_error("Invalid legacy layout: " + name);
					layout.PreservedPanels = layout.Panels;
					const std::string id = layout.ID;
					m_Layouts.push_back(std::move(layout));
					return id;
				}
				catch (const std::exception& error)
				{
					complete = false;
					m_Error = error.what();
					LUX_CORE_ERROR_TAG("Editor", "Workspace migration: {}", m_Error);
					return std::string{};
				}
			};
			try
			{
				if (!m_Loaded)
					m_Active = simple ? "default.layout" : "default.debugging";
				if (FileSystem::Exists(m_Directory / "imgui.ini"))
				{
					const auto imported = import(m_Directory / "imgui.ini", m_Directory / "EditorLayout.yaml",
												 "Personal Layout (Imported)");
					if (!m_Loaded && !imported.empty())
						m_Active = imported;
				}
				std::error_code error;
				const auto directory = m_Directory / "Layouts";
				if (FileSystem::IsDirectory(directory))
				{
					for (const auto& entry : std::filesystem::directory_iterator(directory, error))
						if (entry.is_directory() && FileSystem::Exists(entry.path() / "imgui.ini"))
							import(entry.path() / "imgui.ini", entry.path() / "Panels.yaml",
								   entry.path().filename().string());
					if (error)
						throw std::runtime_error(error.message());
				}
				m_MigrationComplete = complete;
			}
			catch (const std::exception& error)
			{
				m_Error = error.what();
				LUX_CORE_ERROR_TAG("Editor", "Workspace migration: {}", m_Error);
			}
		}
		panels.SetWorkspaceManaged(true);
		ImGui::GetIO().IniFilename = nullptr;
		m_Request = Request{Action::Switch, m_Active, {}};
		m_Enabled = Project::GetActive() != nullptr;
	}

	std::string EditorWorkspaceManager::CaptureIni() const
	{
		size_t count = 0;
		const char* ini = ImGui::SaveIniSettingsToMemory(&count);
		std::unordered_set<std::string> windows;
		for (const auto& [id, panel] : m_Panels->GetPanels(PanelCategory::View))
			windows.insert("[Window][" + std::string(panel.GetWindowName()) + "]");
		std::istringstream lines(std::string(ini, count));
		std::string result, line;
		bool keep = false;
		while (std::getline(lines, line))
		{
			if (!line.empty() && line[0] == '[')
				keep = windows.contains(line) || line == "[Docking][Data]";
			if (keep)
				result += line + '\n';
		}
		return result;
	}

	void EditorWorkspaceManager::Capture()
	{
		LUX_PROFILE_FUNCTION("EditorWorkspaceManager::Capture");
		if (!m_HasDrawn || m_Playing)
			return;
		Layout& layout = *Find(m_Active);
		std::string ini = CaptureIni();
		std::string panels = m_Panels->CaptureState();
		layout.Modified |= layout.Ini != ini || layout.Panels != panels;
		layout.Ini = std::move(ini);
		layout.Panels = std::move(panels);
		layout.Ratios = m_Docking.CaptureRatios();
	}

	void EditorWorkspaceManager::Apply(Layout& layout)
	{
		LUX_PROFILE_FUNCTION("EditorWorkspaceManager::Apply");
		m_PendingDockTabs.clear();
		// The editor owns its docking namespace. Clear only docking and managed window
		// settings, preserving independent ImGui tables, preferences and transient windows.
		ImGui::DockBuilderRemoveNode(0);
		for (const auto& [id, panel] : m_Panels->GetPanels(PanelCategory::View))
		{
			const char* title = panel.GetWindowName();
			ImGui::ClearWindowSettings(title);
			if (std::string_view(title) != panel.Name)
				ImGui::ClearWindowSettings(panel.Name);
			if (ImGuiWindow* window = ImGui::FindWindowByName(title))
			{
				// ClearWindowSettings retires the old entry, but the live window retains its
				// chunk offset. The next save would otherwise resurrect duplicate settings
				// that overwrite the newly loaded DockId. Rebind through the name lookup.
				window->SettingsOffset = -1;
				window->Flags &= ~ImGuiWindowFlags_NoSavedSettings;
				window->AutoFitFramesX = window->AutoFitFramesY = 0;
			}
		}
		ImGui::FocusWindow(nullptr);
		if (!layout.Ini.empty())
			ImGui::LoadIniSettingsFromMemory(layout.Ini.data(), layout.Ini.size());
		if (!layout.PreservedPanels.empty() && !m_Panels->ApplyState(layout.PreservedPanels))
			throw std::runtime_error("Cannot apply preserved panel state for " + layout.Name);
		if (!layout.Panels.empty() && !m_Panels->ApplyState(layout.Panels))
			throw std::runtime_error("Cannot apply panel state for " + layout.Name);
		// Older presets docked the display label even when the panel drew an icon-prefixed title.
		// Preserve the actual window ID and recover that placeholder's dock for personal layouts.
		for (const auto& [id, panel] : m_Panels->GetPanels(PanelCategory::View))
		{
			const char* title = panel.GetWindowName();
			if (std::string_view(title) == panel.Name)
				continue;
			const auto* actual = ImGui::FindWindowSettingsByID(ImHashStr(title));
			const auto* legacy = ImGui::FindWindowSettingsByID(ImHashStr(panel.Name));
			if ((!actual || !actual->DockId) && legacy && legacy->DockId)
				ImGui::DockBuilderDockWindow(title, legacy->DockId);
		}
		// Layouts predating separate views use the former Viewport window identity.
		if (const auto* legacy = ImGui::FindWindowSettingsByID(ImHashStr("Viewport"));
			legacy && legacy->DockId)
		{
			for (const char* name : {"Scene View", "Game View"})
			{
				const auto* settings = ImGui::FindWindowSettingsByID(ImHashStr(name));
				if (!settings || !settings->DockId)
					ImGui::DockBuilderDockWindow(name, legacy->DockId);
			}
		}
		m_Docking.RestoreRatios(layout.Ratios);
		m_Rebuild = layout.Ini.empty();
		m_HasDrawn = false;
		m_Applied = true;
		if (m_Playing)
		{
			m_GameWasOpen = m_Panels->GetPanelData(Hash::GenerateFNVHash("GameViewPanel"))->IsOpen;
			m_SceneWasOpen = m_Panels->GetPanelData(Hash::GenerateFNVHash("SceneViewPanel"))->IsOpen;
			m_Panels->SetOpen(Hash::GenerateFNVHash("GameViewPanel"), true);
		}
	}

	void EditorWorkspaceManager::BeforeFrame()
	{
		if (!m_Panels || !m_Enabled)
			return;
		if (GImGui->ActiveId || GImGui->MovingWindow || ImGui::GetTopMostPopupModal())
			return;
		if (m_Request)
		{
			const Request request = std::move(*m_Request);
			m_Request.reset();
			Capture();
			Layout* source = Find(request.ID);
			if (!source)
			{
				m_Error = "The requested layout is no longer available.";
				LUX_CORE_ERROR_TAG("Editor", "Workspace request: {}", m_Error);
				return;
			}
			std::string target = m_Active;
			switch (request.Type)
			{
			case Action::Switch:
				target = source->ID;
				break;
			case Action::Copy:
			{
				if (std::ranges::count_if(m_Layouts, [](const Layout& layout) { return !layout.Deleted; }) >=
					128)
				{
					m_Error = "Maximum 128 layouts. Delete unused personal layouts first.";
					return;
				}
				Layout copy = *source;
				copy.ID = "personal." + std::to_string(static_cast<uint64_t>(UUID()));
				copy.Name = request.Name;
				copy.Origin = source->Factory ? source->ID : source->Origin;
				copy.Factory = copy.Deleted = false;
				copy.LegacyPath.clear();
				target = copy.ID;
				m_Layouts.push_back(std::move(copy));
				break;
			}
			case Action::Rename:
				if (!source->Factory)
				{
					source->Name = request.Name;
				}
				break;
			case Action::Delete:
				if (!source->Factory)
				{
					source->Deleted = true;
					if (source->ID == target)
						target = "default.layout";
					auto deleted =
						std::ranges::count_if(m_Layouts, [](const Layout& layout) { return layout.Deleted; });
					for (auto it = m_Layouts.begin(); it != m_Layouts.end() && deleted > 32;)
					{
						if (it->Deleted && it->ID != request.ID)
						{
							it = m_Layouts.erase(it);
							--deleted;
						}
						else
							++it;
					}
				}
				break;
			case Action::Restore:
				source->Deleted = false;
				target = source->ID;
				break;
			case Action::Reset:
			{
				m_ResetUndo = *source;
				source->Ini.clear();
				source->Panels.clear();
				source->Ratios.clear();
				source->PreservedPanels.clear();
				source->Modified = false;
				break;
			}
			case Action::UndoReset:
				if (m_ResetUndo)
				{
					*source = std::move(*m_ResetUndo);
					m_ResetUndo.reset();
					target = source->ID;
				}
				break;
			case Action::Recover:
				if (m_ReadOnly && !m_FutureVersion)
				{
					m_Store = CreateScope<WorkspaceStore>(m_Directory / "Workspaces.yaml", m_DiskText, false);
					m_ReadOnly = false;
					m_Error.clear();
				}
				break;
			}
			try
			{
				if (target != m_Active || request.Type == Action::Switch || request.Type == Action::Reset ||
					request.Type == Action::UndoReset)
				{
					Apply(*Find(target));
					m_Active = target;
				}
				Save();
			}
			catch (const std::exception& error)
			{
				m_Error = error.what();
				LUX_CORE_ERROR_TAG("Editor", "Workspace switch: {}", m_Error);
				Apply(*Find(m_Active));
			}
		}
		if (m_HasDrawn &&
			(ImGui::GetIO().WantSaveIniSettings || m_PanelRevision != m_Panels->GetVisibilityRevision()))
		{
			Capture();
			Save();
			ImGui::GetIO().WantSaveIniSettings = false;
		}
	}

	void EditorWorkspaceManager::BuildDefault(const std::string& id, ImGuiID root, ImVec2 size)
	{
		ImGui::DockBuilderRemoveNode(root);
		ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(root, size);
		ImGuiID center = root;
		const auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
		const auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
		const auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
		const char* dominant = id == "default.shading"	   ? "Material Editor"
							   : id == "default.audio"	   ? "Sound Editor"
							   : id == "default.scripting" ? "Beam"
														   : "Scene View";
		for (const auto& [panelID, panel] : m_Panels->GetPanels(PanelCategory::View))
		{
			const std::string_view name = panel.Name;
			ImGuiID home = bottom;
			bool open = name == "Scene Hierarchy" || name == "Properties" || name == "Content Browser" ||
						name == "Log" || name == dominant;
			if (name == "Scene Hierarchy")
				home = left;
			if (name == "Properties")
				home = right;
			if (name == dominant || name == "Scene View" || name == "Game View")
				home = center;
			if (id == "default.shading" && name == "Scene View")
			{
				open = true;
				home = right;
			}
			if (id == "default.audio" && (name == "Audio Mixer" || name == "Sound Bank Editor"))
			{
				open = true;
				home = name == "Audio Mixer" ? bottom : right;
			}
			if (id == "default.debugging" && (name == "Profiler" || name == "Renderer Debugger" ||
											  name == "Scene Renderer" || name == "Light Settings"))
			{
				open = true;
				home = name == "Scene Renderer" || name == "Light Settings" ? right : bottom;
			}
			ImGui::DockBuilderDockWindow(panel.GetWindowName(), home);
			m_Panels->SetOpen(panelID, open);
		}
		ImGui::DockBuilderFinish(root);
		auto select = [this](ImGuiID node, const char* name)
		{
			for (const auto& [id, panel] : m_Panels->GetPanels(PanelCategory::View))
				if (std::string_view(panel.Name) == name)
				{
					name = panel.GetWindowName();
					break;
				}
			const ImGuiID tab = ImHashStr("#TAB", 4, ImHashStr(name));
			ImGui::DockBuilderGetNode(node)->SelectedTabId = tab;
			m_PendingDockTabs.emplace_back(node, tab);
		};
		select(center, dominant);
		if (id == "default.shading")
			select(right, "Scene View");
		else if (id == "default.audio")
		{
			select(right, "Sound Bank Editor");
			select(bottom, "Audio Mixer");
		}
		else if (id == "default.debugging")
		{
			select(right, "Scene Renderer");
			select(bottom, "Profiler");
		}
		ImGui::SetWindowFocus(dominant);
		m_Docking.Reset();
	}

	void EditorWorkspaceManager::DrawDockSpace(ImGuiID root, ImVec2 size)
	{
		if (size.x < 2.0f || size.y < 2.0f)
			return;
		if (!m_Applied)
		{
			ImGui::DockSpace(root, size, ImGuiDockNodeFlags_KeepAliveOnly);
			return;
		}
		if (m_Rebuild)
		{
			Layout& layout = *Find(m_Active);
			BuildDefault(layout.Factory ? layout.ID : layout.Origin, root, size);
			m_Rebuild = false;
		}
		m_Docking.Prepare(root, size);
		ImGui::DockSpace(root, size);
		// New windows join their dock nodes after DockSpace. Select their intended
		// tabs once those tab bars exist rather than losing the request on creation.
		std::erase_if(m_PendingDockTabs,
					  [](const auto& selection)
					  {
						  ImGuiDockNode* node = ImGui::DockBuilderGetNode(selection.first);
						  if (!node)
							  return true;
						  if (!node->TabBar || !ImGui::TabBarFindTabByID(node->TabBar, selection.second))
							  return false;
						  node->TabBar->NextSelectedTabId = selection.second;
						  return true;
					  });
		m_HasDrawn = true;
		// A removed monitor must not strand a managed floating window. Only recover
		// title bars outside every available work area, and never fight an active drag.
		if (!GImGui->MovingWindow && !ImGui::GetPlatformIO().Monitors.empty())
		{
			for (const auto& [id, panel] : m_Panels->GetPanels(PanelCategory::View))
			{
				ImGuiWindow* window = ImGui::FindWindowByName(panel.GetWindowName());
				if (!window || window->DockId || !panel.IsOpen)
					continue;
				const ImRect title(window->Pos, ImVec2(window->Pos.x + window->Size.x,
													   window->Pos.y + ImGui::GetFrameHeight()));
				bool reachable = false;
				for (const auto& monitor : ImGui::GetPlatformIO().Monitors)
					reachable |= title.Overlaps(
						ImRect(monitor.WorkPos, ImVec2(monitor.WorkPos.x + monitor.WorkSize.x,
													   monitor.WorkPos.y + monitor.WorkSize.y)));
				if (!reachable)
				{
					const auto* viewport = ImGui::GetMainViewport();
					ImGui::SetWindowPos(window,
										ImVec2(viewport->WorkPos.x + 32.0f, viewport->WorkPos.y + 32.0f));
					ImGui::SetWindowSize(
						window, ImMin(window->Size, ImVec2(std::max(1.0f, viewport->WorkSize.x - 64.0f),
														   std::max(1.0f, viewport->WorkSize.y - 64.0f))));
				}
			}
		}
	}

	void EditorWorkspaceManager::Save()
	{
		LUX_PROFILE_FUNCTION("EditorWorkspaceManager::Save");
		m_PanelRevision = m_Panels->GetVisibilityRevision();
		if (!m_Store)
			return;
		Yaml::Writer out;
		out << Yaml::BeginMap << Yaml::Key << "Version" << Yaml::Value << 1;
		out << Yaml::Key << "Active" << Yaml::Value << m_Active;
		out << Yaml::Key << "MigrationComplete" << Yaml::Value << m_MigrationComplete;
		out << Yaml::Key << "Layouts" << Yaml::Value << Yaml::BeginSeq;
		for (const auto& layout : m_Layouts)
		{
			out << Yaml::BeginMap << Yaml::Key << "ID" << Yaml::Value << layout.ID;
			if (!layout.Factory)
			{
				out << Yaml::Key << "Name" << Yaml::Value << layout.Name;
				out << Yaml::Key << "Origin" << Yaml::Value << layout.Origin;
				out << Yaml::Key << "Ini" << Yaml::Value << layout.Ini;
				out << Yaml::Key << "Panels" << Yaml::Value << layout.Panels;
				out << Yaml::Key << "PreservedPanels" << Yaml::Value << layout.PreservedPanels;
				out << Yaml::Key << "LegacyPath" << Yaml::Value << layout.LegacyPath;
				out << Yaml::Key << "Deleted" << Yaml::Value << layout.Deleted;
				out << Yaml::Key << "Ratios" << Yaml::Value << Yaml::BeginSeq;
				for (const auto& split : layout.Ratios)
				{
					out << Yaml::BeginMap << Yaml::Key << "Node" << Yaml::Value << split.Node;
					out << Yaml::Key << "First" << Yaml::Value << split.First;
					out << Yaml::Key << "Second" << Yaml::Value << split.Second;
					out << Yaml::Key << "Ratio" << Yaml::Value << split.Ratio << Yaml::EndMap;
				}
				out << Yaml::EndSeq;
			}
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq << Yaml::EndMap;
		std::string serialized = out.str();
		m_Store->Queue(std::move(serialized), ++m_Revision);
	}

	void EditorWorkspaceManager::RequestName(Action action, const Layout& layout)
	{
		m_EditID = layout.ID;
		m_NameAction = action;
		std::snprintf(m_Name, sizeof(m_Name), "%s",
					  action == Action::Rename ? layout.Name.c_str() : (layout.Name + " Personal").c_str());
		m_OpenName = true;
	}

	void EditorWorkspaceManager::DrawNamePopup()
	{
		if (m_OpenName)
		{
			ImGui::OpenPopup("Personal Layout Name");
			m_OpenName = false;
		}
		if (!ImGui::BeginPopupModal("Personal Layout Name", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		const bool enter =
			ImGui::InputText("Name", m_Name, sizeof(m_Name), ImGuiInputTextFlags_EnterReturnsTrue);
		std::string name = m_Name;
		const auto first = name.find_first_not_of(" \t\r\n");
		name = first == std::string::npos ? ""
										  : name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
		const bool valid = !name.empty() && name.find_first_of("\r\n") == std::string::npos;
		ImGui::BeginDisabled(!valid);
		const bool accept =
			ImGui::Button(m_NameAction == Action::Rename ? "Rename" : "Create Personal Layout") ||
			(enter && valid);
		ImGui::EndDisabled();
		if (accept)
		{
			m_Request = Request{m_NameAction, m_EditID, name};
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void EditorWorkspaceManager::DrawPopups()
	{
		DrawNamePopup();
	}

	bool EditorWorkspaceManager::RequestShortcut(size_t slot)
	{
		if (!m_Enabled || slot == 0 || slot > 9)
			return false;
		// Match the menu: defaults first, then non-deleted personal layouts in saved order.
		for (const bool factory : {true, false})
			for (const auto& layout : m_Layouts)
				if (layout.Factory == factory && !layout.Deleted && --slot == 0)
				{
					m_Request = Request{Action::Switch, layout.ID, {}};
					return true;
				}
		return false;
	}

	void EditorWorkspaceManager::DrawMenu()
	{
		size_t slot = 0;
		auto draw = [&](const Layout& layout)
		{
			ImGuiEx::ScopedID id(layout.ID.c_str());
			char shortcut[16]{};
			if (++slot <= 9)
				std::snprintf(shortcut, sizeof(shortcut), "Ctrl+%zu", slot);
			if (ImGui::MenuItem(layout.Name.c_str(), shortcut, layout.ID == m_Active))
				m_Request = Request{Action::Switch, layout.ID, {}};
		};
		ImGui::TextDisabled("Default layouts");
		for (const auto& layout : m_Layouts)
		{
			if (!layout.Factory || layout.Deleted)
				continue;
			draw(layout);
		}
		ImGui::Separator();
		ImGui::TextDisabled("Personal layouts (saved automatically)");
		for (const auto& layout : m_Layouts)
		{
			if (layout.Factory || layout.Deleted)
				continue;
			draw(layout);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Save as Personal Layout..."))
			RequestName(Action::Copy, *Find(m_Active));
		const Layout& active = *Find(m_Active);
		ImGui::TextDisabled("Active: %s (%s)", active.Name.c_str(), active.Factory ? "Default" : "Personal");
		if (!active.Factory)
		{
			if (ImGui::MenuItem("Rename Personal Layout..."))
				RequestName(Action::Rename, active);
			if (ImGui::MenuItem("Delete Personal Layout"))
				m_Request = Request{Action::Delete, active.ID, {}};
		}
		if (ImGui::MenuItem(active.Factory ? "Restore Default" : "Reset to Starting Default"))
			m_Request = Request{Action::Reset, active.ID, {}};
		if (m_ResetUndo && ImGui::MenuItem("Undo Layout Reset"))
			m_Request = Request{Action::UndoReset, m_ResetUndo->ID, {}};
		if (ImGui::BeginMenu("Restore Deleted Personal Layout"))
		{
			for (const auto& layout : m_Layouts)
			{
				if (!layout.Deleted)
					continue;
				ImGuiEx::ScopedID id(layout.ID.c_str());
				if (ImGui::MenuItem(layout.Name.c_str()))
					m_Request = Request{Action::Restore, layout.ID, {}};
			}
			ImGui::EndMenu();
		}
		const std::string error = !m_Error.empty() ? m_Error : m_Store ? m_Store->GetError() : "";
		if (!error.empty())
		{
			ImGui::Separator();
			ImGui::TextWrapped("%s", error.c_str());
			if (m_ReadOnly && !m_FutureVersion && ImGui::MenuItem("Recover Layout File (preserve original)"))
				m_Request = Request{Action::Recover, m_Active, {}};
			if (m_Store && ImGui::MenuItem("Retry Layout Save"))
			{
				Capture();
				Save();
			}
		}
	}

	void EditorWorkspaceManager::DrawPanelsMenu()
	{
		ImGui::InputTextWithHint("##PanelSearch", "Find a panel", m_Search, sizeof(m_Search));
		m_PanelMenu.clear();
		m_PanelMenu.reserve(m_Panels->GetPanels(PanelCategory::View).size());
		for (auto& [id, panel] : m_Panels->GetPanels(PanelCategory::View))
			m_PanelMenu.push_back(&panel);
		std::ranges::sort(m_PanelMenu, {},
						  [](const PanelData* panel) { return std::string_view(panel->Name); });
		auto category = [](std::string_view name) -> std::string_view
		{
			if (name == "Scene View" || name == "Game View" || name == "Scene Hierarchy" ||
				name == "Properties")
				return "Scene";
			if (name == "Content Browser" || name == "Material Editor" || name == "Sound Editor" ||
				name == "Sound Bank Editor" || name == "Audio Mixer" || name == "Asset Manager")
				return "Assets & Authoring";
			if (name == "Beam")
				return "Scripting";
			if (name == "Log" || name == "Profiler" || name == "Renderer Debugger" ||
				name == "Audio Debugger" || name == "Scene Renderer" || name == "Light Settings" ||
				name == "History")
				return "Diagnostics";
			return "Settings & Help";
		};
		auto draw = [this](const PanelData& panel)
		{
			ImGuiEx::ScopedID id(panel.ID);
			if (ImGui::MenuItem(panel.Name, nullptr, panel.IsOpen))
				m_Panels->SetOpen(Hash::GenerateFNVHash(panel.ID), !panel.IsOpen);
		};
		if (m_Search[0])
		{
			for (const auto* panel : m_PanelMenu)
				if (ImStristr(panel->Name, nullptr, m_Search, nullptr))
					draw(*panel);
		}
		else
		{
			for (const char* name :
				 {"Scene", "Assets & Authoring", "Scripting", "Diagnostics", "Settings & Help"})
			{
				if (ImGui::BeginMenu(name))
				{
					for (const auto* panel : m_PanelMenu)
						if (category(panel->Name) == name)
							draw(*panel);
					ImGui::EndMenu();
				}
			}
		}
	}

	void EditorWorkspaceManager::RequestReset()
	{
		m_Request = Request{Action::Reset, m_Active, {}};
	}
	void EditorWorkspaceManager::RequestDefault(bool simple)
	{
		m_Request = Request{Action::Switch, simple ? "default.layout" : "default.debugging", {}};
	}

	void EditorWorkspaceManager::BeginPlay()
	{
		Capture();
		const PanelData* game = m_Panels->GetPanelData(Hash::GenerateFNVHash("GameViewPanel"));
		m_GameWasOpen = game && game->IsOpen;
		m_SceneWasOpen = m_Panels->GetPanelData(Hash::GenerateFNVHash("SceneViewPanel"))->IsOpen;
		m_Playing = true;
	}

	bool EditorWorkspaceManager::EndPlay()
	{
		m_Playing = false;
		m_Panels->SetOpen(Hash::GenerateFNVHash("GameViewPanel"), m_GameWasOpen);
		return m_SceneWasOpen;
	}

	void EditorWorkspaceManager::Shutdown()
	{
		if (!m_Panels)
			return;
		Capture();
		Save();
		m_Store.reset(); // Flush the latest immutable snapshot and join while logging is alive.
		m_Panels = nullptr;
	}
} // namespace Lux
