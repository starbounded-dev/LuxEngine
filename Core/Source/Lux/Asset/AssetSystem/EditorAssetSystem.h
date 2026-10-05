// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

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
	class EditorAssetSystem : public RefCounted
	{
	public:
		EditorAssetSystem();
		~EditorAssetSystem();

		void QueueAssetLoad(const AssetMetadata& metadata);
		void SyncLoadedAssets(std::vector<EditorAssetLoadResponse>& loadedAssets);
		void Stop();

		// Loads queued but not yet handed back through SyncLoadedAssets: waiting, being loaded,
		// or finished and waiting for the main thread. Any thread.
		uint32_t GetPendingLoadCount() const { return m_PendingLoads.load(std::memory_order_relaxed); }

	private:
		void WorkerThread();

	private:
		Thread m_Thread;
		std::atomic_bool m_Running{ false };
		std::atomic<uint32_t> m_PendingLoads{ 0 };

		std::queue<AssetMetadata> m_LoadQueue;
		std::mutex m_LoadQueueMutex;
		std::condition_variable m_LoadQueueCV;

		std::queue<EditorAssetLoadResponse> m_FinishedQueue;
		std::mutex m_FinishedQueueMutex;
	};
}
