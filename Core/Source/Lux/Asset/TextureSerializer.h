#pragma once

#include "AssetSerializer.h"

namespace Lux
{
	// Serializes / deserializes Texture2D assets.
	// Editor path: delegates to TextureImporter (stb_image).
	// The serialize direction is a no-op for source-format textures
	// (the source file IS the serialized form).
	class TextureSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		virtual bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const override;
	};
}
