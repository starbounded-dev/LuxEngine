#include "lpch.h"
#include "MaterialEditorPanel.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Core/Application.h"

#include <glm/gtc/type_ptr.hpp>

#include <imgui/imgui.h>
#include "imgui/imgui_internal.h"

#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/ImGui/ImGuiFonts.h"

namespace Lux {

	MaterialEditorPanel::MaterialEditorPanel()
	{
	}

	void MaterialEditorPanel::OpenMaterial(AssetHandle handle)
	{
		m_CurrentMaterial = handle;
		m_IsDirty = false;
	}

	void MaterialEditorPanel::OpenMaterial(const std::filesystem::path& path)
	{
		auto project = Project::GetActive();
		if (!project)
			return;

		AssetHandle handle = 0;
		const auto& registry = project->GetEditorAssetManager()->GetAssetRegistry();
		for (const auto& [h, meta] : registry)
		{
			if (meta.FilePath == path)
			{
				handle = h;
				break;
			}
		}

		if (handle)
		{
			OpenMaterial(handle);
		}
	}

	void MaterialEditorPanel::CloseMaterial()
	{
		m_CurrentMaterial.reset();
		m_CurrentPath.clear();
		m_IsDirty = false;
	}

	void MaterialEditorPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		ImGui::Begin("Material Editor", &isOpen);

		if (!m_CurrentMaterial)
		{
			ImGui::Text("No material selected");
			ImGui::Text("Select a .mat file from the Content Browser");
			ImGui::End();
			return;
		}

		auto project = Project::GetActive();
		if (!project)
		{
			ImGui::Text("No project active");
			ImGui::End();
			return;
		}

		auto material = AssetManager::GetAsset<MaterialAsset>(*m_CurrentMaterial);
		if (!material)
		{
			ImGui::Text("Failed to load material");
			if (ImGui::Button("Close"))
			{
				CloseMaterial();
			}
			ImGui::End();
			return;
		}

		ImGui::Separator();

		RenderMaterialProperties(material);

		ImGui::Separator();

		if (ImGui::Button("Save"))
		{
			AssetImporter::Serialize(material.As<Asset>());
			m_IsDirty = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Close"))
		{
			CloseMaterial();
		}

		ImGui::End();
	}

	void MaterialEditorPanel::RenderMaterialProperties(Ref<MaterialAsset> material)
	{
		if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGuiEx::BeginPropertyGrid();

			glm::vec3 albedo = material->GetAlbedoColor();
			if (ImGuiEx::PropertyColor("Albedo", albedo))
			{
				material->SetAlbedoColor(albedo);
				m_IsDirty = true;
			}

			float metalness = material->GetMetalness();
			if (ImGuiEx::Property("Metalness", metalness, 0.01f, 0.0f, 1.0f))
			{
				material->SetMetalness(metalness);
				m_IsDirty = true;
			}

			float roughness = material->GetRoughness();
			if (ImGuiEx::Property("Roughness", roughness, 0.01f, 0.0f, 1.0f))
			{
				material->SetRoughness(roughness);
				m_IsDirty = true;
			}

			float emission = material->GetEmission();
			if (ImGuiEx::Property("Emission", emission, 0.01f, 0.0f, 100.0f))
			{
				material->SetEmission(emission);
				m_IsDirty = true;
			}

			float transparency = material->GetTransparency();
			if (ImGuiEx::Property("Transparency", transparency, 0.01f, 0.0f, 1.0f))
			{
				material->SetTransparency(transparency);
				m_IsDirty = true;
			}

			ImGuiEx::EndPropertyGrid();
		}

		if (ImGui::CollapsingHeader("Shadows"))
		{
			ImGuiEx::BeginPropertyGrid();
			bool castsShadows = material->IsShadowCasting();
			if (ImGuiEx::Property("Cast Shadows", castsShadows))
			{
				material->SetShadowCasting(castsShadows);
				m_IsDirty = true;
			}
			ImGuiEx::EndPropertyGrid();
		}

		if (ImGui::CollapsingHeader("Maps", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Texture slots");
			ImGui::Text("(File dialog integration pending)");
		}
	}

	void MaterialEditorPanel::RenderTextureSlot(const char* label, AssetHandle& handle, Ref<Texture2D>& texture, const char* buttonLabel, bool& hasSlot)
	{
		ImGui::Text("%s", label);
		ImGui::SameLine();

		if (texture)
		{
			auto* imguiRenderer = Lux::Application::Get().GetImGuiLayer()->GetImGuiRenderer();
			ImTextureID texId = imguiRenderer->CreateFrameTexture(texture->GetImage()->GetHandle().Get(), nvrhi::AllSubresources);
			ImGui::Image(texId, ImVec2(64, 64));
			ImGui::SameLine();
		}

		if (ImGui::Button(buttonLabel))
		{
		}

		if (handle != 0)
		{
			ImGui::SameLine();
			if (ImGui::Button("Clear"))
			{
				handle = 0;
				texture.reset();
				m_IsDirty = true;
			}
		}
	}

}
