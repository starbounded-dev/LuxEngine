#pragma once

#include "Lux/Asset/AssetManager/AssetManagerBase.h"
#include "Lux/Asset/AssetMetadata.h"
#include "Lux/Core/Ref.h"
#include "Lux/Core/Thread.h"
#include "Lux/Serialization/AssetPack.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

namespace Lux
{
	class RuntimeAssetSystem
	{
	public:
		RuntimeAssetSystem();
		~RuntimeAssetSystem();

		void QueueAssetLoad(const RuntimeAssetLoadRequest& request);
		Ref<Asset> GetAsset(AssetHandle sceneHandle, AssetHandle assetHandle);
		bool RetrieveReadyAssets(std::vector<Ref<Asset>>& outAssetList);
		void UpdateLoadedAssetList(const AssetMap& loadedAssets);

		void SetAssetPack(Ref<AssetPack> assetPack) { m_AssetPack = assetPack; }

		void Stop();
		void StopAndWait();

	private:
		void AssetThreadFunc();
		Ref<Asset> TryLoadData(const RuntimeAssetLoadRequest& request);

	private:
		Thread m_Thread;
		std::atomic<bool> m_Running = true;

		Ref<AssetPack> m_AssetPack;

		std::queue<RuntimeAssetLoadRequest> m_AssetLoadingQueue;
		std::mutex m_AssetLoadingQueueMutex;
		std::condition_variable m_AssetLoadingQueueCV;

		std::vector<Ref<Asset>> m_LoadedAssets;
		std::mutex m_LoadedAssetsMutex;

		AssetMap m_AMLoadedAssets;
		std::mutex m_AMLoadedAssetsMutex;
	};
}
