#include "lpch.h"
#include "AssetManager.h"
#include "RuntimeAssetManager.h"

namespace Lux {

	RuntimeAssetManager::RuntimeAssetManager() = default;

	RuntimeAssetManager::~RuntimeAssetManager()
	{
		m_AssetSystem.Stop();
	}

	Ref<Asset> RuntimeAssetManager::GetAsset(AssetHandle handle)
	{
		if (!IsAssetHandleValid(handle))
			return nullptr;

		if (IsAssetLoaded(handle))
			return m_LoadedAssets.at(handle);

		// Synchronous fallback: this should rarely be hit in a well-structured
		// runtime because assets are expected to be pre-loaded asynchronously.
		LUX_CORE_WARN("RuntimeAssetManager::GetAsset – synchronous load for handle {}", (uint64_t)handle);

		// Trigger an async load and wait for it (simple spin).
		std::atomic_bool done{ false };
		LoadAssetAsync(handle, [&done](AssetHandle, Ref<Asset>)
		{
			done = true;
		});

		while (!done)
		{
			SyncLoadedAssets();
			std::this_thread::yield();
		}

		return IsAssetLoaded(handle) ? m_LoadedAssets.at(handle) : nullptr;
	}

	bool RuntimeAssetManager::IsAssetHandleValid(AssetHandle handle) const
	{
		return m_AssetRegistry.find(handle) != m_AssetRegistry.end();
	}

	bool RuntimeAssetManager::IsAssetLoaded(AssetHandle handle) const
	{
		return m_LoadedAssets.find(handle) != m_LoadedAssets.end();
	}

	AssetType RuntimeAssetManager::GetAssetType(AssetHandle handle) const
	{
		auto it = m_AssetRegistry.find(handle);
		return it != m_AssetRegistry.end() ? it->second : AssetType::None;
	}

	void RuntimeAssetManager::RegisterAsset(AssetHandle handle, AssetType type)
	{
		m_AssetRegistry[handle] = type;
	}

	void RuntimeAssetManager::LoadAssetAsync(AssetHandle handle,
		std::function<void(AssetHandle, Ref<Asset>)> callback)
	{
		if (!IsAssetHandleValid(handle))
		{
			LUX_CORE_WARN("RuntimeAssetManager::LoadAssetAsync – unknown handle {}", (uint64_t)handle);
			return;
		}

		RuntimeAssetLoadRequest request;
		request.Handle   = handle;
		request.Type     = GetAssetType(handle);
		request.Callback = std::move(callback);
		m_AssetSystem.QueueAssetLoad(std::move(request));
	}

	void RuntimeAssetManager::SyncLoadedAssets()
	{
		m_AssetSystem.SyncLoadedAssets(m_LoadedAssets);
	}

	void RuntimeAssetManager::SetRuntimeLoader(RuntimeAssetSystem::RuntimeLoader loader)
	{
		m_AssetSystem.SetLoader(std::move(loader));
	}
}
