#pragma once

#include "AssetManagerBase.h"
#include "AssetMetadata.h"

#include "Lux/Core/Thread.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

namespace Lux
{
	using RuntimeAssetCallback = std::function<void(AssetHandle, Ref<Asset>)>;

	// Request submitted to RuntimeAssetSystem for an async load.
	struct RuntimeAssetLoadRequest
	{
		AssetHandle Handle = 0;
		AssetType   Type   = AssetType::None;

		// Callback invoked on the main thread (via SyncLoadedAssets) once the
		// asset has been loaded.  May be nullptr.
		RuntimeAssetCallback Callback;
	};

	// Background loading thread for runtime (shipped game) use.
	// Unlike EditorAssetSystem it does NOT use Assimp / YAML.  It expects the
	// caller to provide a factory callback that can load the asset from a
	// binary asset-pack stream.
	class RuntimeAssetSystem
	{
	public:
		RuntimeAssetSystem();
		~RuntimeAssetSystem();

		// Enqueue a load request.  Thread-safe.
		void QueueAssetLoad(RuntimeAssetLoadRequest request);

		// Must be called from the main thread each frame.
		// Delivers finished assets to loadedAssets and fires per-asset callbacks.
		void SyncLoadedAssets(AssetMap& loadedAssets);

		// Signals the worker thread to stop and waits for it to finish.
		void Stop();

		// Supply the function used to load an asset at runtime.
		// Signature: Ref<Asset> loader(AssetHandle handle, AssetType type)
		using RuntimeLoader = std::function<Ref<Asset>(AssetHandle, AssetType)>;
		void SetLoader(RuntimeLoader loader) { m_Loader = std::move(loader); }

	private:
		void WorkerThread();

	private:
		Thread            m_Thread;
		std::atomic_bool  m_Running{ false };
		RuntimeLoader     m_Loader;

		std::queue<RuntimeAssetLoadRequest> m_LoadQueue;
		std::mutex                          m_LoadQueueMutex;
		std::condition_variable             m_LoadQueueCV;

		struct LoadedEntry
		{
			AssetHandle Handle = 0;
			Ref<Asset>  LoadedAsset;
			RuntimeAssetCallback CallbackFn;
		};
		std::queue<LoadedEntry> m_FinishedQueue;
		std::mutex              m_FinishedQueueMutex;
	};
}
