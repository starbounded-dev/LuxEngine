#include "lpch.h"
#include "LightSettingsPanel.h"

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
				ImGuiEx::Property("Cast Shadows", light.CastShadows);

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
				ImGuiEx::EndPropertyGrid();

				ImGui::Text("Environment Map: (pending)");

				ImGui::PopID();
			}

			if (lightCount == 0)
			{
				ImGui::Text("No sky light in scene");
			}
		}
	}

}
