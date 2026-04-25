#pragma once

namespace Lux {

	template<typename T, typename... Args>
	T& Entity::AddComponent(Args&&... args)
	{
		LUX_CORE_ASSERT(!HasComponent<T>(), "Entity already has component!");
		T& component = m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
		m_Scene->OnComponentAdded<T>(*this, component);
		return component;
	}

	template<typename T, typename... Args>
	T& Entity::AddOrReplaceComponent(Args&&... args)
	{
		T& component = m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
		m_Scene->OnComponentAdded<T>(*this, component);
		return component;
	}

	template<typename T>
	T& Entity::GetComponent()
	{
		LUX_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
		return m_Scene->m_Registry.get<T>(m_EntityHandle);
	}

	template<typename T>
	const T& Entity::GetComponent() const
	{
		LUX_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
		return m_Scene->m_Registry.get<T>(m_EntityHandle);
	}

	template<typename T>
	T* Entity::TryGetComponent()
	{
		LUX_CORE_ASSERT(IsValid(), "Invalid entity!");
		return m_Scene->m_Registry.try_get<T>(m_EntityHandle);
	}

	template<typename T>
	const T* Entity::TryGetComponent() const
	{
		LUX_CORE_ASSERT(IsValid(), "Invalid entity!");
		return m_Scene->m_Registry.try_get<T>(m_EntityHandle);
	}

	template<typename... T>
	bool Entity::HasComponent()
	{
		LUX_CORE_ASSERT(IsValid(), "Invalid entity!");
		return (m_Scene->m_Registry.template has<T>(m_EntityHandle) && ...);
	}

	template<typename... T>
	bool Entity::HasComponent() const
	{
		LUX_CORE_ASSERT(IsValid(), "Invalid entity!");
		return (m_Scene->m_Registry.template has<T>(m_EntityHandle) && ...);
	}

	template<typename... T>
	bool Entity::HasAny()
	{
		LUX_CORE_ASSERT(IsValid(), "Invalid entity!");
		return (m_Scene->m_Registry.template has<T>(m_EntityHandle) || ...);
	}

	template<typename... T>
	bool Entity::HasAny() const
	{
		LUX_CORE_ASSERT(IsValid(), "Invalid entity!");
		return (m_Scene->m_Registry.template has<T>(m_EntityHandle) || ...);
	}

	template<typename T>
	void Entity::RemoveComponent()
	{
		LUX_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
		m_Scene->m_Registry.remove<T>(m_EntityHandle);
	}

	template<typename T>
	void Entity::RemoveComponentIfExists()
	{
		LUX_CORE_ASSERT(IsValid(), "Invalid entity!");
		if (HasComponent<T>())
			m_Scene->m_Registry.remove<T>(m_EntityHandle);
	}

}
