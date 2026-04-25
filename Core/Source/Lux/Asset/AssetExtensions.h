#pragma once

#include "AssetTypes.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Lux::AssetExtensions
{
	inline const std::unordered_map<std::string, AssetType>& GetExtensionMap()
	{
		static const std::unordered_map<std::string, AssetType> s_ExtensionMap = {
			{ ".luxscene", AssetType::Scene },
			{ ".lprefab", AssetType::Prefab },
			{ ".png", AssetType::Texture },
			{ ".jpg", AssetType::Texture },
			{ ".jpeg", AssetType::Texture },
			{ ".hdr", AssetType::EnvMap },
			{ ".wav", AssetType::Audio },
			{ ".ogg", AssetType::Audio },
			{ ".lsoundc", AssetType::SoundConfig },
			{ ".fbx", AssetType::MeshSource },
			{ ".gltf", AssetType::MeshSource },
			{ ".glb", AssetType::MeshSource },
			{ ".obj", AssetType::MeshSource },
			{ ".dae", AssetType::MeshSource },
			{ ".lmesh", AssetType::Mesh },
			{ ".lsmesh", AssetType::StaticMesh },
			{ ".lmat", AssetType::Material },
			{ ".lskel", AssetType::Skeleton },
			{ ".lanim", AssetType::Animation },
			{ ".lanimgraph", AssetType::AnimationGraph },
			{ ".ttf", AssetType::Font },
			{ ".ttc", AssetType::Font },
			{ ".otf", AssetType::Font },
			{ ".lmc", AssetType::MeshCollider },
			{ ".lsoundgraph", AssetType::SoundGraphSound },
			{ ".cs", AssetType::ScriptFile }
		};

		return s_ExtensionMap;
	}

	inline AssetType GetAssetTypeFromExtension(std::string_view extension)
	{
		if (extension.empty())
			return AssetType::None;

		std::string normalized(extension);
		std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c)
		{
			return (char)std::tolower(c);
		});

		const auto& extensionMap = GetExtensionMap();
		auto it = extensionMap.find(normalized);
		return it != extensionMap.end() ? it->second : AssetType::None;
	}

	inline AssetType GetAssetTypeFromPath(const std::filesystem::path& path)
	{
		return GetAssetTypeFromExtension(path.extension().string());
	}

	inline std::string GetDefaultExtensionForAssetType(AssetType type)
	{
		switch (type)
		{
			case AssetType::Scene:           return ".luxscene";
			case AssetType::Prefab:          return ".lprefab";
			case AssetType::Mesh:            return ".lmesh";
			case AssetType::StaticMesh:      return ".lsmesh";
			case AssetType::Material:        return ".lmat";
			case AssetType::SoundConfig:     return ".lsoundc";
			case AssetType::Skeleton:        return ".lskel";
			case AssetType::Animation:       return ".lanim";
			case AssetType::AnimationGraph:  return ".lanimgraph";
			case AssetType::MeshCollider:    return ".lmc";
			case AssetType::SoundGraphSound: return ".lsoundgraph";
			default:                         return {};
		}
	}
}
