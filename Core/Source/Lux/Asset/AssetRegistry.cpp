#include "lpch.h"
#include "AssetRegistry.h"

namespace Lux
{
	const AssetMetadata& AssetRegistry::Get(AssetHandle handle) const
	{
		LUX_CORE_ASSERT(Contains(handle));
		return m_AssetRegistry.at(handle);
	}

	void AssetRegistry::Set(AssetHandle handle, const AssetMetadata& metadata)
	{
		m_AssetRegistry[handle] = metadata;
	}

	bool AssetRegistry::Contains(AssetHandle handle) const
	{
		return m_AssetRegistry.find(handle) != m_AssetRegistry.end();
	}

	size_t AssetRegistry::Remove(AssetHandle handle)
	{
		return m_AssetRegistry.erase(handle);
	}

	void AssetRegistry::Clear()
	{
		m_AssetRegistry.clear();
	}
}
