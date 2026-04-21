#pragma once

#include "AssetMetadata.h"
#include "AssetSerializer.h"

namespace Lux
{
	// Routes asset load requests to the appropriate AssetSerializer (or legacy
	// importer function) based on the asset type stored in the metadata.
	class AssetImporter
	{
	public:
		static void Init();
		static void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset);
		static void Serialize(const Ref<Asset>& asset);
		static bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset);
		static void RegisterDependencies(const AssetMetadata& metadata);
		static Ref<Asset> ImportAsset(AssetHandle handle, const AssetMetadata& metadata);
	};


}
