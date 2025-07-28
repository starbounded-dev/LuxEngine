#include "sepch.h"
#include "AssetRegistry.h"

#include "StarEngine/Core/Application.h"

namespace StarEngine {

#define SE_ASSETREGISTRY_LOG 0
#if SE_ASSETREGISTRY_LOG
#define ASSET_LOG(...) SE_CORE_TRACE_TAG("ASSET", __VA_ARGS__)
#else 
#define ASSET_LOG(...)
#endif

	const AssetMetadata& AssetRegistry::Get(const AssetHandle handle) const
	{
		SE_CORE_ASSERT(m_AssetRegistry.find(handle) != m_AssetRegistry.end());
		ASSET_LOG("Retrieving const handle {}", handle);
		return m_AssetRegistry.at(handle);
	}

	void AssetRegistry::Set(const AssetHandle handle, const AssetMetadata& metadata)
	{
		SE_CORE_ASSERT(metadata.Handle == handle);
		SE_CORE_ASSERT(handle != 0);
		SE_CORE_ASSERT(Application::IsMainThread(), "AssetRegistry::Set() has been called from other than the main thread!"); // Refer comments in EditorAssetManager
		m_AssetRegistry[handle] = metadata;
	}

	bool AssetRegistry::Contains(const AssetHandle handle) const
	{
		ASSET_LOG("Contains handle {}", handle);
		return m_AssetRegistry.find(handle) != m_AssetRegistry.end();
	}

	size_t AssetRegistry::Remove(const AssetHandle handle)
	{
		ASSET_LOG("Removing handle", handle);
		return m_AssetRegistry.erase(handle);
	}

	void AssetRegistry::Clear()
	{
		ASSET_LOG("Clearing registry");
		m_AssetRegistry.clear();
	}

}
