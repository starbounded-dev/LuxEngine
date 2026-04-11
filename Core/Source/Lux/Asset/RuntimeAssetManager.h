#pragma once

#include "AssetManagerBase.h"
#include "AssetMetadata.h"
#include "RuntimeAssetSystem.h"

namespace Lux {

	// Asset manager for the shipped runtime.  Assets are loaded via a
	// RuntimeAssetSystem worker thread.  The registry is built programmatically
	// (e.g. by scanning packed asset data) rather than from YAML on disk.
	class RuntimeAssetManager : public AssetManagerBase
	{
	public:
		RuntimeAssetManager();
		virtual ~RuntimeAssetManager();

		// ── AssetManagerBase interface ────────────────────────────────────────
		virtual Ref<Asset> GetAsset(AssetHandle handle) override;

		virtual bool IsAssetHandleValid(AssetHandle handle) const override;
		virtual bool IsAssetLoaded(AssetHandle handle) const override;
		virtual AssetType GetAssetType(AssetHandle handle) const override;

		// ── Runtime-specific API ──────────────────────────────────────────────

		// Register an asset handle so the manager knows its type.
		// Call this during application start-up for every asset in the pack.
		void RegisterAsset(AssetHandle handle, AssetType type);

		// Enqueue an async load.  The asset will be available after the next
		// SyncLoadedAssets() call on the main thread.
		void LoadAssetAsync(AssetHandle handle,
			std::function<void(AssetHandle, Ref<Asset>)> callback = nullptr);

		// Must be called every frame on the main thread.
		// Drains finished background loads into the loaded-asset map.
		void SyncLoadedAssets();

		// Provide the actual loader function used by the worker thread.
		void SetRuntimeLoader(RuntimeAssetSystem::RuntimeLoader loader);

	private:
		using AssetRegistry = std::map<AssetHandle, AssetType>;

		AssetRegistry m_AssetRegistry;
		AssetMap      m_LoadedAssets;

		RuntimeAssetSystem m_AssetSystem;
	};
}

