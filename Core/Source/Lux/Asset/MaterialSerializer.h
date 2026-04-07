#pragma once

#include "AssetSerializer.h"

namespace Lux
{
	// YAML-based serializer for MaterialAsset.
	class MaterialSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		virtual bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const override;
	};
}
