// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

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
		++m_Revision;
	}

	bool AssetRegistry::Contains(AssetHandle handle) const
	{
		return m_AssetRegistry.find(handle) != m_AssetRegistry.end();
	}

	size_t AssetRegistry::Remove(AssetHandle handle)
	{
		const size_t removed = m_AssetRegistry.erase(handle);
		if (removed)
			++m_Revision;
		return removed;
	}

	void AssetRegistry::Clear()
	{
		if (!m_AssetRegistry.empty())
		{
			m_AssetRegistry.clear();
			++m_Revision;
		}
	}
}
