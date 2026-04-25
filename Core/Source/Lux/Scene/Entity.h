#pragma once

#include "Lux/Core/UUID.h"
#include "Components.h"

#include "entt/entt.hpp"

namespace Lux
{
	class Scene;

	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity handle, Scene* scene)
			: m_EntityHandle(handle), m_Scene(scene) {
		}

		bool IsValid() const;

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args);

		template<typename T, typename... Args>
		T& AddOrReplaceComponent(Args&&... args);

		template<typename T>
		T& GetComponent();

		template<typename T>
		const T& GetComponent() const;

		template<typename T>
		T* TryGetComponent();

		template<typename T>
		const T* TryGetComponent() const;

		template<typename... T>
		bool HasComponent();

		template<typename... T>
		bool HasComponent() const;

		template<typename... T>
		bool HasAny();

		template<typename... T>
		bool HasAny() const;

		template<typename T>
		void RemoveComponent();

		template<typename T>
		void RemoveComponentIfExists();

		operator bool() const { return m_EntityHandle != entt::null; }
		operator entt::entity() const { return m_EntityHandle; }
		operator uint32_t() const { return (uint32_t)m_EntityHandle; }

		UUID GetUUID() const { return GetComponent<IDComponent>().ID; }
		std::string& Name() { return HasComponent<TagComponent>() ? GetComponent<TagComponent>().Tag : NoName; }
		const std::string& Name() const { return HasComponent<TagComponent>() ? GetComponent<TagComponent>().Tag : NoName; }
		const std::string& GetName() const { return Name(); }
		Scene* GetScene() const { return m_Scene; }
		Entity GetParent() const;
		void SetParent(Entity parent);
		void SetParentUUID(UUID parent) { GetComponent<RelationshipComponent>().ParentHandle = parent; }
		UUID GetParentUUID() const { return GetComponent<RelationshipComponent>().ParentHandle; }
		std::vector<UUID>& Children();
		const std::vector<UUID>& Children() const;
		bool RemoveChild(Entity child);
		bool HasParent() const;
		bool IsAncestorOf(Entity entity) const;
		bool IsDescendantOf(Entity entity) const { return entity.IsAncestorOf(*this); }

		TransformComponent& Transform() { return GetComponent<TransformComponent>(); }
		const glm::mat4 Transform() const { return GetComponent<TransformComponent>().GetTransform(); }

		bool operator==(const Entity& other) const
		{
			return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
		}

		bool operator!=(const Entity& other) const
		{
			return !(*this == other);
		}
	private:
		entt::entity m_EntityHandle{ entt::null };
		Scene* m_Scene = nullptr;

		inline static std::string NoName = "Unnamed";

		friend class Prefab;
		friend class Scene;
		friend class SceneSerializer;
	};

}
