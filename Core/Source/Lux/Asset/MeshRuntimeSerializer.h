#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Asset/AssetSerializer.h"

#include "Lux/Serialization/AssetPack.h"
#include "Lux/Serialization/AssetPackFile.h"
#include "Lux/Serialization/FileStream.h"

namespace Lux {

	class MeshRuntimeSerializer
	{
	public:
		bool SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo);
		Ref<Asset> DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo);
	};

}

