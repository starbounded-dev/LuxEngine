#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Core/UUID.h"

namespace Lux {

	class Scene;
	class Entity;

	class Prefab : public Asset
	{
	public:
		Prefab();

		static AssetType GetStaticType() { return AssetType::Prefab; }
		virtual AssetType GetAssetType() const override { return GetStaticType(); }

		Entity CreatePrefabFromEntity(Entity entity);

		const Ref<Scene>& GetScene() const { return m_Scene; }
		UUID GetRootEntityID() const { return m_RootEntityID; }

	private:
		Ref<Scene> m_Scene;
		UUID m_RootEntityID = 0;
	};

}
