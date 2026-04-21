#pragma once

#include "AssetManagerBase.h"

#include "Lux/Asset/AssetExtensions.h"
#include "Lux/Asset/AssetMetadata.h"
#include "Lux/Asset/AssetRegistry.h"
#include "Lux/Asset/AssetSystem/EditorAssetSystem.h"

#include <shared_mutex>

namespace Lux
{
	class EditorAssetManager : public AssetManagerBase
	{
	public:
		EditorAssetManager();
		virtual ~EditorAssetManager();

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

		const AssetRegistry& GetAssetRegistry() const { return m_AssetRegistry; }

		AssetMetadata GetMetadata(AssetHandle handle) const;
		void SetMetadata(AssetHandle handle, const AssetMetadata& metadata);

		AssetHandle ImportAsset(const std::filesystem::path& filepath);
		AssetHandle ImportScriptAsset(const std::filesystem::path& filepath, uint64_t uuid);

		AssetHandle GetAssetHandleFromFilePath(const std::filesystem::path& filepath) const;

		AssetType GetAssetTypeFromExtension(const std::string& extension) const;
		std::string GetDefaultExtensionForAssetType(AssetType type) const;
		AssetType GetAssetTypeFromPath(const std::filesystem::path& path) const;

		std::filesystem::path GetFileSystemPath(AssetHandle handle) const;
		std::filesystem::path GetFileSystemPath(const AssetMetadata& metadata) const;
		std::string GetFileSystemPathString(const AssetMetadata& metadata) const;
		std::filesystem::path GetRelativePath(const std::filesystem::path& filepath) const;
		std::filesystem::path GetFilePath(AssetHandle handle) const;

		bool FileExists(AssetMetadata& metadata) const;

		void LoadAssetAsync(AssetHandle handle);
		void SyncLoadedAssets();
		void SerializeAssetRegistry();
		bool DeserializeAssetRegistry();

	private:
		Ref<Asset> GetAssetIncludingInvalid(AssetHandle assetHandle);
		bool LoadAssetData(const AssetMetadata& metadata, Ref<Asset>& asset);
		void LoadAssetRegistry();
		void ProcessDirectory(const std::filesystem::path& directoryPath);
		void ReloadAssets();
		void WriteRegistryToFile();
		void RegisterAssetDependencies(const Ref<Asset>& asset);
		void UpdateDependents(AssetHandle handle);

	private:
		AssetMap m_LoadedAssets;
		std::unordered_map<AssetHandle, Ref<Asset>> m_MemoryAssets;
		mutable std::shared_mutex m_MemoryAssetsMutex;

		std::unordered_map<AssetHandle, std::unordered_set<AssetHandle>> m_AssetDependents;
		std::unordered_map<AssetHandle, std::unordered_set<AssetHandle>> m_AssetDependencies;
		mutable std::shared_mutex m_AssetDependenciesMutex;

		Ref<EditorAssetSystem> m_AssetThread;

		AssetRegistry m_AssetRegistry;
		mutable std::shared_mutex m_AssetRegistryMutex;
	};
}
