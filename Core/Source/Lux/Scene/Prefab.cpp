#include "lpch.h"
#include "Lux/Scene/Prefab.h"

#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"

namespace Lux {

	template<typename... Component>
	static void CopyComponentIfExists(Entity dst, Entity src)
	{
		([&]()
		{
			if (src.HasComponent<Component>())
				dst.AddOrReplaceComponent<Component>(src.GetComponent<Component>());
		}(), ...);
	}

	template<typename... Component>
	static void CopyComponentIfExists(ComponentGroup<Component...>, Entity dst, Entity src)
	{
		CopyComponentIfExists<Component...>(dst, src);
	}

	using PrefabCloneComponents =
		ComponentGroup<TransformComponent, SpriteRendererComponent, CircleRendererComponent, CameraComponent, ScriptComponent,
		NativeScriptComponent, RigidBody2DComponent, BoxCollider2DComponent, CircleCollider2DComponent, TextComponent,
		MeshComponent, MeshTagComponent, StaticMeshComponent, SubmeshComponent,
		DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent>;

	Prefab::Prefab()
	{
		m_Scene = Ref<Scene>::Create();
	}

	void Prefab::Create(Entity entity, bool serialize)
	{
		(void)serialize;
		m_Entity = CreatePrefabFromEntity(entity);
	}

	UUID Prefab::GetRootEntityID() const
	{
		return m_Entity ? m_Entity.GetUUID() : UUID(0);
	}

	std::unordered_set<AssetHandle> Prefab::GetAssetList(bool recursive)
	{
		(void)recursive;
		return m_Scene ? m_Scene->GetAssetList() : std::unordered_set<AssetHandle>{};
	}

	Entity Prefab::CreatePrefabFromEntity(Entity entity)
	{
		LUX_CORE_ASSERT(entity, "Cannot create prefab from null entity!");

		m_Scene = Ref<Scene>::Create();
		m_Entity = {};

		std::function<Entity(Entity, Entity)> duplicateHierarchy;
		duplicateHierarchy = [&](Entity source, Entity parent) -> Entity
		{
			Entity destination = m_Scene->CreateEntity(source.GetName());
			CopyComponentIfExists(PrefabCloneComponents{}, destination, source);

			if (parent)
				destination.SetParent(parent);

			if (source.HasComponent<RelationshipComponent>())
			{
				for (const UUID childID : source.Children())
				{
					Entity child = source.GetScene()->GetEntityByUUID(childID);
					if (child)
						duplicateHierarchy(child, destination);
				}
			}

			return destination;
		};

		Entity root = duplicateHierarchy(entity, {});
		m_Entity = root;
		return root;
	}

}
