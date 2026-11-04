// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "PanelManager.h"
#include "Lux/Utilities/FileSystem.h"

#include "Lux/Serialization/Yaml.h"
#include "Lux/ImGui/ImGuiScaling.h"

namespace Lux {

	PanelManager::~PanelManager()
	{
		for (auto& map : m_Panels)
			map.clear();
	}

	PanelData* PanelManager::GetPanelData(uint32_t panelID)
	{
		for (auto& panelMap : m_Panels)
		{
			if (panelMap.find(panelID) == panelMap.end())
				continue;

			return &panelMap.at(panelID);
		}

		return nullptr;
	}

	const PanelData* PanelManager::GetPanelData(uint32_t panelID) const
	{
		for (auto& panelMap : m_Panels)
		{
			if (panelMap.find(panelID) == panelMap.end())
				continue;

			return &panelMap.at(panelID);
		}

		return nullptr;
	}

	void PanelManager::RemovePanel(const char* strID)
	{
		uint32_t id = Hash::GenerateFNVHash(strID);
		for (auto& panelMap : m_Panels)
		{
			if (panelMap.find(id) == panelMap.end())
				continue;

			panelMap.erase(id);
			return;
		}

		LUX_CORE_ERROR_TAG("PanelManager", "Couldn't find panel with id '{0}'", strID);
	}

	void PanelManager::OnImGuiRender()
	{
		bool openStateChanged = false;
		for (auto& panelMap : m_Panels)
		{
			for (auto& [id, panelData] : panelMap)
			{
				if (panelData.ObservedIsOpen && !panelData.IsOpen)
					panelData.Panel->OnClose();
				if (panelData.ObservedIsOpen != panelData.IsOpen)
					++m_VisibilityRevision;
				if (panelData.IsOpen)
				{
					ImGuiEx::ScopedWindowScale windowScale(panelData.Name);
					panelData.Panel->OnImGuiRender(panelData.IsOpen);
					if (!panelData.IsOpen)
					{
						panelData.Panel->OnClose();
						++m_VisibilityRevision;
					}
				}
				openStateChanged |= panelData.IsOpen != panelData.SavedIsOpen;
				panelData.ObservedIsOpen = panelData.IsOpen;
			}
		}

		// Covers panels closed with their own X above and panels opened or closed anywhere else
		// since the last frame; saving only on close lost every panel opened from the View menu.
		if (openStateChanged && m_LayoutLoaded && !m_WorkspaceManaged)
			Serialize();
	}

	void PanelManager::OnEvent(Event& e)
	{
		for (auto& panelMap : m_Panels)
		{
			for (auto& [id, panelData] : panelMap)
			{
				if (e.Handled)
					return;
				if (panelData.IsOpen)
					panelData.Panel->OnEvent(e);
			}
		}
	}

	void PanelManager::SetSceneContext(const Ref<Scene>& context)
	{
		for (auto& panelMap : m_Panels)
		{
			for (auto& [id, panelData] : panelMap)
				panelData.Panel->SetSceneContext(context);
		}
	}

	void PanelManager::OnProjectChanged(const Ref<Project>& project)
	{
		for (auto& panelMap : m_Panels)
		{
			for (auto& [id, panelData] : panelMap)
				panelData.Panel->OnProjectChanged(project);
		}

		// Panel state changes are saved on exit, so save them before reloading the file.
		if (m_WorkspaceManaged)
			return;
		if (m_LayoutLoaded)
			Serialize();
		Deserialize();
	}

	void PanelManager::Serialize(const std::filesystem::path& path) const
	{
		if (path.empty() && m_WorkspaceManaged)
			return;
		if (FileSystem::WriteFileAtomically(path.empty() ? FileSystem::GetPersistentStoragePath() / "EditorLayout.yaml" : path, CaptureState()))
		{
			if (path.empty())
			{
				for (const auto& panelMap : m_Panels)
					for (const auto& [id, panel] : panelMap)
						panel.SavedIsOpen = panel.IsOpen;
			}
		}
	}

	std::string PanelManager::CaptureState() const
	{
		Yaml::Writer out;
		out << Yaml::BeginMap;

		out << Yaml::Key << "Panels" << Yaml::Value << Yaml::BeginSeq;
		{
			for (size_t category = 0; category < m_Panels.size(); category++)
			{
				for (const auto& [panelID, panel] : m_Panels[category])
				{
					out << Yaml::BeginMap;
					out << Yaml::Key << "ID" << Yaml::Value << panelID;
					out << Yaml::Key << "Name" << Yaml::Value << panel.Name;
					out << Yaml::Key << "IsOpen" << Yaml::Value << panel.IsOpen;
					out << Yaml::Key << "State" << Yaml::Value << Yaml::BeginMap;
					panel.Panel->SaveState(out);
					out << Yaml::EndMap;
					out << Yaml::EndMap;
				}
			}
		}
		out << Yaml::EndSeq;

		out << Yaml::EndMap;

		return out.str();
	}

	void PanelManager::Deserialize(const std::filesystem::path& path)
	{
		m_LayoutLoaded = true;
		const std::filesystem::path layoutPath = path.empty() ? FileSystem::GetPersistentStoragePath() / "EditorLayout.yaml" : path;
		if (!FileSystem::Exists(layoutPath))
			return;

		std::ifstream stream(layoutPath);
		LUX_CORE_VERIFY(stream);
		std::stringstream ss;
		ss << stream.rdbuf();

		ApplyState(ss.str());
	}

	void PanelManager::SetOpen(uint32_t panelID, bool open)
	{
		if (PanelData* panel = GetPanelData(panelID))
		{
			if (panel->IsOpen != open)
				++m_VisibilityRevision;
			if (panel->IsOpen && !open)
				panel->Panel->OnClose();
			panel->IsOpen = panel->ObservedIsOpen = open;
		}
	}

	bool PanelManager::ApplyState(const std::string& text)
	{
		try
		{
			const Yaml::Node data = Yaml::Load(text);
			if (!data["Panels"].IsSequence())
				throw Yaml::Exception("Missing Panels sequence");
			bool hasProperties = false;
			std::optional<bool> legacyHierarchyOpen;
			for (const auto& node : data["Panels"])
			{
				const uint32_t id = node["ID"].as<uint32_t>(0);
				hasProperties |= id == Hash::GenerateFNVHash("PropertiesPanel");
				if (id == Hash::GenerateFNVHash("SceneHierarchyPanel"))
					legacyHierarchyOpen = node["IsOpen"].as<bool>(true);
			}
			if (!hasProperties && legacyHierarchyOpen)
				SetOpen(Hash::GenerateFNVHash("PropertiesPanel"), *legacyHierarchyOpen);
			for (const auto& panelNode : data["Panels"])
			{
				PanelData* panelData = GetPanelData(panelNode["ID"].as<uint32_t>(0));
				if (!panelData)
					continue;
				SetOpen(panelNode["ID"].as<uint32_t>(0), panelNode["IsOpen"].as<bool>(panelData->IsOpen));
				panelData->SavedIsOpen = panelData->IsOpen;
				if (Yaml::Node state = panelNode["State"]; state.IsMap())
					panelData->Panel->LoadState(state);
			}
			m_LayoutLoaded = true;
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("PanelManager", "Cannot restore panel layout: {}", error.what());
			return false;
		}
	}

}
