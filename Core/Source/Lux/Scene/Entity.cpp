#include "lpch.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/ScriptableEntity.h"

#include <algorithm>

namespace Lux {

	Entity::Entity(entt::entity handle, Scene* scene)
		: m_EntityHandle(handle), m_Scene(scene)
	{

	}

	Entity Entity::GetParent() const
	{
		if (!m_Scene || !m_Scene->m_Registry.template has<RelationshipComponent>(m_EntityHandle))
			return {};

		const auto& relationship = m_Scene->m_Registry.get<RelationshipComponent>(m_EntityHandle);
		if (relationship.ParentHandle == 0)
			return {};

		return m_Scene->GetEntityByUUID(relationship.ParentHandle);
	}

	void Entity::SetParent(Entity parent)
	{
		if (!*this || !m_Scene)
			return;

		if (parent && parent.m_Scene != m_Scene)
			return;

		if (parent == *this)
			return;

		auto& relationship = GetComponent<RelationshipComponent>();
		const UUID selfUUID = GetUUID();

		// Prevent cycles
		for (Entity current = parent; current; current = current.GetParent())
		{
			if (current == *this)
				return;
		}

		Entity currentParent = GetParent();
		if (currentParent)
		{
			auto& siblings = currentParent.GetComponent<RelationshipComponent>().Children;
			siblings.erase(std::remove(siblings.begin(), siblings.end(), selfUUID), siblings.end());
		}

		relationship.ParentHandle = 0;

		if (!parent)
			return;

		auto& parentRelationship = parent.GetComponent<RelationshipComponent>();
		if (std::find(parentRelationship.Children.begin(), parentRelationship.Children.end(), selfUUID) == parentRelationship.Children.end())
			parentRelationship.Children.emplace_back(selfUUID);

		relationship.ParentHandle = parent.GetUUID();
	}

	std::vector<UUID>& Entity::Children()
	{
		return GetComponent<RelationshipComponent>().Children;
	}

	bool Entity::HasParent() const
	{
		return (bool)GetParent();
	}

	ScriptComponent::ScriptComponent() = default;
	ScriptComponent::ScriptComponent(const ScriptComponent&) = default;
	ScriptComponent& ScriptComponent::operator=(const ScriptComponent&) = default;
	ScriptComponent::~ScriptComponent() = default;

}
