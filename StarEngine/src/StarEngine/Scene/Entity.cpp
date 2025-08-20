#include "sepch.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "StarEngine/Scene/Entity.h"
#include "StarEngine/Scene/Scene.h" // Include the full definition of Scene

namespace StarEngine {

	Entity Entity::GetParent() const
	{
		return m_Scene->TryGetEntityWithUUID(GetParentUUID());
	}

	bool Entity::IsAncestorOf(Entity entity) const
	{
		const auto& children = Children();

		if (children.empty())
			return false;

		for (UUID child : children)
		{
			if (child == entity.GetUUID())
				return true;
		}

		for (UUID child : children)
		{
			if (m_Scene->GetEntityWithUUID(child).IsAncestorOf(entity))
				return true;
		}

		return false;
	}

	bool Entity::IsValid() const { return (m_EntityHandle != entt::null) && m_Scene && m_Scene->m_Registry.valid(m_EntityHandle); }
	Entity::operator bool() const { return IsValid(); }
}
