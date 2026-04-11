#include "lpch.h"
#include "TextureSerializer.h"

#include "TextureImporter.h"

namespace Lux
{
	void TextureSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		// Source-format textures (.png/.jpg/…) are their own serialized form – nothing to write back.
	}

	bool TextureSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		asset = TextureImporter::ImportTexture(metadata.Handle, metadata);
		return asset != nullptr;
	}
}
