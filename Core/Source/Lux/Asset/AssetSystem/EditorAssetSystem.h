#pragma once

#include "Lux/Asset/AssetMetadata.h"
#include "Lux/Core/Thread.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

namespace Lux
{
	struct EditorAssetLoadResponse
	{
		AssetMetadata Metadata;
		Ref<Asset> Asset;
	};

	class EditorAssetSystem : public RefCounted
	{
	public:
		EditorAssetSystem();
		~EditorAssetSystem();

		void QueueAssetLoad(const AssetMetadata& metadata);
		void SyncLoadedAssets(std::vector<EditorAssetLoadResponse>& loadedAssets);
		void Stop();

	private:
		void WorkerThread();

	private:
		Thread m_Thread;
		std::atomic_bool m_Running{ false };

		std::queue<AssetMetadata> m_LoadQueue;
		std::mutex m_LoadQueueMutex;
		std::condition_variable m_LoadQueueCV;

		std::queue<EditorAssetLoadResponse> m_FinishedQueue;
		std::mutex m_FinishedQueueMutex;
	};
}
