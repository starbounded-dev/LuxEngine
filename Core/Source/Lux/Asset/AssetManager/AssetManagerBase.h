// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Asset/AssetTypes.h"

#include <atomic>
#include <unordered_map>
#include <unordered_set>

namespace Lux
{
	using AssetMap = std::unordered_map<AssetHandle, Ref<Asset>>;

	class AssetManagerBase : public RefCounted
	{
	public:
		virtual ~AssetManagerBase() = default;

		virtual void Shutdown() = 0;

		virtual AssetType GetAssetType(AssetHandle assetHandle) = 0;
		virtual Ref<Asset> GetAsset(AssetHandle assetHandle) = 0;
		virtual AsyncAssetResult<Asset> GetAssetAsync(AssetHandle assetHandle) = 0;

		virtual void AddMemoryOnlyAsset(Ref<Asset> asset) = 0;
		virtual bool ReloadData(AssetHandle assetHandle) = 0;
		virtual void ReloadDataAsync(AssetHandle assetHandle) = 0;
		virtual bool EnsureCurrent(AssetHandle assetHandle) = 0;
		virtual bool EnsureAllLoadedCurrent() = 0;
		virtual bool IsAssetHandleValid(AssetHandle assetHandle) = 0;
		virtual Ref<Asset> GetMemoryAsset(AssetHandle handle) = 0;
		virtual bool IsAssetLoaded(AssetHandle handle) = 0;
		virtual bool IsAssetValid(AssetHandle handle) = 0;
		virtual bool IsAssetMissing(AssetHandle handle) = 0;
		virtual bool IsMemoryAsset(AssetHandle handle) = 0;
		virtual bool IsPhysicalAsset(AssetHandle handle) = 0;
		virtual void RemoveAsset(AssetHandle handle) = 0;

		virtual void RegisterDependency(AssetHandle dependency, AssetHandle handle) = 0;
		virtual void DeregisterDependency(AssetHandle dependency, AssetHandle handle) = 0;
		virtual void DeregisterDependencies(AssetHandle handle) = 0;
		virtual std::unordered_set<AssetHandle> GetDependencies(AssetHandle handle) = 0;

		virtual void SyncWithAssetThread() = 0;

		virtual std::unordered_set<AssetHandle> GetAllAssetsWithType(AssetType type) = 0;
		virtual const AssetMap& GetLoadedAssets() = 0;

		// Bumped whenever a loaded asset's data is replaced (a reload, or an asynchronous load landing).
		// Caches that hold asset objects by handle compare it to know when to look them up again.
		uint64_t GetDataGeneration() const { return m_DataGeneration.load(std::memory_order_relaxed); }

	protected:
		void MarkDataChanged() { m_DataGeneration.fetch_add(1, std::memory_order_relaxed); }

	private:
		std::atomic<uint64_t> m_DataGeneration{ 1 };
	};
}
