// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Scene/Components.h"

#include <string_view>

namespace Lux {

	// The manual page (EditorDocs topic) for a component, shown as a help button on its inspector
	// section. A component without an entry has no button. Each topic must exist under
	// Editor/Resources/Docs; the Help panel reports a missing one.
	template<typename TComponent>
	struct ComponentDocTopic
	{
		static constexpr std::string_view Topic = {};
		static constexpr std::string_view Title = {};
	};

#define LUX_COMPONENT_DOC_TOPIC(ComponentType, TopicString, TitleString) \
	template<> struct ComponentDocTopic<ComponentType>                    \
	{                                                                    \
		static constexpr std::string_view Topic = TopicString;           \
		static constexpr std::string_view Title = TitleString;           \
	};

	LUX_COMPONENT_DOC_TOPIC(TransformComponent, "components/transform", "Transform")
	LUX_COMPONENT_DOC_TOPIC(StaticMeshComponent, "components/static-mesh", "Static Mesh")
	LUX_COMPONENT_DOC_TOPIC(CameraComponent, "components/camera", "Camera")
	LUX_COMPONENT_DOC_TOPIC(ScriptComponent, "scripting/csharp-scripts", "C# Scripts")
	LUX_COMPONENT_DOC_TOPIC(DirectionalLightComponent, "components/lights", "Lights")
	LUX_COMPONENT_DOC_TOPIC(PointLightComponent, "components/lights", "Lights")
	LUX_COMPONENT_DOC_TOPIC(SpotLightComponent, "components/lights", "Lights")
	LUX_COMPONENT_DOC_TOPIC(AreaLightComponent, "components/lights", "Lights")
	LUX_COMPONENT_DOC_TOPIC(SkyLightComponent, "components/lights", "Lights")
	LUX_COMPONENT_DOC_TOPIC(RigidBodyComponent, "components/physics", "Physics")
	LUX_COMPONENT_DOC_TOPIC(CharacterControllerComponent, "components/physics", "Physics")
	LUX_COMPONENT_DOC_TOPIC(BoxColliderComponent, "components/physics", "Physics")
	LUX_COMPONENT_DOC_TOPIC(SphereColliderComponent, "components/physics", "Physics")
	LUX_COMPONENT_DOC_TOPIC(CapsuleColliderComponent, "components/physics", "Physics")
	LUX_COMPONENT_DOC_TOPIC(MeshColliderComponent, "components/physics", "Physics")
	LUX_COMPONENT_DOC_TOPIC(CompoundColliderComponent, "components/physics", "Physics")
	LUX_COMPONENT_DOC_TOPIC(AudioSourceComponent, "components/audio", "Audio")
	LUX_COMPONENT_DOC_TOPIC(AudioListenerComponent, "components/audio", "Audio")
	LUX_COMPONENT_DOC_TOPIC(SpriteRendererComponent, "components/2d", "2D: Sprites, Circles and Text")
	LUX_COMPONENT_DOC_TOPIC(CircleRendererComponent, "components/2d", "2D: Sprites, Circles and Text")
	LUX_COMPONENT_DOC_TOPIC(TextComponent, "components/2d", "2D: Sprites, Circles and Text")
	LUX_COMPONENT_DOC_TOPIC(PrefabComponent, "editor/prefabs", "Prefabs")

#undef LUX_COMPONENT_DOC_TOPIC

}
