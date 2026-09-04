#pragma once

#include "Entity.h"

#include "Lux/Asset/Asset.h"
#include "Lux/Core/UUID.h"

#include <unordered_map>
#include <unordered_set>

namespace Lux {

	class Scene;

	class Prefab : public Asset
	{
	public:
		Prefab();
		~Prefab() = default;

		static AssetType GetStaticType() { return AssetType::Prefab; }
		virtual AssetType GetAssetType() const override { return GetStaticType(); }

		// outSourceToPrefab, if given, receives a map from each source entity's UUID to the UUID of
		// its clone inside the prefab — the caller uses it to link the source entities as instances.
		void Create(Entity entity, bool serialize = true, std::unordered_map<UUID, UUID>* outSourceToPrefab = nullptr);

		const Ref<Scene>& GetScene() const { return m_Scene; }
		UUID GetRootEntityID() const;
		std::unordered_set<AssetHandle> GetAssetList(bool recursive = true);

	private:
		Entity CreatePrefabFromEntity(Entity entity, std::unordered_map<UUID, UUID>* outSourceToPrefab);

	private:
		Ref<Scene> m_Scene;
		Entity m_Entity;

		friend class Scene;
		friend class SceneSerializer;
		friend class PrefabSerializer;
	};

}
