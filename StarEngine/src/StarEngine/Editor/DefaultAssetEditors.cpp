#include "sepch.h"
#include "DefaultAssetEditors.h"

#include "StarEngine/Asset/AssetImporter.h"
#include "StarEngine/Asset/AssetManager.h"
//#include "StarEngine/Audio/AudioFileUtils.h"
//#include "StarEngine/Audio/Sound.h"
//#include "StarEngine/Editor/NodeGraphEditor/SoundGraph/SoundGraphAsset.h"
#include "StarEngine/Renderer/Renderer.h"
#include "StarEngine/Editor/SelectionManager.h"
#include "StarEngine/Audio/AudioEngine.h"

#include "imgui_internal.h"

namespace StarEngine {

	MaterialEditor::MaterialEditor()
		: AssetEditor("Material Editor")
	{
	}

	void MaterialEditor::OnOpen()
	{
		if (!m_MaterialAsset)
			SetOpen(false);
	}

	void MaterialEditor::OnClose()
	{
		m_MaterialAsset = nullptr;
	}

	void MaterialEditor::Render()
	{
		SE_CORE_ASSERT(m_MaterialAsset);

		bool needsSerialize = false;

		bool transparent = m_MaterialAsset->IsTransparent();
		UI::BeginPropertyGrid();
		UI::PushID();
		if (UI::Property("Transparent", transparent))
		{
			if (transparent)
				m_MaterialAsset->SetMaterial(Material::Create(Renderer::GetShaderLibrary()->Get("HazelPBR_Transparent")));
			else
				m_MaterialAsset->SetMaterial(Material::Create(Renderer::GetShaderLibrary()->Get("HazelPBR_Static")));

			m_MaterialAsset->m_Transparent = transparent;
			m_MaterialAsset->SetDefaults();
			needsSerialize = true;
		}
		UI::PopID();
		UI::EndPropertyGrid();

		ImGui::Text("Shader: %s", m_MaterialAsset->GetMaterial()->GetShader()->GetName().c_str());
		
		auto getDroppedTextureHandle = []() {
			auto data = ImGui::AcceptDragDropPayload("asset_payload"); 
			if (data && data->DataSize / sizeof(AssetHandle) == 1)
			{
				AssetHandle assetHandle = *(((AssetHandle*)data->Data));
				Ref<Asset> asset = AssetManager::GetAsset<Asset>(assetHandle);
				if (asset && asset->GetAssetType() == AssetType::Texture)
				{
					return asset->Handle;
				}
			}
			return AssetHandle{ 0 };
		};

		// Albedo
		if (ImGui::CollapsingHeader("Albedo", nullptr, ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));

			auto albedoColor = m_MaterialAsset->GetAlbedoColor();
			auto albedoMap = m_MaterialAsset->GetAlbedoMap();
			bool hasAlbedoMap = albedoMap ? !albedoMap.EqualsObject(Renderer::GetWhiteTexture()) && albedoMap->GetImage() : false;
			Ref<Texture2D> albedoUITexture = hasAlbedoMap ? albedoMap : EditorResources::CheckerboardTexture;

			ImVec2 textureCursorPos = ImGui::GetCursorPos();
			UI::Image(albedoUITexture, ImVec2(64, 64));
			if (ImGui::BeginDragDropTarget())
			{
				if (auto handle = getDroppedTextureHandle(); handle != 0)
				{
					m_MaterialAsset->SetAlbedoMap(handle);
					needsSerialize = true;
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::PopStyleVar();
			if (ImGui::IsItemHovered())
			{
				if (hasAlbedoMap)
				{
					UI::ImageToolTip(albedoMap);
				}
				if (ImGui::IsItemClicked())
				{
				}
			}

			ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
			ImGui::SameLine();
			ImVec2 properCursorPos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(textureCursorPos);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
			if (hasAlbedoMap && ImGui::Button("X##AlbedoMap", ImVec2(18, 18)))
			{
				m_MaterialAsset->ClearAlbedoMap();
				needsSerialize = true;
			}
			ImGui::PopStyleVar();
			ImGui::SetCursorPos(properCursorPos);

			if (UI::ColorEdit3("##Albedo", glm::value_ptr(albedoColor), ImGuiColorEditFlags_NoInputs))
				m_MaterialAsset->SetAlbedoColor(albedoColor);
			if (ImGui::IsItemDeactivated())
				needsSerialize = true;
			ImGui::SameLine();
			ImGui::TextUnformatted("Color");

			float& emissive = m_MaterialAsset->GetEmission();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100.0f);
			UI::DragFloat("##Emission", &emissive, 0.1f, 0.0f, 20.0f);
			if (ImGui::IsItemDeactivated())
				needsSerialize = true;
			ImGui::SameLine();
			ImGui::TextUnformatted("Emission");

			ImGui::SetCursorPos(nextRowCursorPos);
		}

		if (transparent)
		{
			float& transparency = m_MaterialAsset->GetTransparency();

			UI::BeginPropertyGrid();
			UI::Property("Transparency", transparency, 0.01f, 0.0f, 1.0f);
			if (ImGui::IsItemDeactivated())
				needsSerialize = true;
			UI::EndPropertyGrid();
		}
		else
		{
			// Textures ------------------------------------------------------------------------------
			{
				// Normals
				if (ImGui::CollapsingHeader("Normals", nullptr, ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
					bool useNormalMap = m_MaterialAsset->IsUsingNormalMap();
					Ref<Texture2D> normalMap = m_MaterialAsset->GetNormalMap();

					bool hasNormalMap = normalMap ? !normalMap.EqualsObject(Renderer::GetWhiteTexture()) && normalMap->GetImage() : false;
					ImVec2 textureCursorPos = ImGui::GetCursorPos();
					UI::Image(hasNormalMap ? normalMap : EditorResources::CheckerboardTexture, ImVec2(64, 64));

					if (ImGui::BeginDragDropTarget())
					{
						if (auto handle = getDroppedTextureHandle(); handle != 0)
						{
							m_MaterialAsset->SetNormalMap(handle);
							m_MaterialAsset->SetUseNormalMap(true);
							needsSerialize = true;
						}
						ImGui::EndDragDropTarget();
					}

					ImGui::PopStyleVar();
					if (ImGui::IsItemHovered())
					{
						if (hasNormalMap)
						{
							UI::ImageToolTip(normalMap);
						}
						if (ImGui::IsItemClicked())
						{
						}
					}
					ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
					ImGui::SameLine();
					ImVec2 properCursorPos = ImGui::GetCursorPos();
					ImGui::SetCursorPos(textureCursorPos);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
					if (hasNormalMap && ImGui::Button("X##NormalMap", ImVec2(18, 18)))
					{
						m_MaterialAsset->ClearNormalMap();
						needsSerialize = true;
					}
					ImGui::PopStyleVar();
					ImGui::SetCursorPos(properCursorPos);

					if (UI::Checkbox("##Use", &useNormalMap))
						m_MaterialAsset->SetUseNormalMap(useNormalMap);
					if (ImGui::IsItemDeactivated())
						needsSerialize = true;
					ImGui::SameLine();
					ImGui::TextUnformatted("Use");

					ImGui::SetCursorPos(nextRowCursorPos);
				}
			}
			{
				// Metalness
				if (ImGui::CollapsingHeader("Metalness", nullptr, ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
					float& metalnessValue = m_MaterialAsset->GetMetalness();
					Ref<Texture2D> metalnessMap = m_MaterialAsset->GetMetalnessMap();

					bool hasMetalnessMap = metalnessMap ? !metalnessMap.EqualsObject(Renderer::GetWhiteTexture()) && metalnessMap->GetImage() : false;
					ImVec2 textureCursorPos = ImGui::GetCursorPos();
					UI::Image(hasMetalnessMap ? metalnessMap : EditorResources::CheckerboardTexture, ImVec2(64, 64));

					if (ImGui::BeginDragDropTarget())
					{
						if (auto handle = getDroppedTextureHandle(); handle != 0)
						{
							m_MaterialAsset->SetMetalnessMap(handle);
							needsSerialize = true;
						}
						ImGui::EndDragDropTarget();
					}

					ImGui::PopStyleVar();
					if (ImGui::IsItemHovered())
					{
						if (hasMetalnessMap)
						{
							UI::ImageToolTip(metalnessMap);
						}
						if (ImGui::IsItemClicked())
						{
						}
					}
					ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
					ImGui::SameLine();
					ImVec2 properCursorPos = ImGui::GetCursorPos();
					ImGui::SetCursorPos(textureCursorPos);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
					if (hasMetalnessMap && ImGui::Button("X##MetalnessMap", ImVec2(18, 18)))
					{
						m_MaterialAsset->ClearMetalnessMap();
						needsSerialize = true;
					}
					ImGui::PopStyleVar();
					ImGui::SetCursorPos(properCursorPos);
					ImGui::SetNextItemWidth(200.0f);
					UI::SliderFloat("##MetalnessInput", &metalnessValue, 0.0f, 1.0f);
					if (ImGui::IsItemDeactivated())
						needsSerialize = true;
					ImGui::SameLine();
					ImGui::TextUnformatted("Metalness Value");
					ImGui::SetCursorPos(nextRowCursorPos);
				}
			}
			{
				// Roughness
				if (ImGui::CollapsingHeader("Roughness", nullptr, ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
					float& roughnessValue = m_MaterialAsset->GetRoughness();
					Ref<Texture2D> roughnessMap = m_MaterialAsset->GetRoughnessMap();
					bool hasRoughnessMap = roughnessMap ? !roughnessMap.EqualsObject(Renderer::GetWhiteTexture()) && roughnessMap->GetImage() : false;
					ImVec2 textureCursorPos = ImGui::GetCursorPos();
					UI::Image(hasRoughnessMap ? roughnessMap : EditorResources::CheckerboardTexture, ImVec2(64, 64));

					if (ImGui::BeginDragDropTarget())
					{
						if (auto handle = getDroppedTextureHandle(); handle != 0)
						{
							m_MaterialAsset->SetRoughnessMap(handle);
							needsSerialize = true;
						}
						ImGui::EndDragDropTarget();
					}

					ImGui::PopStyleVar();
					if (ImGui::IsItemHovered())
					{
						if (hasRoughnessMap)
						{
							UI::ImageToolTip(roughnessMap);
						}
						if (ImGui::IsItemClicked())
						{
						}
					}
					ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
					ImGui::SameLine();
					ImVec2 properCursorPos = ImGui::GetCursorPos();
					ImGui::SetCursorPos(textureCursorPos);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
					if (hasRoughnessMap && ImGui::Button("X##RoughnessMap", ImVec2(18, 18)))
					{
						m_MaterialAsset->ClearRoughnessMap();
						needsSerialize = true;
					}
					ImGui::PopStyleVar();
					ImGui::SetCursorPos(properCursorPos);
					ImGui::SetNextItemWidth(200.0f);
					UI::SliderFloat("##RoughnessInput", &roughnessValue, 0.0f, 1.0f);
					if (ImGui::IsItemDeactivated())
						needsSerialize = true;
					ImGui::SameLine();
					ImGui::TextUnformatted("Roughness Value");
					ImGui::SetCursorPos(nextRowCursorPos);
				}
			}

			UI::BeginPropertyGrid();

			bool castsShadows = m_MaterialAsset->IsShadowCasting();
			if (UI::Property("Casts shadows", castsShadows))
				m_MaterialAsset->SetShadowCasting(castsShadows);

			UI::EndPropertyGrid();
		}

		if (needsSerialize && !AssetManager::IsMemoryAsset(m_MaterialAsset->Handle))
			AssetImporter::Serialize(m_MaterialAsset);
	}

	TextureViewer::TextureViewer()
		: AssetEditor("Edit Texture")
	{
		SetMinSize(200, 600);
		SetMaxSize(500, 1000);
	}

	void TextureViewer::OnOpen()
	{
		if (!m_Asset)
			SetOpen(false);
	}

	void TextureViewer::OnClose()
	{
		m_Asset = nullptr;
	}

	void TextureViewer::Render()
	{
		float textureWidth = (float)m_Asset->GetWidth();
		float textureHeight = (float)m_Asset->GetHeight();
		//float bitsPerPixel = Texture::GetBPP(m_Asset->GetFormat());
		float imageSize = ImGui::GetWindowWidth() - 40;
		imageSize = glm::min(imageSize, 500.0f);

		ImGui::SetCursorPosX(20);
		UI::Image(m_Asset.As<Texture2D>(), {imageSize, imageSize});

		UI::BeginPropertyGrid();
		UI::BeginDisabled();
		UI::Property("Width", textureWidth);
		UI::Property("Height", textureHeight);
		// UI::Property("Bits", bitsPerPixel, 0.1f, 0.0f, 0.0f, true); // TODO: Format
		UI::EndDisabled();
		UI::EndPropertyGrid();
	}

	PrefabEditor::PrefabEditor()
		: AssetEditor("Prefab Editor"), m_SceneHierarchyPanel(nullptr, SelectionContext::PrefabEditor, false)
	{
	}

	void PrefabEditor::OnOpen()
	{
		SelectionManager::DeselectAll();
	}

	void PrefabEditor::OnClose()
	{
		SelectionManager::DeselectAll(SelectionContext::PrefabEditor);
	}

	void PrefabEditor::Render()
	{
		// Need to do this in order to ensure that the scene hierarchy panel doesn't close immediately.
		// There's been some structural changes since the addition of the PanelManager.
		bool isOpen = true;
		m_SceneHierarchyPanel.OnImGuiRender(isOpen);
	}

}
