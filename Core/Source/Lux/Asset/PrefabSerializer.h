#pragma once

#include "Asset.h"
#include "AssetMetadata.h"
#include "AssetSerializer.h"

namespace Lux
{
	// A prefab is a self-contained sub-hierarchy stored as a scene (.lprefab). Save/load delegate to
	// SceneSerializer on the prefab's own Scene; load then re-identifies the single root entity.
	class PrefabSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		virtual bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const override;
	};
}
