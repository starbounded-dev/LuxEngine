#include "lpch.h"
#include "RuntimeAssetSystem.h"

namespace Lux
{
	RuntimeAssetSystem::RuntimeAssetSystem()
		: m_Thread("RuntimeAssetSystem")
	{
		m_Running = true;
		m_Thread.Dispatch(&RuntimeAssetSystem::WorkerThread, this);
	}

	RuntimeAssetSystem::~RuntimeAssetSystem()
	{
		Stop();
	}

	void RuntimeAssetSystem::QueueAssetLoad(RuntimeAssetLoadRequest request)
	{
		{
			std::scoped_lock lock(m_LoadQueueMutex);
			m_LoadQueue.push(std::move(request));
		}
		m_LoadQueueCV.notify_one();
	}

	void RuntimeAssetSystem::SyncLoadedAssets(AssetMap& loadedAssets)
	{
		std::scoped_lock lock(m_FinishedQueueMutex);
		while (!m_FinishedQueue.empty())
		{
			auto& entry = m_FinishedQueue.front();
			if (entry.Asset)
			{
				loadedAssets[entry.Handle] = entry.Asset;
				if (entry.Callback)
					entry.Callback(entry.Handle, entry.Asset);
			}
			m_FinishedQueue.pop();
		}
	}

	void RuntimeAssetSystem::Stop()
	{
		if (!m_Running.exchange(false))
			return; // already stopped

		m_LoadQueueCV.notify_all();
		m_Thread.Join();
	}

	void RuntimeAssetSystem::WorkerThread()
	{
		while (m_Running)
		{
			RuntimeAssetLoadRequest request;

			{
				std::unique_lock lock(m_LoadQueueMutex);
				m_LoadQueueCV.wait(lock, [this]
				{
					return !m_LoadQueue.empty() || !m_Running;
				});

				if (!m_Running && m_LoadQueue.empty())
					break;

				request = std::move(m_LoadQueue.front());
				m_LoadQueue.pop();
			}

			Ref<Asset> asset;
			if (m_Loader)
				asset = m_Loader(request.Handle, request.Type);

			if (asset)
				asset->Handle = request.Handle;

			{
				std::scoped_lock lock(m_FinishedQueueMutex);
				m_FinishedQueue.push({ request.Handle, asset, std::move(request.Callback) });
			}
		}
	}
}
