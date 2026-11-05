// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "LightSettingsPanel.h"

#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"

#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Renderer/SceneEnvironment.h"
#include "Lux/Core/Events/KeyEvent.h"

#include <imgui/imgui.h>
#include <glm/gtc/type_ptr.hpp>

namespace Lux {

	LightSettingsPanel::LightSettingsPanel()
	{
	}

	void LightSettingsPanel::OnImGuiRender(bool& isOpen)
	{
		m_IsFocused = false;
		if (!isOpen)
			return;

		ImGui::SetNextWindowSize({560.0f, 680.0f}, ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Light Settings", &isOpen))
		{
			ImGui::End();
			return;
		}
		m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		auto scene = GetSceneContext();
		if (!scene)
		{
			ImGuiEx::PanelEmptyState("##lights_scene", "No scene selected",
									 "Open a scene to edit its lights.");
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

	void LightSettingsPanel::OnEvent(Event& event)
	{
		if (!m_IsFocused)
			return;
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(
			[](KeyPressedEvent&) { return ImGui::GetIO().WantTextInput || GImGui->OpenPopupStack.Size > 0; });
	}

	void LightSettingsPanel::RenderDirectionalLight()
	{
		ImGuiEx::ScopedID section("DirectionalLights");
		auto scene = GetSceneContext();
		if (!scene)
			return;

		auto view = scene->GetAllEntitiesWith<DirectionalLightComponent>();

		if (ImGuiEx::BoundedCollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int lightCount = 0;
			for (auto e : view)
			{
				Entity entity = { e, scene.get() };
				lightCount++;
				auto& light = entity.GetComponent<DirectionalLightComponent>();
				const auto& name = entity.GetComponent<TagComponent>().Tag;

				ImGuiEx::ScopedID id(reinterpret_cast<const void*>(static_cast<uint64_t>(entity.GetUUID())));
				ImGui::TextWrapped("%s", name.c_str());

				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 12.0f, ImGui::GetFontSize() * 28.0f);

				glm::vec3 color = light.Radiance;
				if (ImGuiEx::PropertyColor("Color", color))
				{
					light.Radiance = color;
				}

				if (ImGuiEx::Property("Intensity", light.Intensity, 0.1f, 0.0f, 100.0f))
				{
				}

				glm::vec3 rotation = glm::degrees(entity.GetComponent<TransformComponent>().GetRotationEuler());
				if (ImGuiEx::Property("Rotation", rotation, 1.0f))
				{
					entity.GetComponent<TransformComponent>().SetRotationEuler(glm::radians(rotation));
				}

				ImGuiEx::Property("Cast Shadows", light.CastShadows);
				ImGuiEx::Property("Soft Shadows", light.SoftShadows);
				ImGuiEx::Property("Light Size", light.LightSize, 0.01f, 0.0f, 10.0f);
				ImGuiEx::Property("Shadow Amount", light.ShadowAmount, 0.01f, 0.0f, 1.0f);
				ImGuiEx::Property("Shadow Distance", light.ShadowDistance, 1.0f, 0.0f, 10000.0f);
				ImGuiEx::Property("Shadow Tier", light.ShadowResolutionTier, 0u, 3u);

				ImGuiEx::EndPropertyGrid();
			}

			if (lightCount == 0)
			{
				ImGui::Text("No directional light in scene");
			}
		}
	}

	void LightSettingsPanel::RenderPointLights()
	{
		ImGuiEx::ScopedID section("PointLights");
		auto scene = GetSceneContext();
		if (!scene)
			return;

		auto view = scene->GetAllEntitiesWith<PointLightComponent>();

		if (ImGuiEx::BoundedCollapsingHeader("Point Lights", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int lightCount = 0;
			for (auto e : view)
			{
				Entity entity = { e, scene.get() };
				lightCount++;
				auto& light = entity.GetComponent<PointLightComponent>();
				const auto& name = entity.GetComponent<TagComponent>().Tag;

				ImGuiEx::ScopedID id(reinterpret_cast<const void*>(static_cast<uint64_t>(entity.GetUUID())));
				ImGui::TextWrapped("%s", name.c_str());

				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 12.0f, ImGui::GetFontSize() * 28.0f);

				glm::vec3 color = light.Radiance;
				if (ImGuiEx::PropertyColor("Color", color))
				{
					light.Radiance = color;
				}

				ImGuiEx::Property("Intensity", light.Intensity, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Radius", light.Radius, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Light Size", light.LightSize, 0.01f, 0.0f, 10.0f);
				ImGuiEx::Property("Cast Shadows", light.CastsShadows);
				ImGuiEx::Property("Soft Shadows", light.SoftShadows);

				ImGuiEx::EndPropertyGrid();
			}

			if (lightCount == 0)
			{
				ImGui::Text("No point lights in scene");
			}
		}
	}

	void LightSettingsPanel::RenderSpotLights()
	{
		ImGuiEx::ScopedID section("SpotLights");
		auto scene = GetSceneContext();
		if (!scene)
			return;

		auto view = scene->GetAllEntitiesWith<SpotLightComponent>();

		if (ImGuiEx::BoundedCollapsingHeader("Spot Lights", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int lightCount = 0;
			for (auto e : view)
			{
				Entity entity = { e, scene.get() };
				lightCount++;
				auto& light = entity.GetComponent<SpotLightComponent>();
				const auto& name = entity.GetComponent<TagComponent>().Tag;

				ImGuiEx::ScopedID id(reinterpret_cast<const void*>(static_cast<uint64_t>(entity.GetUUID())));
				ImGui::TextWrapped("%s", name.c_str());

				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 12.0f, ImGui::GetFontSize() * 28.0f);

				glm::vec3 color = light.Radiance;
				if (ImGuiEx::PropertyColor("Color", color))
				{
					light.Radiance = color;
				}

				ImGuiEx::Property("Intensity", light.Intensity, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Range", light.Range, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Light Size", light.Falloff, 0.01f, 0.0f, 10.0f);
				ImGuiEx::Property("Angle", light.Angle, 1.0f, 0.0f, 180.0f);
				ImGuiEx::Property("Angle Atten", light.AngleAttenuation, 0.1f, 0.0f, 1.0f);
				ImGuiEx::Property("Cast Shadows", light.CastsShadows);
				ImGuiEx::Property("Soft Shadows", light.SoftShadows);
				ImGuiEx::Property("Shadow Distance", light.ShadowDistance, 1.0f, 0.0f, 10000.0f);
				ImGuiEx::Property("Shadow Tier", light.ShadowResolutionTier, 0u, 3u);

				ImGuiEx::EndPropertyGrid();
			}

			if (lightCount == 0)
			{
				ImGui::Text("No spot lights in scene");
			}
		}
	}

	void LightSettingsPanel::RenderSkyLight()
	{
		ImGuiEx::ScopedID section("SkyLights");
		auto scene = GetSceneContext();
		if (!scene)
			return;

		auto view = scene->GetAllEntitiesWith<SkyLightComponent>();

		if (ImGuiEx::BoundedCollapsingHeader("Sky Light (IBL)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int lightCount = 0;
			for (auto e : view)
			{
				Entity entity = { e, scene.get() };
				lightCount++;
				auto& light = entity.GetComponent<SkyLightComponent>();
				const auto& name = entity.GetComponent<TagComponent>().Tag;

				ImGuiEx::ScopedID id(reinterpret_cast<const void*>(static_cast<uint64_t>(entity.GetUUID())));
				ImGui::TextWrapped("%s", name.c_str());

				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 12.0f, ImGui::GetFontSize() * 28.0f);
				ImGuiEx::Property("Intensity", light.Intensity, 0.1f, 0.0f, 100.0f);
				ImGuiEx::Property("Lod", light.Lod, 0.01f, 0.0f, 10.0f);
				ImGuiEx::Property("Dynamic Sky", light.DynamicSky);
				ImGuiEx::Property("Turbidity", light.TurbidityAzimuthInclination.x, 0.01f, 0.0f, 20.0f);
				ImGuiEx::Property("Azimuth", light.TurbidityAzimuthInclination.y, 0.01f, -360.0f, 360.0f);
				ImGuiEx::Property("Inclination", light.TurbidityAzimuthInclination.z, 0.01f, -180.0f, 180.0f);
				ImGuiEx::EndPropertyGrid();

				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 12.0f, ImGui::GetFontSize() * 28.0f);
				ImGuiEx::PropertyAssetReference<Environment>(
					"Environment Map", light.SceneEnvironment,
					"Choose an environment map from the project, or drag one from Content Browser.");
				ImGuiEx::EndPropertyGrid();
			}

			if (lightCount == 0)
			{
				ImGui::Text("No sky light in scene");
			}
		}
	}

}
