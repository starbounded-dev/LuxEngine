#pragma once

#include "Asset.h"

#include <filesystem>

namespace Lux
{
	enum class AssetStatus
	{
		None = 0,
		Ready = 1,
		Invalid = 2,
		Loading = 3,
		Missing = 4
	};

	struct AssetMetadata
	{
		AssetHandle Handle = 0;
		AssetType Type = AssetType::None;
		std::filesystem::path FilePath;
		AssetStatus Status = AssetStatus::None;
		uint64_t FileLastWriteTime = 0;
		bool IsDataLoaded = false;

		bool IsValid() const { return Handle != 0; }
		operator bool() const { return IsValid(); }
	};

	struct EditorAssetLoadResponse
	{
		AssetMetadata Metadata;
		Ref<Asset> Asset;
	};

	struct RuntimeAssetLoadRequest
	{
		AssetHandle SceneHandle = 0;
		AssetHandle Handle = 0;
	};

}
