#pragma once

#include "Lux/Asset/AssetManager/AssetManagerBase.h"
#include "Lux/Asset/AssetMetadata.h"
#include "Lux/Core/Thread.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

namespace Lux
{
	using RuntimeAssetCallback = std::function<void(AssetHandle, Ref<Asset>)>;

	struct RuntimeAssetLoadRequest
	{
		AssetHandle Handle = 0;
		AssetType Type = AssetType::None;
		RuntimeAssetCallback Callback;
	};

	class RuntimeAssetSystem
	{
	public:
		RuntimeAssetSystem();
		~RuntimeAssetSystem();

		void QueueAssetLoad(RuntimeAssetLoadRequest request);
		void SyncLoadedAssets(AssetMap& loadedAssets);
		void Stop();

		using RuntimeLoader = std::function<Ref<Asset>(AssetHandle, AssetType)>;
		void SetLoader(RuntimeLoader loader) { m_Loader = std::move(loader); }

	private:
		void WorkerThread();

	private:
		Thread m_Thread;
		std::atomic_bool m_Running{ false };
		RuntimeLoader m_Loader;

		std::queue<RuntimeAssetLoadRequest> m_LoadQueue;
		std::mutex m_LoadQueueMutex;
		std::condition_variable m_LoadQueueCV;

		struct LoadedEntry
		{
			AssetHandle Handle = 0;
			Ref<Asset> LoadedAsset;
			RuntimeAssetCallback CallbackFn;
		};
		std::queue<LoadedEntry> m_FinishedQueue;
		std::mutex m_FinishedQueueMutex;
	};
}
