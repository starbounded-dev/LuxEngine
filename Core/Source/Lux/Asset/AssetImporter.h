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
		static Ref<Asset> ImportAsset(AssetHandle handle, const AssetMetadata& metadata);
	};


}
