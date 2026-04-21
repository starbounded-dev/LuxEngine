#pragma once

#include "AssetManager/AssetManagerBase.h"

#include "Lux/Project/Project.h"

namespace Lux
{
	class AssetManager
	{
	public:
		template<typename T>
		static Ref<T> GetAsset(AssetHandle handle)
		{
			//LUX_PROFILE_FUNCTION_COLOR("AssetManager::GetAsset", 0x8CCBFF);

			Ref<Asset> asset = Project::GetAssetManager()->GetAsset(handle);
			return asset.As<T>();
		}

		static bool IsAssetHandleValid(AssetHandle handle)
		{
			return Project::GetAssetManager()->IsAssetHandleValid(handle);
		}

		static bool IsAssetValid(AssetHandle handle)
		{
			return Project::GetAssetManager()->IsAssetValid(handle);
		}

		static bool IsAssetMissing(AssetHandle handle)
		{
			return Project::GetAssetManager()->IsAssetMissing(handle);
		}

		static bool IsMemoryAsset(AssetHandle handle)
		{
			return Project::GetAssetManager()->IsMemoryAsset(handle);
		}

		static bool IsPhysicalAsset(AssetHandle handle)
		{
			return Project::GetAssetManager()->IsPhysicalAsset(handle);
		}

		static bool IsAssetLoaded(AssetHandle handle)
		{
			return Project::GetAssetManager()->IsAssetLoaded(handle);
		}

		static AssetType GetAssetType(AssetHandle handle)
		{
			return Project::GetAssetManager()->GetAssetType(handle);
		}

		static AsyncAssetResult<Asset> GetAssetAsync(AssetHandle handle)
		{
			return Project::GetAssetManager()->GetAssetAsync(handle);
		}

		static bool ReloadData(AssetHandle handle)
		{
			return Project::GetAssetManager()->ReloadData(handle);
		}

		static void ReloadDataAsync(AssetHandle handle)
		{
			Project::GetAssetManager()->ReloadDataAsync(handle);
		}

		static bool EnsureCurrent(AssetHandle handle)
		{
			return Project::GetAssetManager()->EnsureCurrent(handle);
		}

		static bool EnsureAllLoadedCurrent()
		{
			return Project::GetAssetManager()->EnsureAllLoadedCurrent();
		}

		static void SyncWithAssetThread()
		{
			Project::GetAssetManager()->SyncWithAssetThread();
		}

		template<typename TAsset>
		static AssetHandle AddMemoryOnlyAsset(Ref<TAsset> asset)
		{
			static_assert(std::is_base_of_v<Asset, TAsset>, "AddMemoryOnlyAsset requires an Asset-derived type");
			if (!asset->Handle)
				asset->Handle = AssetHandle();

			Project::GetAssetManager()->AddMemoryOnlyAsset(asset);
			return asset->Handle;
		}

		static Ref<Asset> GetMemoryAsset(AssetHandle handle)
		{
			return Project::GetAssetManager()->GetMemoryAsset(handle);
		}

		static void RegisterDependency(AssetHandle dependency, AssetHandle handle)
		{
			Project::GetAssetManager()->RegisterDependency(dependency, handle);
		}

		static void DeregisterDependency(AssetHandle dependency, AssetHandle handle)
		{
			Project::GetAssetManager()->DeregisterDependency(dependency, handle);
		}

		static void DeregisterDependencies(AssetHandle handle)
		{
			Project::GetAssetManager()->DeregisterDependencies(handle);
		}

		static void RemoveAsset(AssetHandle handle)
		{
			Project::GetAssetManager()->RemoveAsset(handle);
		}

		template<typename T>
		static std::unordered_set<AssetHandle> GetAllAssetsWithType()
		{
			return Project::GetAssetManager()->GetAllAssetsWithType(T::GetStaticType());
		}

		static const AssetMap& GetLoadedAssets()
		{
			return Project::GetAssetManager()->GetLoadedAssets();
		}
	};
}
