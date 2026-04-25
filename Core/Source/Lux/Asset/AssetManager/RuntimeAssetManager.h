#pragma once

#include "AssetManagerBase.h"

#include "Lux/Asset/AssetMetadata.h"
#include "Lux/Asset/AssetSystem/RuntimeAssetSystem.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Serialization/AssetPack.h"

#include <shared_mutex>

namespace Lux
{
	class RuntimeAssetManager : public AssetManagerBase
	{
	public:
		RuntimeAssetManager();
		virtual ~RuntimeAssetManager();

		virtual void Shutdown() override;

		virtual AssetType GetAssetType(AssetHandle assetHandle) override;
		virtual Ref<Asset> GetAsset(AssetHandle assetHandle) override;
		virtual AsyncAssetResult<Asset> GetAssetAsync(AssetHandle assetHandle) override;

		virtual void AddMemoryOnlyAsset(Ref<Asset> asset) override;

		virtual bool ReloadData(AssetHandle assetHandle) override;
		virtual void ReloadDataAsync(AssetHandle assetHandle) override;
		virtual bool EnsureCurrent(AssetHandle assetHandle) override;
		virtual bool EnsureAllLoadedCurrent() override;
		virtual bool IsAssetHandleValid(AssetHandle assetHandle) override;
		virtual Ref<Asset> GetMemoryAsset(AssetHandle handle) override;
		virtual bool IsAssetLoaded(AssetHandle handle) override;
		virtual bool IsAssetValid(AssetHandle handle) override;
		virtual bool IsAssetMissing(AssetHandle handle) override;
		virtual bool IsMemoryAsset(AssetHandle handle) override;
		virtual bool IsPhysicalAsset(AssetHandle handle) override;
		virtual void RemoveAsset(AssetHandle handle) override;

		virtual void RegisterDependency(AssetHandle dependency, AssetHandle handle) override;
		virtual void DeregisterDependency(AssetHandle dependency, AssetHandle handle) override;
		virtual void DeregisterDependencies(AssetHandle handle) override;
		virtual std::unordered_set<AssetHandle> GetDependencies(AssetHandle handle) override;

		virtual void SyncWithAssetThread() override;

		virtual std::unordered_set<AssetHandle> GetAllAssetsWithType(AssetType type) override;
		virtual const AssetMap& GetLoadedAssets() override { return m_LoadedAssets; }

		Ref<Scene> LoadScene(AssetHandle handle);
		void SetAssetPack(Ref<AssetPack> assetPack);

	private:
		void UpdateDependents(AssetHandle handle);

		Ref<AssetPack> m_AssetPack;
		AssetHandle m_ActiveScene = 0;

		AssetMap m_LoadedAssets;
		std::unordered_map<AssetHandle, Ref<Asset>> m_MemoryAssets;
		std::unordered_map<AssetHandle, std::unordered_set<AssetHandle>> m_AssetDependents;
		std::unordered_map<AssetHandle, std::unordered_set<AssetHandle>> m_AssetDependencies;
		std::unordered_set<AssetHandle> m_PendingAssets;

		mutable std::shared_mutex m_MemoryAssetsMutex;
		mutable std::shared_mutex m_AssetDependenciesMutex;

		RuntimeAssetSystem m_AssetSystem;
	};
}
