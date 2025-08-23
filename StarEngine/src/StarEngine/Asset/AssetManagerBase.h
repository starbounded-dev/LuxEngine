#pragma once

#include "Asset.h"
#include "StarEngine/Core/Base.h"

#include <map>

namespace StarEngine
{
	using AssetMap = std::map<AssetHandle, Ref<Asset>>;

	class AssetManagerBase : public RefCounted
	{
	public:
		virtual Ref<Asset> GetAsset(AssetHandle handle) = 0;

		virtual bool IsAssetHandleValid(AssetHandle handle) const = 0;
		virtual bool IsAssetLoaded(AssetHandle handle) const = 0;
		virtual AssetType GetAssetType(AssetHandle handle) const = 0;

		virtual Ref<Asset> GetMemoryAsset(AssetHandle handle) = 0;    // if exists in memory only (i.e. there is no backing file) return it otherwise return nullptr (this is more efficient than IsMemoryAsset() followed by GetAsset())
		virtual bool IsAssetMissing(AssetHandle handle) = 0;          // asset file is missing
	};
}
