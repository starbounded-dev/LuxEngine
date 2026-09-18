#pragma once
#include "AssetSerializer.h"

namespace Lux
{
	class DialogueTableSerializer : public AssetSerializer
	{
	public:
		void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const override;
		bool SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const override;
		Ref<Asset> DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const override;
	};
}
