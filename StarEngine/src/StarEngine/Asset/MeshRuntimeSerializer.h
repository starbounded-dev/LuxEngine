#pragma once

#include "StarEngine/Asset/Asset.h"
#include "StarEngine/Asset/AssetSerializer.h"

#include "StarEngine/Serialization/AssetPack.h"
#include "StarEngine/Serialization/AssetPackFile.h"
#include "StarEngine/Serialization/FileStream.h"

namespace StarEngine {

	class MeshRuntimeSerializer
	{
	public:
		bool SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo);
		Ref<Asset> DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo);
	};

}
