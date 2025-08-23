#pragma once

#include "AssetManagerBase.h"

#include "StarEngine/Project/Project.h"

namespace StarEngine
{
	class AssetManager
	{
	public:
		template<typename T>
		static Ref<T> GetAsset(AssetHandle assetHandle)
		{
			//static std::mutex mutex;
			//std::scoped_lock<std::mutex> lock(mutex);

			Ref<Asset> asset = Project::GetAssetManager()->GetAsset(assetHandle);
			return asset.As<T>();
		}

		static bool IsAssetHandleValid(AssetHandle handle)
		{
			return Project::GetActive()->GetAssetManager()->IsAssetHandleValid(handle);
		}

		static bool IsAssetLoaded(AssetHandle handle)
		{
			return Project::GetActive()->GetAssetManager()->IsAssetLoaded(handle);
		}

		static AssetType GetAssetType(AssetHandle handle)
		{
			return Project::GetActive()->GetAssetManager()->GetAssetType(handle);
		}

		static bool IsAssetMissing(AssetHandle assetHandle)
		{
			return Project::GetAssetManager()->IsAssetMissing(assetHandle);
		}
	};
}
