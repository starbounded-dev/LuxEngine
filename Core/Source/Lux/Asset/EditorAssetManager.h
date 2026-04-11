#pragma once

#include "AssetManagerBase.h"
#include "AssetMetadata.h"
#include "EditorAssetSystem.h"

#include <map>

namespace Lux {

	using AssetRegistry = std::map<AssetHandle, AssetMetadata>;

	class EditorAssetManager : public AssetManagerBase
	{
	public:
		EditorAssetManager();
		virtual ~EditorAssetManager();

		virtual Ref<Asset> GetAsset(AssetHandle handle) override;

		virtual bool IsAssetHandleValid(AssetHandle handle) const override;
		virtual bool IsAssetLoaded(AssetHandle handle) const override;
		virtual AssetType GetAssetType(AssetHandle handle) const override;

		void ImportAsset(const std::filesystem::path& filepath);
		void ImportScriptAsset(const std::filesystem::path& filepath, uint64_t uuid);

		// Queue a background async load.  Call SyncLoadedAssets() each frame to
		// pick up finished results.
		void LoadAssetAsync(AssetHandle handle);

		// Must be called every frame on the main thread to drain the finished
		// load queue from EditorAssetSystem.
		void SyncLoadedAssets();

		const AssetMetadata& GetMetadata(AssetHandle handle) const;
		const std::filesystem::path& GetFilePath(AssetHandle handle) const;

		const AssetRegistry& GetAssetRegistry() const { return m_AssetRegistry; }

		void SerializeAssetRegistry();
		bool DeserializeAssetRegistry();

	private:
		AssetRegistry      m_AssetRegistry;
		AssetMap           m_LoadedAssets;
		EditorAssetSystem  m_AssetSystem;

		// TODO: memory-only assets
	};
}
