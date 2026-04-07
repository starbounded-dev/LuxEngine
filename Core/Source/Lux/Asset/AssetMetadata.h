#pragma once

#include "Asset.h"

#include <filesystem>

namespace Lux
{
	struct AssetMetadata
	{
		AssetHandle Handle = 0;
		AssetType Type = AssetType::None;
		std::filesystem::path FilePath = "";

		operator bool() const { return Type != AssetType::None; }
	};

}
