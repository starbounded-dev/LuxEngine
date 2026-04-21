#include "lpch.h"
#include "AssetManagerPanel.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/ImGui/ImGuiEx.h"
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

		bool MatchesSearch(std::string_view search, const AssetMetadata& metadata)
		{
			if (search.empty())
				return true;

			const std::string searchLower = Utils::String::ToLowerCopy(std::string(search));
			const std::string handleString = std::to_string((uint64_t)metadata.Handle);
			const std::string typeString = Utils::String::ToLowerCopy(std::string(AssetTypeToString(metadata.Type)));
			const std::string filePathString = Utils::String::ToLowerCopy(metadata.FilePath.generic_string());
			const std::string statusString = Utils::String::ToLowerCopy(AssetStatusToString(metadata.Status));

			return Utils::String::ToLowerCopy(handleString).find(searchLower) != std::string::npos
				|| typeString.find(searchLower) != std::string::npos
				|| filePathString.find(searchLower) != std::string::npos
				|| statusString.find(searchLower) != std::string::npos;
		}
	}

	void AssetManagerPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		if (!ImGui::Begin("Asset Manager", &isOpen))
		{
			ImGui::End();
			return;
		}

		Ref<Project> project = Project::GetActive();
		Ref<EditorAssetManager> assetManager = Project::GetEditorAssetManager();
		if (!project || !assetManager)
		{
			ImGui::TextDisabled("Open a project to inspect the asset registry.");
			ImGui::End();
			return;
		}

		const AssetRegistry& registry = assetManager->GetAssetRegistry();
		const AssetMap& loadedAssets = AssetManager::GetLoadedAssets();

		size_t loadedAssetCount = loadedAssets.size();
		size_t loadedMemoryAssetCount = 0;
		for (const auto& [handle, asset] : loadedAssets)
		{
			if (AssetManager::IsMemoryAsset(handle))
				loadedMemoryAssetCount++;
		}

		ImGui::Text("Registry: %zu assets", registry.size());
		ImGui::SameLine();
		ImGui::Text("Loaded: %zu", loadedAssetCount);
		ImGui::SameLine();
		ImGui::Text("Memory-only: %zu", loadedMemoryAssetCount);

		if (ImGui::Button("Sync Asset Thread"))
			AssetManager::SyncWithAssetThread();
		ImGui::SameLine();
		if (ImGui::Button("Reload Loaded Assets"))
			AssetManager::EnsureAllLoadedCurrent();
		ImGui::SameLine();
		if (ImGui::Button("Save Registry"))
			assetManager->SerializeAssetRegistry();

		ImGui::Separator();

		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::Property("Search", m_SearchBuffer, sizeof(m_SearchBuffer));
		ImGuiEx::Property("Show Only Loaded", m_ShowOnlyLoaded);
		ImGuiEx::EndPropertyGrid();

		ImGui::Separator();

		const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV
			| ImGuiTableFlags_RowBg
			| ImGuiTableFlags_Resizable
			| ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_SizingStretchProp;

		if (ImGui::BeginTable("##asset_registry_table", 6, tableFlags, ImVec2(0.0f, 0.0f)))
		{
			ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, 130.0f);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("Path");
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Loaded", ImGuiTableColumnFlags_WidthFixed, 65.0f);
			ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthFixed, 120.0f);
			ImGui::TableHeadersRow();

			for (const auto& [handle, metadata] : registry)
			{
				if (m_ShowOnlyLoaded && !AssetManager::IsAssetLoaded(handle))
					continue;

				if (!MatchesSearch(m_SearchBuffer, metadata))
					continue;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%llu", (unsigned long long)metadata.Handle);

				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(std::string(AssetTypeToString(metadata.Type)).c_str());

				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(metadata.FilePath.generic_string().c_str());
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(metadata.FilePath.generic_string().c_str());
					ImGui::EndTooltip();
				}

				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(AssetStatusToString(metadata.Status));

				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(metadata.IsDataLoaded ? "Yes" : "No");

				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%llu", (unsigned long long)metadata.FileLastWriteTime);
			}

			ImGui::EndTable();
		}

		ImGui::End();
	}

}
