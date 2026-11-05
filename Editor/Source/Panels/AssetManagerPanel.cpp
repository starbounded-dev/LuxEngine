// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AssetManagerPanel.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Input.h"
#include <algorithm>
#include "Lux/Project/Project.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>

namespace Lux {

	namespace
	{
		const char* AssetStatusToString(AssetStatus status)
		{
			switch (status)
			{
			case AssetStatus::Ready: return "Ready";
			case AssetStatus::Invalid: return "Invalid";
			case AssetStatus::Loading: return "Loading";
			case AssetStatus::Missing: return "Missing";
			case AssetStatus::None:
			default:
				return "None";
			}
		}

	} // namespace

	void AssetManagerPanel::OnClose()
	{
		ImGuiEx::ClosePopupByID(m_DetailsPopupID);
		m_DetailsPopupID = 0;
		m_Inspecting = 0;
		m_IsFocused = false;
		m_FocusSearch = false;
	}

	void AssetManagerPanel::OnProjectChanged(const Ref<Project>& project)
	{
		OnClose();
		m_RegistryOwner = nullptr;
		m_Rows.clear();
		m_VisibleRows.clear();
		m_SearchBuffer[0] = 0;
		m_Query.clear();
	}

	void AssetManagerPanel::OnEvent(Event& event)
	{
		if (!m_IsFocused)
			return;
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(
			[this](KeyPressedEvent& key)
			{
				if (ImGui::GetIO().WantTextInput || GImGui->OpenPopupStack.Size > 0)
					return true;
				if (key.GetKeyCode() == Key::F && !key.GetRepeatCount() &&
					(Input::IsKeyDown(Key::LeftControl) || Input::IsKeyDown(Key::RightControl)))
				{
					m_FocusSearch = true;
					return true;
				}
				return false;
			});
	}

	void AssetManagerPanel::OnImGuiRender(bool& isOpen)
	{
		m_IsFocused = false;
		if (!isOpen)
			return;
		if (!ImGui::Begin("Asset Manager", &isOpen))
		{
			ImGui::End();
			return;
		}
		m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		const auto project = Project::GetActive();
		auto manager = project ? Project::GetEditorAssetManager() : nullptr;
		if (!manager)
		{
			ImGuiEx::PanelEmptyState("##registry_project", "No active project",
									 "Open a project to inspect its asset registry.");
			ImGui::End();
			return;
		}
		const ImGuiEx::PanelAction actions[] = {{0, "Sync asset thread", LUX_ICON_REFRESH},
												{1, "Reload loaded assets", LUX_ICON_REPEAT},
												{2, "Save registry", LUX_ICON_FLOPPY_O},
												{3, "Only loaded", nullptr, nullptr, true, m_ShowOnlyLoaded}};
		switch (ImGuiEx::PanelActionBar("##registry_actions", actions))
		{
		case 0:
			AssetManager::SyncWithAssetThread();
			break;
		case 1:
			AssetManager::EnsureAllLoadedCurrent();
			break;
		case 2:
			manager->SerializeAssetRegistry();
			break;
		case 3:
			m_ShowOnlyLoaded = !m_ShowOnlyLoaded;
			break;
		default:
			break;
		}
		if (m_FocusSearch)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusSearch = false;
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputTextWithHint("##registry_search", "Search path, type, status or handle... (Ctrl+F)",
									 m_SearchBuffer, sizeof(m_SearchBuffer)))
			m_Query = Utils::String::ToLowerCopy(std::string(m_SearchBuffer));
		const auto& registry = manager->GetAssetRegistry();
		if (m_RegistryOwner != manager.Raw() || m_RegistryRevision != registry.GetRevision())
		{
			m_RegistryOwner = manager.Raw();
			m_RegistryRevision = registry.GetRevision();
			m_Rows.clear();
			m_Rows.reserve(registry.Count());
			for (const auto& [handle, metadata] : registry)
			{
				std::string path = Utils::PathToUTF8(metadata.FilePath);
				std::string type(AssetTypeToString(metadata.Type));
				std::string handleText = std::to_string(static_cast<uint64_t>(handle));
				const std::string caption =
					std::format("{} | {} | {}", type, AssetStatusToString(metadata.Status),
								metadata.IsDataLoaded ? "Loaded" : "Unloaded");
				std::string search = Utils::String::ToLowerCopy(
					std::format("{} {} {} {}", path, type, handleText, AssetStatusToString(metadata.Status)));
				m_Rows.push_back({handle, metadata, std::move(path), std::move(handleText), std::move(type),
								  std::move(search), caption});
			}
			std::ranges::sort(m_Rows, {}, &RegistryRow::Path);
		}
		m_VisibleRows.clear();
		m_VisibleRows.reserve(m_Rows.size());
		for (size_t index = 0; index < m_Rows.size(); ++index)
			if ((!m_ShowOnlyLoaded || AssetManager::IsAssetLoaded(m_Rows[index].ID)) &&
				(m_Query.empty() || m_Rows[index].Search.find(m_Query) != std::string::npos))
				m_VisibleRows.push_back(index);
		const auto& loaded = AssetManager::GetLoadedAssets();
		size_t memory = 0;
		for (const auto& [handle, asset] : loaded)
			if (AssetManager::IsMemoryAsset(handle))
				++memory;
		ImGui::TextWrapped("Registry: %zu | Loaded: %zu | Memory: %zu", registry.size(), loaded.size(),
						   memory);
		const bool compact = ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 55.0f;
		bool openDetails = false;
		const float line = ImGui::GetTextLineHeight();
		const float cellHeight = compact ? line * 2.0f + ImGui::GetStyle().ItemInnerSpacing.y : line;
		const float rowHeight = cellHeight + ImGui::GetStyle().CellPadding.y * 2.0f;
		if (m_VisibleRows.empty())
			ImGuiEx::PanelEmptyState(
				"##registry_empty", "No matching assets",
				"Change the search or disable Only loaded. Import assets with Content Browser.");
		else if (ImGui::BeginTable(compact ? "##asset_registry_compact" : "##asset_registry_table",
								   compact ? 1 : 6,
								   ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
									   ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit))
		{
			if (!compact)
			{
				ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed,
										ImGui::GetFontSize() * 10.0f);
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed,
										ImGui::GetFontSize() * 8.0f);
			}
			ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
			if (!compact)
			{
				ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed,
										ImGui::GetFontSize() * 7.0f);
				ImGui::TableSetupColumn("Loaded", ImGuiTableColumnFlags_WidthFixed,
										ImGui::GetFontSize() * 5.0f);
				ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthFixed,
										ImGui::GetFontSize() * 10.0f);
			}
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(m_VisibleRows.size()), rowHeight);
			while (clipper.Step())
				for (int visible = clipper.DisplayStart; visible < clipper.DisplayEnd; ++visible)
				{
					const auto& row = m_Rows[m_VisibleRows[static_cast<size_t>(visible)]];
					ImGuiEx::ScopedID id(reinterpret_cast<const void*>(static_cast<uint64_t>(row.ID)));
					ImGui::TableNextRow(0, rowHeight);
					ImGui::TableNextColumn();
					if (!compact)
					{
						ImGui::TextUnformatted(row.Handle.c_str());
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(row.Type.c_str());
						ImGui::TableNextColumn();
					}
					const ImVec2 minimum = ImGui::GetCursorScreenPos();
					const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
					if (ImGui::Selectable("##asset", row.ID == m_Inspecting, 0, {0.0f, cellHeight}))
					{
						m_Inspecting = row.ID;
						openDetails = true;
					}
					ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), minimum,
											  {minimum.x + width, minimum.y + line}, minimum.x + width,
											  row.Path.c_str(), row.Path.c_str() + row.Path.size(), nullptr);
					if (compact)
					{
						ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
						ImGui::RenderTextEllipsis(
							ImGui::GetWindowDrawList(),
							{minimum.x, minimum.y + line + ImGui::GetStyle().ItemInnerSpacing.y},
							{minimum.x + width, minimum.y + cellHeight}, minimum.x + width,
							row.Caption.c_str(), row.Caption.c_str() + row.Caption.size(), nullptr);
					}
					else
					{
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(AssetStatusToString(row.Metadata.Status));
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(row.Metadata.IsDataLoaded ? "Yes" : "No");
						ImGui::TableNextColumn();
						ImGui::Text("%llu", static_cast<unsigned long long>(row.Metadata.FileLastWriteTime));
					}
				}
			ImGui::EndTable();
		}
		if (openDetails)
		{
			m_DetailsPopupID = ImGui::GetID("Asset details");
			ImGui::OpenPopup("Asset details");
		}
		ImGui::SetNextWindowSize(
			{std::min(ImGui::GetFontSize() * 32.0f, ImGui::GetWindowViewport()->WorkSize.x * 0.9f), 0.0f});
		if (ImGui::BeginPopup("Asset details"))
		{
			if (!registry.Contains(m_Inspecting))
				ImGuiEx::PanelMessage("Asset no longer registered",
									  "Close this view and choose another asset.");
			else
			{
				const auto metadata = manager->GetMetadata(m_Inspecting);
				const std::string path = Utils::PathToUTF8(metadata.FilePath);
				ImGui::TextWrapped("%s", path.c_str());
				ImGui::Text("Handle: %llu", static_cast<unsigned long long>(m_Inspecting));
				ImGui::Text("Type: %s", AssetTypeToString(metadata.Type).data());
				ImGui::Text("Status: %s | %s", AssetStatusToString(metadata.Status),
							metadata.IsDataLoaded ? "Loaded" : "Unloaded");
				ImGui::Text("Timestamp: %llu", static_cast<unsigned long long>(metadata.FileLastWriteTime));
				if (ImGui::Button("Copy path"))
					ImGui::SetClipboardText(path.c_str());
			}
			if (ImGui::Button("Close"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::End();
	}

}
