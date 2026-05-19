#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lux {

	struct ShaderPermutationKey
	{
		std::string ShaderName;
		std::vector<std::pair<std::string, std::string>> Macros;

		bool operator==(const ShaderPermutationKey& other) const
		{
			return ShaderName == other.ShaderName && Macros == other.Macros;
		}
	};

	struct ShaderPermutationKeyHasher
	{
		size_t operator()(const ShaderPermutationKey& key) const
		{
			size_t hash = std::hash<std::string>{}(key.ShaderName);
			for (const auto& [name, value] : key.Macros)
			{
				hash ^= std::hash<std::string>{}(name) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
				hash ^= std::hash<std::string>{}(value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
			}
			return hash;
		}
	};

	class ShaderPermutationCache
	{
	public:
		void Clear() { m_Permutations.clear(); }
		bool Contains(const ShaderPermutationKey& key) const { return m_Permutations.find(key) != m_Permutations.end(); }
		void Add(const ShaderPermutationKey& key)
		{
			if (Contains(key))
				return;

			m_Permutations[key] = static_cast<uint32_t>(m_Permutations.size());
		}
		uint32_t GetPermutationCount() const { return static_cast<uint32_t>(m_Permutations.size()); }

	private:
		std::unordered_map<ShaderPermutationKey, uint32_t, ShaderPermutationKeyHasher> m_Permutations;
	};

}
