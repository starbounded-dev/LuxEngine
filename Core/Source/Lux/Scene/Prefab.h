#pragma once

#include "Entity.h"

#include "Lux/Asset/Asset.h"
#include "Lux/Core/UUID.h"

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

		void Create(Entity entity, bool serialize = true);

		const Ref<Scene>& GetScene() const { return m_Scene; }
		UUID GetRootEntityID() const;
		std::unordered_set<AssetHandle> GetAssetList(bool recursive = true);

	private:
		Entity CreatePrefabFromEntity(Entity entity);

	private:
		Ref<Scene> m_Scene;
		Entity m_Entity;

		friend class Scene;
		friend class SceneSerializer;
		friend class PrefabSerializer;
	};

}
