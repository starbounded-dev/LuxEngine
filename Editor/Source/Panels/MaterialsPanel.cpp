#include "lpch.h"
#include "MaterialsPanel.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Editor/SceneHierarchyPanel.h"
#include "Lux/Editor/EditorResources.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"

#include <imgui/imgui.h>
#include <glm/gtc/type_ptr.hpp>

namespace Lux {

	MaterialsPanel::MaterialsPanel(const Ref<SceneHierarchyPanel>& sceneHierarchyPanel, std::function<void(AssetHandle)> openMaterialCallback)
		: m_SceneHierarchyPanel(sceneHierarchyPanel), m_OpenMaterialCallback(std::move(openMaterialCallback))
	{
		m_CheckerboardTexture = EditorResources::CheckerboardTexture;
	}

	void MaterialsPanel::SetSceneContext(const Ref<Scene>& context)
	{
		m_Context = context;
	}

	void MaterialsPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		if (!ImGui::Begin("Materials", &isOpen))
		{
			ImGui::End();
			return;
		}

		if (!m_Context || !m_SceneHierarchyPanel)
		{
			ImGui::TextDisabled("No scene selected.");
			ImGui::End();
			return;
		}

		Entity selectedEntity = m_SceneHierarchyPanel->GetSelectedEntity();
		if (!selectedEntity)
		{
			ImGui::TextDisabled("Select a mesh entity to inspect its materials.");
			ImGui::End();
			return;
		}

		Ref<MaterialTable> materialTable;
		bool isOverride = false;
		std::string entityName = selectedEntity.HasComponent<TagComponent>() ? selectedEntity.GetComponent<TagComponent>().Tag : "Selected Entity";

		if (selectedEntity.HasComponent<MeshComponent>())
		{
			const auto& component = selectedEntity.GetComponent<MeshComponent>();
			if (component.Mesh)
			{
				Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(component.Mesh);
				if (mesh)
					materialTable = mesh->GetMaterials();
			}

		}
		else if (selectedEntity.HasComponent<StaticMeshComponent>())
		{
			const auto& component = selectedEntity.GetComponent<StaticMeshComponent>();
			if (component.StaticMesh)
			{
				Ref<StaticMesh> mesh = StaticMesh::GetOrCreateRuntime(component.StaticMesh);
				if (mesh)
					materialTable = mesh->GetMaterials();
			}

			if (component.MaterialTable && !component.MaterialTable->GetMaterials().empty())
			{
				materialTable = component.MaterialTable;
				isOverride = true;
			}
		}

		ImGui::Text("Entity: %s", entityName.c_str());
		ImGui::Separator();

		if (!materialTable || materialTable->GetMaterialCount() == 0)
		{
			ImGui::TextDisabled("The selected entity does not expose any material slots.");
			ImGui::End();
			return;
		}

		bool renderedAnyMaterial = false;
		for (uint32_t i = 0; i < materialTable->GetMaterialCount(); i++)
		{
			if (!materialTable->HasMaterial(i))
				continue;

			RenderMaterialSlot(i, materialTable->GetMaterial(i), isOverride);
			renderedAnyMaterial = true;
		}

		if (!renderedAnyMaterial)
			ImGui::TextDisabled("No material assets are assigned to the selected entity.");

		ImGui::End();
	}

	void MaterialsPanel::RenderMaterialSlot(uint32_t materialIndex, AssetHandle materialHandle, bool isOverride)
	{
		Ref<MaterialAsset> materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);
		if (!materialAsset || !materialAsset->GetMaterial())
			return;

		std::string materialName = materialAsset->GetMaterial() ? materialAsset->GetMaterial()->GetName() : "";
		if (materialName.empty())
			materialName = "Unnamed Material";

		std::string header = "Slot " + std::to_string(materialIndex) + " - " + materialName;
		if (!ImGuiEx::PropertyGridHeader(header, materialIndex == 0))
			return;

		ImGui::TextDisabled("%s", isOverride ? "Entity override" : "Mesh default");

		if (m_OpenMaterialCallback && ImGui::Button("Open in Material Editor"))
			m_OpenMaterialCallback(materialHandle);

		ImGui::SameLine();
		const bool canSave = AssetManager::IsPhysicalAsset(materialHandle);
		if (!canSave)
			ImGui::BeginDisabled();
		if (ImGui::Button("Save Material"))
			SaveMaterial(materialHandle, materialAsset);
		if (!canSave)
			ImGui::EndDisabled();

		ImGuiEx::BeginPropertyGrid();

		glm::vec3 albedo = materialAsset->GetAlbedoColor();
		if (ImGuiEx::Property("Albedo", albedo, 0.01f, 0.0f, 1.0f))
			materialAsset->SetAlbedoColor(albedo);

		float metalness = materialAsset->GetMetalness();
		if (ImGuiEx::Property("Metalness", metalness, 0.01f, 0.0f, 1.0f))
			materialAsset->SetMetalness(metalness);

		float roughness = materialAsset->GetRoughness();
		if (ImGuiEx::Property("Roughness", roughness, 0.01f, 0.0f, 1.0f))
			materialAsset->SetRoughness(roughness);

		float emission = materialAsset->GetEmission();
		if (ImGuiEx::Property("Emission", emission, 0.01f, 0.0f, 100.0f))
			materialAsset->SetEmission(emission);

		float transparency = materialAsset->GetTransparency();
		if (ImGuiEx::Property("Transparency", transparency, 0.01f, 0.0f, 1.0f))
			materialAsset->SetTransparency(transparency);

		bool castsShadows = materialAsset->IsShadowCasting();
		if (ImGuiEx::Property("Cast Shadows", castsShadows))
			materialAsset->SetShadowCasting(castsShadows);

		bool useNormalMap = materialAsset->IsUsingNormalMap();
		if (ImGuiEx::Property("Use Normal Map", useNormalMap))
			materialAsset->SetUseNormalMap(useNormalMap);

		ImGuiEx::EndPropertyGrid();

		ImGui::Spacing();
		ImGui::TextUnformatted("Texture Maps");

		AssetHandle albedoMapHandle = materialAsset->GetAlbedoMapHandle();
		if (RenderTextureSlot("Albedo", albedoMapHandle, materialAsset->GetAlbedoMap(), AssetType::Texture))
		{
			if (albedoMapHandle)
				materialAsset->SetAlbedoMap(albedoMapHandle);
			else
				materialAsset->ClearAlbedoMap();
		}

		AssetHandle normalMapHandle = materialAsset->GetNormalMapHandle();
		if (RenderTextureSlot("Normal", normalMapHandle, materialAsset->GetNormalMap(), AssetType::Texture))
		{
			if (normalMapHandle)
			{
				materialAsset->SetNormalMap(normalMapHandle);
				materialAsset->SetUseNormalMap(true);
			}
			else
			{
				materialAsset->ClearNormalMap();
				materialAsset->SetUseNormalMap(false);
			}
		}

		AssetHandle metalnessMapHandle = materialAsset->GetMetalnessMapHandle();
		if (RenderTextureSlot("Metalness", metalnessMapHandle, materialAsset->GetMetalnessMap(), AssetType::Texture))
		{
			if (metalnessMapHandle)
				materialAsset->SetMetalnessMap(metalnessMapHandle);
			else
				materialAsset->ClearMetalnessMap();
		}

		AssetHandle roughnessMapHandle = materialAsset->GetRoughnessMapHandle();
		if (RenderTextureSlot("Roughness", roughnessMapHandle, materialAsset->GetRoughnessMap(), AssetType::Texture))
		{
			if (roughnessMapHandle)
				materialAsset->SetRoughnessMap(roughnessMapHandle);
			else
				materialAsset->ClearRoughnessMap();
		}

		ImGui::TreePop();
	}

	bool MaterialsPanel::RenderTextureSlot(const char* label, AssetHandle& handle, const Ref<Texture2D>& texture, AssetType expectedType)
	{
		bool modified = false;

		ImGui::PushID(label);
		ImGui::TextUnformatted(label);

		Ref<Texture2D> previewTexture = texture ? texture : m_CheckerboardTexture;
		if (previewTexture)
			ImGuiEx::Image(previewTexture, ImVec2(64.0f, 64.0f));
		else
			ImGui::Dummy(ImVec2(64.0f, 64.0f));

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
			{
				const size_t itemCount = payload->DataSize / sizeof(AssetHandle);
				if (itemCount > 0)
				{
					const AssetHandle droppedHandle = *(const AssetHandle*)payload->Data;
					if (AssetManager::GetAssetType(droppedHandle) == expectedType)
					{
						handle = droppedHandle;
						modified = true;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (handle)
		{
			const AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
			if (metadata.IsValid())
				ImGui::TextWrapped("%s", metadata.FilePath.generic_string().c_str());
		}
		else
		{
			ImGui::TextDisabled("Drag a texture asset here");
		}

		if (handle && ImGui::Button("Clear"))
		{
			handle = 0;
			modified = true;
		}

		ImGui::Separator();
		ImGui::PopID();
		return modified;
	}

	void MaterialsPanel::SaveMaterial(AssetHandle handle, const Ref<MaterialAsset>& materialAsset) const
	{
		if (!handle || !materialAsset || !AssetManager::IsPhysicalAsset(handle))
			return;

		AssetImporter::Serialize(materialAsset.As<Asset>());
	}

}
