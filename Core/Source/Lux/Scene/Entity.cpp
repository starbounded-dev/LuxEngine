#include "lpch.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/ScriptableEntity.h"

#include <algorithm>

namespace Lux {

	bool Entity::IsValid() const
	{
		return m_Scene && m_EntityHandle != entt::null && m_Scene->m_Registry.valid(m_EntityHandle);
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

		Entity currentParent = GetParent();
		if (currentParent == parent)
			return;

		// Prevent cycles
		for (Entity current = parent; current; current = current.GetParent())
		{
			if (current == *this)
				return;
		}

		if (currentParent)
			currentParent.RemoveChild(*this);

		SetParentUUID(0);

		if (!parent)
			return;

		auto& parentRelationship = parent.GetComponent<RelationshipComponent>();
		const UUID selfUUID = GetUUID();
		if (std::find(parentRelationship.Children.begin(), parentRelationship.Children.end(), selfUUID) == parentRelationship.Children.end())
			parentRelationship.Children.emplace_back(selfUUID);

		SetParentUUID(parent.GetUUID());
	}

	std::vector<UUID>& Entity::Children()
	{
		return GetComponent<RelationshipComponent>().Children;
	}

	const std::vector<UUID>& Entity::Children() const
	{
		return GetComponent<RelationshipComponent>().Children;
	}

	bool Entity::RemoveChild(Entity child)
	{
		const UUID childID = child.GetUUID();
		std::vector<UUID>& children = Children();
		auto it = std::find(children.begin(), children.end(), childID);
		if (it == children.end())
			return false;

		children.erase(it);
		return true;
	}

	bool Entity::HasParent() const
	{
		return (bool)GetParent();
	}

	bool Entity::IsAncestorOf(Entity entity) const
	{
		if (!*this || !entity)
			return false;

		for (Entity parent = entity.GetParent(); parent; parent = parent.GetParent())
		{
			if (parent == *this)
				return true;
		}

		return false;
	}

	ScriptComponent::ScriptComponent() = default;
	ScriptComponent::ScriptComponent(const ScriptComponent&) = default;
	ScriptComponent& ScriptComponent::operator=(const ScriptComponent&) = default;
	ScriptComponent::~ScriptComponent() = default;

}
