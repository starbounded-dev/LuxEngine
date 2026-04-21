#include "lpch.h"
#include "Lux/Asset/AssetSystem/EditorAssetSystem.h"

#include "Lux/Asset/AssetImporter.h"

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

	void EditorAssetSystem::SyncLoadedAssets(std::vector<EditorAssetLoadResponse>& loadedAssets)
	{
		std::scoped_lock lock(m_FinishedQueueMutex);
		while (!m_FinishedQueue.empty())
		{
			loadedAssets.emplace_back(std::move(m_FinishedQueue.front()));
			m_FinishedQueue.pop();
		}
	}

	void EditorAssetSystem::Stop()
	{
		if (!m_Running.exchange(false))
			return;

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

			Ref<Asset> asset;
			if (AssetImporter::TryLoadData(metadata, asset) && asset)
				asset->Handle = metadata.Handle;

			{
				std::scoped_lock lock(m_FinishedQueueMutex);
				m_FinishedQueue.push({ metadata, asset });
			}
		}
	}
}
