#pragma once

#include "StarEngine/Core/UUID.h"

#include "StarEngine/Asset/AssetTypes.h"

#include <string_view>

namespace StarEngine {

	using AssetHandle = UUID;

	std::string_view AssetTypeToString(AssetType type);
	AssetType AssetTypeFromString(std::string_view assetType);

	class Asset : public RefCounted
	{
	public:
		AssetHandle Handle; // Generate handle

		static AssetType GetStaticType() { return AssetType::None; }
		virtual AssetType GetAssetType() const { return AssetType::None; }
	};

}
