#pragma once

#include "AssetManagerBase.h"
#include "AssetMetadata.h"

#include "Lux/Core/Thread.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

namespace Lux
{
	// Processes asset load requests on a background thread so that the main
	// thread is not blocked.  Once a load completes the asset is placed in a
	// "pending sync" queue that EditorAssetManager drains each frame on the
	// main thread via SyncLoadedAssets().
	class EditorAssetSystem
	{
	public:
		EditorAssetSystem();
		~EditorAssetSystem();

		// Push a load request.  Thread-safe.
		void QueueAssetLoad(const AssetMetadata& metadata);

		// Must be called from the main thread each frame.
		// Drains the finished-load queue and inserts the assets into the
		// provided map.
		void SyncLoadedAssets(AssetMap& loadedAssets);

		// Signals the worker thread to stop and waits for it to finish.
		void Stop();

	private:
		void WorkerThread();

	private:
		Thread          m_Thread;
		std::atomic_bool m_Running{ false };

		// Pending load requests (main → worker)
		std::queue<AssetMetadata> m_LoadQueue;
		std::mutex                m_LoadQueueMutex;
		std::condition_variable   m_LoadQueueCV;

		// Finished loads (worker → main)
		struct LoadedEntry { AssetHandle Handle; Ref<Asset> Asset; };
		std::queue<LoadedEntry> m_FinishedQueue;
		std::mutex              m_FinishedQueueMutex;
	};
}
