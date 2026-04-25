#include "lpch.h"
#include "Lux/Asset/AssetSystem/RuntimeAssetSystem.h"

namespace Lux
{
	RuntimeAssetSystem::RuntimeAssetSystem()
		: m_Thread("Asset Thread")
	{
		m_Thread.Dispatch([this]() { AssetThreadFunc(); });
	}

	RuntimeAssetSystem::~RuntimeAssetSystem()
	{
		StopAndWait();
	}

	void RuntimeAssetSystem::Stop()
	{
		m_Running = false;
		m_AssetLoadingQueueCV.notify_one();
	}

	void RuntimeAssetSystem::StopAndWait()
	{
		Stop();
		m_Thread.Join();
	}

	void RuntimeAssetSystem::AssetThreadFunc()
	{
		while (m_Running)
		{
			bool queueEmptyOrStop = false;
			while (!queueEmptyOrStop)
			{
				RuntimeAssetLoadRequest request;
				{
					std::scoped_lock<std::mutex> lock(m_AssetLoadingQueueMutex);
					if (m_AssetLoadingQueue.empty() || !m_Running)
					{
						queueEmptyOrStop = true;
					}
					else
					{
						request = m_AssetLoadingQueue.front();
						m_AssetLoadingQueue.pop();
					}
				}

				if (request.Handle != 0 && request.SceneHandle != 0)
					TryLoadData(request);
			}

			std::unique_lock<std::mutex> lock(m_AssetLoadingQueueMutex);
			if (m_AssetLoadingQueue.empty() && m_Running)
			{
				m_AssetLoadingQueueCV.wait(lock, [this]
				{
					return !m_Running || !m_AssetLoadingQueue.empty();
				});
			}
		}
	}

	Ref<Asset> RuntimeAssetSystem::TryLoadData(const RuntimeAssetLoadRequest& request)
	{
		if (!m_AssetPack)
			return nullptr;

		Ref<Asset> asset = m_AssetPack->LoadAsset(request.SceneHandle, request.Handle);
		if (asset)
		{
			std::scoped_lock<std::mutex> lock(m_LoadedAssetsMutex);
			m_LoadedAssets.emplace_back(asset);
		}
		else
		{
			LUX_CORE_ERROR("RuntimeAssetSystem: failed to load asset {}", (uint64_t)request.Handle);
		}

		return asset;
	}

	void RuntimeAssetSystem::QueueAssetLoad(const RuntimeAssetLoadRequest& request)
	{
		{
			std::scoped_lock<std::mutex> lock(m_AssetLoadingQueueMutex);
			m_AssetLoadingQueue.push(request);
		}
		m_AssetLoadingQueueCV.notify_one();
	}

	Ref<Asset> RuntimeAssetSystem::GetAsset(AssetHandle sceneHandle, AssetHandle assetHandle)
	{
		{
			std::scoped_lock<std::mutex> lock(m_AMLoadedAssetsMutex);
			if (auto it = m_AMLoadedAssets.find(assetHandle); it != m_AMLoadedAssets.end())
				return it->second;
		}

		return TryLoadData({ sceneHandle, assetHandle });
	}

	bool RuntimeAssetSystem::RetrieveReadyAssets(std::vector<Ref<Asset>>& outAssetList)
	{
		std::scoped_lock<std::mutex> lock(m_LoadedAssetsMutex);
		if (m_LoadedAssets.empty())
			return false;

		outAssetList = m_LoadedAssets;
		m_LoadedAssets.clear();
		return true;
	}

	void RuntimeAssetSystem::UpdateLoadedAssetList(const AssetMap& loadedAssets)
	{
		std::scoped_lock<std::mutex> lock(m_AMLoadedAssetsMutex);
		m_AMLoadedAssets = loadedAssets;
	}
}
