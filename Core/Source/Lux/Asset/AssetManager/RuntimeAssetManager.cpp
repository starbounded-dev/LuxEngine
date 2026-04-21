#include "lpch.h"
#include "Lux/Asset/AssetManager/RuntimeAssetManager.h"

#include <chrono>
#include <thread>

namespace Lux
{
	RuntimeAssetManager::RuntimeAssetManager() = default;

	RuntimeAssetManager::~RuntimeAssetManager()
	{
		Shutdown();
	}

	void RuntimeAssetManager::Shutdown()
	{
		m_AssetSystem.Stop();
	}

	AssetType RuntimeAssetManager::GetAssetType(AssetHandle assetHandle)
	{
		if (auto memoryAsset = GetMemoryAsset(assetHandle))
			return memoryAsset->GetAssetType();

		auto it = m_AssetRegistry.find(assetHandle);
		return it != m_AssetRegistry.end() ? it->second.Type : AssetType::None;
	}

	Ref<Asset> RuntimeAssetManager::GetAsset(AssetHandle assetHandle)
	{
		SyncWithAssetThread();

		if (auto memoryAsset = GetMemoryAsset(assetHandle))
			return memoryAsset;

		if (!IsAssetHandleValid(assetHandle))
			return nullptr;

		auto loadedIt = m_LoadedAssets.find(assetHandle);
		if (loadedIt != m_LoadedAssets.end())
			return loadedIt->second;

		auto registryIt = m_AssetRegistry.find(assetHandle);
		if (registryIt == m_AssetRegistry.end())
			return nullptr;

		std::atomic_bool finished = false;
		LoadAssetAsync(assetHandle, [&finished](AssetHandle, Ref<Asset>)
		{
			finished = true;
		});

		while (!finished)
		{
			SyncLoadedAssets();
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(1ms);
		}

		return m_LoadedAssets.contains(assetHandle) ? m_LoadedAssets.at(assetHandle) : nullptr;
	}

	AsyncAssetResult<Asset> RuntimeAssetManager::GetAssetAsync(AssetHandle assetHandle)
	{
		SyncWithAssetThread();

		if (auto memoryAsset = GetMemoryAsset(assetHandle))
			return { memoryAsset, true };

		if (!IsAssetHandleValid(assetHandle))
			return {};

		if (IsAssetLoaded(assetHandle))
			return { m_LoadedAssets.at(assetHandle), true };

		if (m_AssetRegistry.contains(assetHandle) && m_AssetRegistry[assetHandle].Status != AssetStatus::Loading)
			LoadAssetAsync(assetHandle);

		return {};
	}

	void RuntimeAssetManager::AddMemoryOnlyAsset(Ref<Asset> asset)
	{
		if (!asset)
			return;

		if (!asset->Handle)
			asset->Handle = AssetHandle();

		m_MemoryAssets[asset->Handle] = asset;
	}

	bool RuntimeAssetManager::ReloadData(AssetHandle assetHandle)
	{
		if (!IsAssetHandleValid(assetHandle))
			return false;

		m_LoadedAssets.erase(assetHandle);
		if (m_AssetRegistry.contains(assetHandle))
		{
			m_AssetRegistry[assetHandle].IsDataLoaded = false;
			m_AssetRegistry[assetHandle].Status = AssetStatus::None;
		}
		return GetAsset(assetHandle) != nullptr;
	}

	void RuntimeAssetManager::ReloadDataAsync(AssetHandle assetHandle)
	{
		if (!IsAssetHandleValid(assetHandle))
			return;

		m_LoadedAssets.erase(assetHandle);
		if (m_AssetRegistry.contains(assetHandle))
		{
			m_AssetRegistry[assetHandle].IsDataLoaded = false;
			m_AssetRegistry[assetHandle].Status = AssetStatus::Loading;
		}
		LoadAssetAsync(assetHandle);
	}

	bool RuntimeAssetManager::EnsureCurrent(AssetHandle assetHandle)
	{
		return IsAssetValid(assetHandle);
	}

	bool RuntimeAssetManager::EnsureAllLoadedCurrent()
	{
		bool allCurrent = true;
		for (const auto& [handle, metadata] : m_AssetRegistry)
			allCurrent &= EnsureCurrent(handle);
		return allCurrent;
	}

	bool RuntimeAssetManager::IsAssetHandleValid(AssetHandle assetHandle)
	{
		return IsMemoryAsset(assetHandle) || m_AssetRegistry.find(assetHandle) != m_AssetRegistry.end();
	}

	Ref<Asset> RuntimeAssetManager::GetMemoryAsset(AssetHandle handle)
	{
		auto it = m_MemoryAssets.find(handle);
		return it != m_MemoryAssets.end() ? it->second : nullptr;
	}

	bool RuntimeAssetManager::IsAssetLoaded(AssetHandle handle)
	{
		return IsMemoryAsset(handle) || m_LoadedAssets.find(handle) != m_LoadedAssets.end();
	}

	bool RuntimeAssetManager::IsAssetValid(AssetHandle handle)
	{
		if (IsMemoryAsset(handle))
			return true;

		auto it = m_AssetRegistry.find(handle);
		if (it == m_AssetRegistry.end())
			return false;

		if (it->second.Status == AssetStatus::Invalid || it->second.Status == AssetStatus::Missing)
			return false;

		if (it->second.IsDataLoaded)
			return m_LoadedAssets.find(handle) != m_LoadedAssets.end();

		return GetAsset(handle) != nullptr;
	}

	bool RuntimeAssetManager::IsAssetMissing(AssetHandle)
	{
		return false;
	}

	bool RuntimeAssetManager::IsMemoryAsset(AssetHandle handle)
	{
		return m_MemoryAssets.find(handle) != m_MemoryAssets.end();
	}

	bool RuntimeAssetManager::IsPhysicalAsset(AssetHandle handle)
	{
		return !IsMemoryAsset(handle) && m_AssetRegistry.find(handle) != m_AssetRegistry.end();
	}

	void RuntimeAssetManager::RemoveAsset(AssetHandle handle)
	{
		m_MemoryAssets.erase(handle);
		m_LoadedAssets.erase(handle);
		m_AssetRegistry.erase(handle);
		DeregisterDependencies(handle);
	}

	void RuntimeAssetManager::RegisterDependency(AssetHandle dependency, AssetHandle handle)
	{
		std::scoped_lock lock(m_AssetDependenciesMutex);
		m_AssetDependencies[handle].insert(dependency);
		m_AssetDependents[dependency].insert(handle);
	}

	void RuntimeAssetManager::DeregisterDependency(AssetHandle dependency, AssetHandle handle)
	{
		std::scoped_lock lock(m_AssetDependenciesMutex);
		if (m_AssetDependencies.contains(handle))
		{
			m_AssetDependencies[handle].erase(dependency);
			if (m_AssetDependencies[handle].empty())
				m_AssetDependencies.erase(handle);
		}

		if (m_AssetDependents.contains(dependency))
		{
			m_AssetDependents[dependency].erase(handle);
			if (m_AssetDependents[dependency].empty())
				m_AssetDependents.erase(dependency);
		}
	}

	void RuntimeAssetManager::DeregisterDependencies(AssetHandle handle)
	{
		std::scoped_lock lock(m_AssetDependenciesMutex);
		auto it = m_AssetDependencies.find(handle);
		if (it == m_AssetDependencies.end())
			return;

		for (AssetHandle dependency : it->second)
		{
			if (m_AssetDependents.contains(dependency))
			{
				m_AssetDependents[dependency].erase(handle);
				if (m_AssetDependents[dependency].empty())
					m_AssetDependents.erase(dependency);
			}
		}

		m_AssetDependencies.erase(it);
	}

	std::unordered_set<AssetHandle> RuntimeAssetManager::GetDependencies(AssetHandle handle)
	{
		std::shared_lock lock(m_AssetDependenciesMutex);
		auto it = m_AssetDependencies.find(handle);
		return it != m_AssetDependencies.end() ? it->second : std::unordered_set<AssetHandle>{};
	}

	void RuntimeAssetManager::SyncWithAssetThread()
	{
		SyncLoadedAssets();
	}

	std::unordered_set<AssetHandle> RuntimeAssetManager::GetAllAssetsWithType(AssetType type)
	{
		std::unordered_set<AssetHandle> result;

		for (const auto& [handle, asset] : m_MemoryAssets)
		{
			if (asset && asset->GetAssetType() == type)
				result.insert(handle);
		}

		for (const auto& [handle, metadata] : m_AssetRegistry)
		{
			if (metadata.Type == type)
				result.insert(handle);
		}

		return result;
	}

	void RuntimeAssetManager::RegisterAsset(AssetHandle handle, AssetType type)
	{
		AssetMetadata metadata;
		metadata.Handle = handle;
		metadata.Type = type;
		m_AssetRegistry[handle] = metadata;
	}

	void RuntimeAssetManager::LoadAssetAsync(AssetHandle handle, RuntimeAssetCallback callback)
	{
		if (!IsAssetHandleValid(handle))
			return;

		RuntimeAssetLoadRequest request;
		request.Handle = handle;
		request.Type = GetAssetType(handle);
		request.Callback = [this, callback = std::move(callback)](AssetHandle loadedHandle, Ref<Asset> asset)
		{
			if (m_AssetRegistry.contains(loadedHandle))
			{
				auto& metadata = m_AssetRegistry[loadedHandle];
				metadata.IsDataLoaded = asset != nullptr;
				metadata.Status = asset ? AssetStatus::Ready : AssetStatus::Invalid;
			}

			if (callback)
				callback(loadedHandle, asset);
		};
		if (m_AssetRegistry.contains(handle))
		{
			auto& metadata = m_AssetRegistry[handle];
			metadata.IsDataLoaded = false;
			metadata.Status = AssetStatus::Loading;
		}
		m_AssetSystem.QueueAssetLoad(std::move(request));
	}

	void RuntimeAssetManager::SyncLoadedAssets()
	{
		m_AssetSystem.SyncLoadedAssets(m_LoadedAssets);

		for (auto& [handle, metadata] : m_AssetRegistry)
		{
			metadata.IsDataLoaded = m_LoadedAssets.find(handle) != m_LoadedAssets.end();
			metadata.Status = metadata.IsDataLoaded ? AssetStatus::Ready : metadata.Status;
		}
	}

	void RuntimeAssetManager::SetRuntimeLoader(RuntimeAssetSystem::RuntimeLoader loader)
	{
		m_AssetSystem.SetLoader(std::move(loader));
	}
}
