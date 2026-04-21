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
			{ ".hazel", AssetType::Scene },
			{ ".png", AssetType::Texture },
			{ ".jpg", AssetType::Texture },
			{ ".jpeg", AssetType::Texture },
			{ ".hdr", AssetType::EnvMap },
			{ ".mp3", AssetType::Audio },
			{ ".wav", AssetType::Audio },
			{ ".ogg", AssetType::Audio },
			{ ".fbx", AssetType::MeshSource },
			{ ".gltf", AssetType::MeshSource },
			{ ".glb", AssetType::MeshSource },
			{ ".obj", AssetType::MeshSource },
			{ ".lmesh", AssetType::Mesh },
			{ ".lsmesh", AssetType::StaticMesh },
			{ ".lmat", AssetType::Material },
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
		case AssetType::Scene:      return ".luxscene";
		case AssetType::Mesh:       return ".lmesh";
		case AssetType::StaticMesh: return ".lsmesh";
		case AssetType::Material:   return ".lmat";
		default:                    return {};
		}
	}
}
