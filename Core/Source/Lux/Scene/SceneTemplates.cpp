// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SceneTemplates.h"

#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"

namespace Lux::SceneTemplates {

	void Populate2DStarter(Scene& scene)
	{
		scene.SetName("Main");

		Entity camera = scene.CreateEntity("Main Camera");
		auto& cameraComponent = camera.AddComponent<CameraComponent>();
		cameraComponent.Camera.SetOrthographic(10.0f, -100.0f, 100.0f);
		cameraComponent.SolidBackground = true;

		const auto addBox = [&scene](const char* name, const glm::vec3& position, const glm::vec3& scale, const glm::vec4& color, RigidBody2DComponent::Type bodyType)
		{
			Entity box = scene.CreateEntity(name);
			auto& transform = box.GetComponent<TransformComponent>();
			transform.Translation = position;
			transform.Scale = scale;
			box.AddComponent<SpriteRendererComponent>(color);
			box.AddComponent<RigidBody2DComponent>().BodyType = bodyType;
			box.AddComponent<BoxCollider2DComponent>();
		};
		addBox("Ground", { 0.0f, -3.5f, 0.0f }, { 14.0f, 1.0f, 1.0f }, { 0.35f, 0.55f, 0.38f, 1.0f }, RigidBody2DComponent::Type::Static);
		addBox("Box", { 0.0f, 2.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.95f, 0.62f, 0.2f, 1.0f }, RigidBody2DComponent::Type::Dynamic);
	}

}
