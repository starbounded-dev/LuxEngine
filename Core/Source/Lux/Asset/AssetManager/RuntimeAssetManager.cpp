#include "lpch.h"
#include "Lux/Asset/AssetManager/RuntimeAssetManager.h"
#include "Lux/Asset/AssetManager.h"

namespace Lux
{
	RuntimeAssetManager::RuntimeAssetManager() = default;

	RuntimeAssetManager::~RuntimeAssetManager()
	{
		Shutdown();
	}

	void RuntimeAssetManager::Shutdown()
	{
		m_AssetSystem.StopAndWait();
		m_PendingAssets.clear();
		m_LoadedAssets.clear();
		m_MemoryAssets.clear();
		m_AssetPack = nullptr;
		m_ActiveScene = 0;
	}

	AssetType RuntimeAssetManager::GetAssetType(AssetHandle assetHandle)
	{
		if (auto memoryAsset = GetMemoryAsset(assetHandle))
			return memoryAsset->GetAssetType();

		if (!m_AssetPack)
			return AssetType::None;

		return m_AssetPack->GetAssetType(m_ActiveScene, assetHandle);
	}

	Ref<Asset> RuntimeAssetManager::GetAsset(AssetHandle assetHandle)
	{
		SyncWithAssetThread();

		if (auto memoryAsset = GetMemoryAsset(assetHandle))
			return memoryAsset;

		if (!m_AssetPack || !IsAssetHandleValid(assetHandle))
			return nullptr;

		if (IsAssetLoaded(assetHandle))
			return m_LoadedAssets.at(assetHandle);

		Ref<Asset> asset = m_AssetSystem.GetAsset(m_ActiveScene, assetHandle);
		if (asset)
		{
			m_LoadedAssets[assetHandle] = asset;
			UpdateDependents(assetHandle);
		}

		return asset;
	}

	AsyncAssetResult<Asset> RuntimeAssetManager::GetAssetAsync(AssetHandle assetHandle)
	{
		SyncWithAssetThread();

		if (auto memoryAsset = GetMemoryAsset(assetHandle))
			return { memoryAsset, true };

		if (!m_AssetPack || !IsAssetHandleValid(assetHandle))
			return {};

		if (IsAssetLoaded(assetHandle))
			return { m_LoadedAssets.at(assetHandle), true };

		if (!m_PendingAssets.contains(assetHandle))
		{
			m_PendingAssets.insert(assetHandle);
			m_AssetSystem.QueueAssetLoad({ m_ActiveScene, assetHandle });
		}

		return { AssetManager::GetPlaceholderAsset(m_AssetPack->GetAssetType(m_ActiveScene, assetHandle)), false };
	}

	void RuntimeAssetManager::AddMemoryOnlyAsset(Ref<Asset> asset)
	{
		if (!asset)
			return;

		std::scoped_lock lock(m_MemoryAssetsMutex);
		m_MemoryAssets[asset->Handle] = asset;
	}

	bool RuntimeAssetManager::ReloadData(AssetHandle assetHandle)
	{
		if (!m_AssetPack || !IsAssetHandleValid(assetHandle))
			return false;

		Ref<Asset> asset = m_AssetPack->LoadAsset(m_ActiveScene, assetHandle);
		if (asset)
		{
			m_LoadedAssets[assetHandle] = asset;
			UpdateDependents(assetHandle);
		}

		return asset != nullptr;
	}

	void RuntimeAssetManager::ReloadDataAsync(AssetHandle assetHandle)
	{
		if (!m_AssetPack || !IsAssetHandleValid(assetHandle))
			return;

		if (!m_PendingAssets.contains(assetHandle))
		{
			m_PendingAssets.insert(assetHandle);
			m_AssetSystem.QueueAssetLoad({ m_ActiveScene, assetHandle });
		}
	}

	bool RuntimeAssetManager::EnsureCurrent(AssetHandle assetHandle)
	{
		return IsAssetValid(assetHandle);
	}

	bool RuntimeAssetManager::EnsureAllLoadedCurrent()
	{
		if (!m_AssetPack)
			return false;

		bool allCurrent = true;
		for (const auto& [handle, asset] : m_LoadedAssets)
			allCurrent &= EnsureCurrent(handle);
		return allCurrent;
	}

	bool RuntimeAssetManager::IsAssetHandleValid(AssetHandle assetHandle)
	{
		if (assetHandle == 0)
			return false;

		return GetMemoryAsset(assetHandle) || (m_AssetPack && m_AssetPack->IsAssetHandleValid(assetHandle));
	}

	Ref<Asset> RuntimeAssetManager::GetMemoryAsset(AssetHandle handle)
	{
		std::shared_lock lock(m_MemoryAssetsMutex);
		auto it = m_MemoryAssets.find(handle);
		return it != m_MemoryAssets.end() ? it->second : nullptr;
	}

	bool RuntimeAssetManager::IsAssetLoaded(AssetHandle handle)
	{
		return m_LoadedAssets.contains(handle);
	}

	bool RuntimeAssetManager::IsAssetValid(AssetHandle handle)
	{
		return GetAsset(handle) != nullptr;
	}

	bool RuntimeAssetManager::IsAssetMissing(AssetHandle handle)
	{
		return !IsAssetValid(handle);
	}

	bool RuntimeAssetManager::IsMemoryAsset(AssetHandle handle)
	{
		std::shared_lock lock(m_MemoryAssetsMutex);
		return m_MemoryAssets.contains(handle);
	}

	bool RuntimeAssetManager::IsPhysicalAsset(AssetHandle handle)
	{
		return !IsMemoryAsset(handle);
	}

	void RuntimeAssetManager::RemoveAsset(AssetHandle handle)
	{
		{
			std::scoped_lock lock(m_MemoryAssetsMutex);
			m_MemoryAssets.erase(handle);
		}

		m_LoadedAssets.erase(handle);
		m_PendingAssets.erase(handle);
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
		std::vector<Ref<Asset>> freshAssets;
		if (!m_AssetSystem.RetrieveReadyAssets(freshAssets))
			return;

		for (const auto& asset : freshAssets)
		{
			m_LoadedAssets[asset->Handle] = asset;
			m_PendingAssets.erase(asset->Handle);
		}

		m_AssetSystem.UpdateLoadedAssetList(m_LoadedAssets);

		for (const auto& asset : freshAssets)
			UpdateDependents(asset->Handle);
	}

	std::unordered_set<AssetHandle> RuntimeAssetManager::GetAllAssetsWithType(AssetType type)
	{
		std::unordered_set<AssetHandle> result;
		if (!m_AssetPack)
			return result;

		for (const auto& [sceneHandle, sceneInfo] : m_AssetPack->m_File.Index.Scenes)
		{
			if (sceneHandle == m_ActiveScene)
			{
				for (const auto& [assetHandle, assetInfo] : sceneInfo.Assets)
				{
					if ((AssetType)assetInfo.Type == type)
						result.insert(assetHandle);
				}
			}
		}

		return result;
	}

	Ref<Scene> RuntimeAssetManager::LoadScene(AssetHandle handle)
	{
		if (!m_AssetPack)
			return nullptr;

		Ref<Scene> scene = m_AssetPack->LoadScene(handle);
		if (scene)
			m_ActiveScene = handle;

		return scene;
	}

	void RuntimeAssetManager::SetAssetPack(Ref<AssetPack> assetPack)
	{
		m_AssetPack = assetPack;
		m_AssetSystem.SetAssetPack(assetPack);
	}

	void RuntimeAssetManager::UpdateDependents(AssetHandle handle)
	{
		std::unordered_set<AssetHandle> dependents;
		{
			std::shared_lock lock(m_AssetDependenciesMutex);
			auto it = m_AssetDependents.find(handle);
			if (it != m_AssetDependents.end())
				dependents = it->second;
		}

		for (AssetHandle dependentHandle : dependents)
		{
			if (!IsAssetLoaded(dependentHandle))
				continue;

			Ref<Asset> dependentAsset = IsMemoryAsset(dependentHandle) ? GetMemoryAsset(dependentHandle) : m_LoadedAssets.at(dependentHandle);
			if (dependentAsset)
				dependentAsset->OnDependencyUpdated(handle);
		}
	}
}
