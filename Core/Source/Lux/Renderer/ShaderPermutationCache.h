#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
		bool Contains(const ShaderPermutationKey& key) const
		{
			return m_Permutations.find(NormalizeKey(key)) != m_Permutations.end();
		}
		void Add(const ShaderPermutationKey& key)
		{
			ShaderPermutationKey normalizedKey = NormalizeKey(key);
			if (m_Permutations.find(normalizedKey) != m_Permutations.end())
				return;

			m_Permutations[std::move(normalizedKey)] = static_cast<uint32_t>(m_Permutations.size());
		}
		uint32_t GetPermutationCount() const { return static_cast<uint32_t>(m_Permutations.size()); }

		std::vector<ShaderPermutationKey> GetKeys() const
		{
			std::vector<ShaderPermutationKey> keys;
			keys.reserve(m_Permutations.size());
			for (const auto& [key, index] : m_Permutations)
				keys.push_back(key);
			std::sort(keys.begin(), keys.end(), [](const ShaderPermutationKey& a, const ShaderPermutationKey& b)
				{
					if (a.ShaderName != b.ShaderName)
						return a.ShaderName < b.ShaderName;
					return a.Macros < b.Macros;
				});
			return keys;
		}

		bool LoadFromFile(const std::filesystem::path& path)
		{
			std::ifstream stream(path);
			if (!stream)
				return false;

			ShaderPermutationKey key;
			uint32_t macroCount = 0;
			while (stream >> std::quoted(key.ShaderName) >> macroCount)
			{
				key.Macros.clear();
				key.Macros.reserve(macroCount);
				for (uint32_t macro = 0; macro < macroCount; macro++)
				{
					std::string name;
					std::string value;
					stream >> std::quoted(name) >> std::quoted(value);
					key.Macros.emplace_back(std::move(name), std::move(value));
				}
				Add(key);
			}

			return true;
		}

		bool SaveToFile(const std::filesystem::path& path) const
		{
			const std::filesystem::path parent = path.parent_path();
			if (!parent.empty())
				std::filesystem::create_directories(parent);

			std::ofstream stream(path);
			if (!stream)
				return false;

			for (const ShaderPermutationKey& key : GetKeys())
			{
				stream << std::quoted(key.ShaderName) << ' ' << key.Macros.size();
				for (const auto& [name, value] : key.Macros)
					stream << ' ' << std::quoted(name) << ' ' << std::quoted(value);
				stream << '\n';
			}

			return true;
		}

	private:
		static ShaderPermutationKey NormalizeKey(ShaderPermutationKey key)
		{
			std::sort(key.Macros.begin(), key.Macros.end(), [](const auto& a, const auto& b)
				{
					if (a.first != b.first)
						return a.first < b.first;
					return a.second < b.second;
				});
			return key;
		}

		std::unordered_map<ShaderPermutationKey, uint32_t, ShaderPermutationKeyHasher> m_Permutations;
	};

}
