#pragma once

#include "AssetMetadata.h"

#include <unordered_map>

namespace Lux
{
	class AssetRegistry
	{
	public:
		using RegistryMap = std::unordered_map<AssetHandle, AssetMetadata>;
		using iterator = RegistryMap::iterator;
		using const_iterator = RegistryMap::const_iterator;

		const AssetMetadata& Get(AssetHandle handle) const;
		void Set(AssetHandle handle, const AssetMetadata& metadata);

		size_t Count() const { return m_AssetRegistry.size(); }
		size_t size() const { return m_AssetRegistry.size(); }
		bool empty() const { return m_AssetRegistry.empty(); }
		bool Contains(AssetHandle handle) const;
		bool contains(AssetHandle handle) const { return Contains(handle); }
		size_t Remove(AssetHandle handle);
		void Clear();
		const AssetMetadata& at(AssetHandle handle) const { return Get(handle); }
		iterator find(AssetHandle handle) { return m_AssetRegistry.find(handle); }
		const_iterator find(AssetHandle handle) const { return m_AssetRegistry.find(handle); }

		iterator begin() { return m_AssetRegistry.begin(); }
		iterator end() { return m_AssetRegistry.end(); }
		const_iterator begin() const { return m_AssetRegistry.cbegin(); }
		const_iterator end() const { return m_AssetRegistry.cend(); }
		const_iterator cbegin() const { return m_AssetRegistry.cbegin(); }
		const_iterator cend() const { return m_AssetRegistry.cend(); }

	private:
		RegistryMap m_AssetRegistry;
	};
}
