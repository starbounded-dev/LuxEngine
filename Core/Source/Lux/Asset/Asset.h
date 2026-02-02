#pragma once

#include "StarEngine/Core/UUID.h"

#include <string_view>

namespace StarEngine {

	using AssetHandle = UUID;

	enum class AssetType : uint16_t
	{
		None = 0,
		Scene,
		Texture2D,
		Audio,/*
		ObjModel,
		ScriptFile,*/
	};

	std::string_view AssetTypeToString(AssetType type);
	AssetType AssetTypeFromString(std::string_view assetType);

	class Asset
	{
	public:
		AssetHandle Handle; // Generate handle

		virtual AssetType GetType() const = 0;
	};

}
