#pragma once

#include "AssetManagerBase.h"
#include "AssetMetadata.h"

#include <map>

#include <shared_mutex>

namespace StarEngine {

	using AssetRegistry = std::map<AssetHandle, AssetMetadata>;

	class EditorAssetManager : public AssetManagerBase
	{
	public:
		virtual Ref<Asset> GetAsset(AssetHandle handle) override;

		virtual bool IsAssetHandleValid(AssetHandle handle) const override;
		virtual bool IsAssetLoaded(AssetHandle handle) const override;
		virtual AssetType GetAssetType(AssetHandle handle) const override;

		virtual Ref<Asset> GetMemoryAsset(AssetHandle handle) override;
		virtual bool IsAssetMissing(AssetHandle handle) override;

		void ImportAsset(const std::filesystem::path& filepath);
		void ImportScriptAsset(const std::filesystem::path& filepath, uint64_t uuid);

		const AssetMetadata& GetMetadata(AssetHandle handle) const;
		const std::filesystem::path& GetFilePath(AssetHandle handle) const;

		const AssetRegistry& GetAssetRegistry() const { return m_AssetRegistry; }

		void SerializeAssetRegistry();
		bool DeserializeAssetRegistry();

		AssetHandle GetAssetHandleFromFilePath(const std::filesystem::path& filepath);
		std::filesystem::path GetRelativePath(const std::filesystem::path& filepath);

	private:
		// NOTE (0x): this collection is accessed and modified from both the main thread and
		//            the asset thread, and so requires synchronization
		std::unordered_map<AssetHandle, Ref<Asset>> m_MemoryAssets;
		std::shared_mutex m_MemoryAssetsMutex;

		AssetRegistry m_AssetRegistry;
		std::shared_mutex m_AssetRegistryMutex;

		AssetMap m_LoadedAssets;

		// TODO: memory-only assets
	};
}
