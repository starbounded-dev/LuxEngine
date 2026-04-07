#include "lpch.h"
#include "EditorAssetSystem.h"

#include "AssetImporter.h"

namespace Lux
{
	EditorAssetSystem::EditorAssetSystem()
		: m_Thread("EditorAssetSystem")
	{
		m_Running = true;
		m_Thread.Dispatch(&EditorAssetSystem::WorkerThread, this);
	}

	EditorAssetSystem::~EditorAssetSystem()
	{
		Stop();
	}

	void EditorAssetSystem::QueueAssetLoad(const AssetMetadata& metadata)
	{
		{
			std::scoped_lock lock(m_LoadQueueMutex);
			m_LoadQueue.push(metadata);
		}
		m_LoadQueueCV.notify_one();
	}

	void EditorAssetSystem::SyncLoadedAssets(AssetMap& loadedAssets)
	{
		std::scoped_lock lock(m_FinishedQueueMutex);
		while (!m_FinishedQueue.empty())
		{
			auto& entry = m_FinishedQueue.front();
			if (entry.Asset)
				loadedAssets[entry.Handle] = entry.Asset;
			m_FinishedQueue.pop();
		}
	}

	void EditorAssetSystem::Stop()
	{
		if (!m_Running.exchange(false))
			return; // already stopped

		m_LoadQueueCV.notify_all();
		m_Thread.Join();
	}

	void EditorAssetSystem::WorkerThread()
	{
		while (m_Running)
		{
			AssetMetadata metadata;

			{
				std::unique_lock lock(m_LoadQueueMutex);
				m_LoadQueueCV.wait(lock, [this]
				{
					return !m_LoadQueue.empty() || !m_Running;
				});

				if (!m_Running && m_LoadQueue.empty())
					break;

				metadata = m_LoadQueue.front();
				m_LoadQueue.pop();
			}

			// Load on worker thread
			Ref<Asset> asset = AssetImporter::ImportAsset(metadata.Handle, metadata);
			if (asset)
				asset->Handle = metadata.Handle;

			{
				std::scoped_lock lock(m_FinishedQueueMutex);
				m_FinishedQueue.push({ metadata.Handle, asset });
			}
		}
	}
}
