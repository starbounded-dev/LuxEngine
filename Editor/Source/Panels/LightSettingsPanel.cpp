#include "lpch.h"
#include "LightSettingsPanel.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/AssetMetadata.h"
#include "Lux/Project/Project.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"

#include "Lux/ImGui/ImGuiEx.h"

#include <imgui/imgui.h>
#include <glm/gtc/type_ptr.hpp>

namespace Lux {

	LightSettingsPanel::LightSettingsPanel()
	{
	}

	void LightSettingsPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		ImGui::Begin("Light Settings", &isOpen);

		auto scene = GetSceneContext();
		if (!scene)
		{
			ImGui::Text("No scene selected");
			ImGui::End();
			return;
		}

		RenderDirectionalLight();
		ImGui::Separator();
		RenderPointLights();
		ImGui::Separator();
		RenderSpotLights();
		ImGui::Separator();
		RenderSkyLight();

		ImGui::End();
	}

	void LightSettingsPanel::RenderDirectionalLight()
	{
		auto scene = GetSceneContext();
		if (!scene)
			return;

		auto view = scene->GetAllEntitiesWith<DirectionalLightComponent>();

		if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int lightCount = 0;
			for (auto e : view)
			{
				Entity entity = { e, scene.get() };
				lightCount++;
				auto& light = entity.GetComponent<DirectionalLightComponent>();
				auto name = entity.GetComponent<TagComponent>().Tag;

				ImGui::PushID(lightCount);
				ImGui::Text("%s", name.c_str());

				ImGuiEx::BeginPropertyGrid();

				glm::vec3 color = light.Radiance;
				if (ImGuiEx::PropertyColor("Color", color))
				{
					light.Radiance = color;
				}

				if (ImGuiEx::Property("Intensity", light.Intensity, 0.1f, 0.0f, 100.0f))
				{
				}

				glm::vec3 rotation = glm::degrees(entity.GetComponent<TransformComponent>().Rotation);
				if (ImGuiEx::Property("Rotation", rotation, 1.0f))
				{
					entity.GetComponent<TransformComponent>().Rotation = glm::radians(rotation);
				}

				ImGuiEx::Property("Cast Shadows", light.CastShadows);
				ImGuiEx::Property("Soft Shadows", light.SoftShadows);
				ImGuiEx::Property("Light Size", light.LightSize, 0.01f, 0.0f, 10.0f);
				ImGuiEx::Property("Shadow Amount", light.ShadowAmount, 0.01f, 0.0f, 1.0f);

				ImGuiEx::EndPropertyGrid();

				ImGui::PopID();
			}

			if (lightCount == 0)
			{
				ImGui::Text("No directional light in scene");
			}
		}
	}

	void LightSettingsPanel::RenderPointLights()
	{
		auto scene = GetSceneContext();
		if (!scene)
			return;

		auto view = scene->GetAllEntitiesWith<PointLightComponent>();

		if (ImGui::CollapsingHeader("Point Lights", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int lightCount = 0;
			for (auto e : view)
			{
				Entity entity = { e, scene.get() };
				lightCount++;
				auto& light = entity.GetComponent<PointLightComponent>();
				auto name = entity.GetComponent<TagComponent>().Tag;

				ImGui::PushID(lightCount);
				ImGui::Text("%s", name.c_str());

				ImGuiEx::BeginPropertyGrid();

				glm::vec3 color = light.Radiance;
				if (ImGuiEx::PropertyColor("Color", color))
				{
					light.Radiance = color;
				}

				ImGuiEx::Property("Intensity", light.Intensity, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Radius", light.Radius, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Falloff", light.Falloff, 0.1f, 0.0f, 10.0f);
				ImGuiEx::Property("Min Radius", light.MinRadius, 0.001f, 0.0f, 1.0f);
				ImGuiEx::Property("Light Size", light.LightSize, 0.01f, 0.0f, 10.0f);
				ImGuiEx::Property("Cast Shadows", light.CastShadows);
				ImGuiEx::Property("Soft Shadows", light.SoftShadows);

				ImGuiEx::EndPropertyGrid();

				ImGui::PopID();
			}

			if (lightCount == 0)
			{
				ImGui::Text("No point lights in scene");
			}
		}
	}

	void LightSettingsPanel::RenderSpotLights()
	{
		auto scene = GetSceneContext();
		if (!scene)
			return;

		auto view = scene->GetAllEntitiesWith<SpotLightComponent>();

		if (ImGui::CollapsingHeader("Spot Lights", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int lightCount = 0;
			for (auto e : view)
			{
				Entity entity = { e, scene.get() };
				lightCount++;
				auto& light = entity.GetComponent<SpotLightComponent>();
				auto name = entity.GetComponent<TagComponent>().Tag;

				ImGui::PushID(lightCount);
				ImGui::Text("%s", name.c_str());

				ImGuiEx::BeginPropertyGrid();

				glm::vec3 color = light.Radiance;
				if (ImGuiEx::PropertyColor("Color", color))
				{
					light.Radiance = color;
				}

				ImGuiEx::Property("Intensity", light.Intensity, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Range", light.Range, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Falloff", light.Falloff, 0.1f, 0.0f, 10.0f);
				ImGuiEx::Property("Angle", light.Angle, 1.0f, 0.0f, 180.0f);
				ImGuiEx::Property("Angle Atten", light.AngleAttenuation, 0.1f, 0.0f, 1.0f);
				ImGuiEx::Property("Cast Shadows", light.CastShadows);
				ImGuiEx::Property("Soft Shadows", light.SoftShadows);

				ImGuiEx::EndPropertyGrid();

				ImGui::PopID();
			}

			if (lightCount == 0)
			{
				ImGui::Text("No spot lights in scene");
			}
		}
	}

	void LightSettingsPanel::RenderSkyLight()
	{
		auto scene = GetSceneContext();
		if (!scene)
			return;

		auto view = scene->GetAllEntitiesWith<SkyLightComponent>();

		if (ImGui::CollapsingHeader("Sky Light (IBL)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int lightCount = 0;
			for (auto e : view)
			{
				Entity entity = { e, scene.get() };
				lightCount++;
				auto& light = entity.GetComponent<SkyLightComponent>();
				auto name = entity.GetComponent<TagComponent>().Tag;

				ImGui::PushID(lightCount);
				ImGui::Text("%s", name.c_str());

				ImGuiEx::BeginPropertyGrid();
				ImGuiEx::Property("Intensity", light.Intensity, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Lod", light.Lod, 0.01f, 0.0f, 10.0f);
				ImGuiEx::Property("Dynamic Sky", light.DynamicSky);
				ImGuiEx::Property("Turbidity", light.TurbidityAzimuthInclination.x, 0.01f, 0.0f, 20.0f);
				ImGuiEx::Property("Azimuth", light.TurbidityAzimuthInclination.y, 0.01f, -360.0f, 360.0f);
				ImGuiEx::Property("Inclination", light.TurbidityAzimuthInclination.z, 0.01f, -180.0f, 180.0f);
				ImGuiEx::EndPropertyGrid();

				std::string envLabel = "None";
				bool isEnvironmentValid = false;
				if (light.EnvironmentMap != 0)
				{
					if (AssetManager::IsAssetHandleValid(light.EnvironmentMap)
						&& AssetManager::GetAssetType(light.EnvironmentMap) == AssetType::EnvMap)
					{
						const AssetMetadata& metadata = Project::GetActive()->GetEditorAssetManager()->GetMetadata(light.EnvironmentMap);
						envLabel = metadata.FilePath.filename().string();
						isEnvironmentValid = true;
					}
					else
					{
						envLabel = "Invalid";
					}
				}

				ImVec2 buttonLabelSize = ImGui::CalcTextSize(envLabel.c_str());
				buttonLabelSize.x += 20.0f;
				float buttonLabelWidth = glm::max<float>(100.0f, buttonLabelSize.x);

				ImGui::Button(envLabel.c_str(), ImVec2(buttonLabelWidth, 0.0f));
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
					{
						AssetHandle handle = *(AssetHandle*)payload->Data;
						if (AssetManager::GetAssetType(handle) == AssetType::EnvMap)
						{
							light.EnvironmentMap = handle;
						}
					}
					ImGui::EndDragDropTarget();
				}

				if (isEnvironmentValid)
				{
					ImGui::SameLine();
					ImVec2 xLabelSize = ImGui::CalcTextSize("X");
					float buttonSize = xLabelSize.y + ImGui::GetStyle().FramePadding.y * 2.0f;
					if (ImGui::Button("X", ImVec2(buttonSize, buttonSize)))
						light.EnvironmentMap = 0;
				}

				ImGui::SameLine();
				ImGui::Text("Environment Map");

				ImGui::PopID();
			}

			if (lightCount == 0)
			{
				ImGui::Text("No sky light in scene");
			}
		}
	}

}
