#include "lpch.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/ScriptableEntity.h"

namespace Lux {

	Entity::Entity(entt::entity handle, Scene* scene)
		: m_EntityHandle(handle), m_Scene(scene)
	{

	}

	ScriptComponent::ScriptComponent() = default;
	ScriptComponent::ScriptComponent(const ScriptComponent&) = default;
	ScriptComponent& ScriptComponent::operator=(const ScriptComponent&) = default;
	ScriptComponent::~ScriptComponent() = default;

}
