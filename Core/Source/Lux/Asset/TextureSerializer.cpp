#include "lpch.h"
#include "TextureSerializer.h"

#include "TextureImporter.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/SceneEnvironment.h"

#include <filesystem>

namespace Lux
{
	void TextureSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		// Source-format textures (.png/.jpg/…) are their own serialized form – nothing to write back.
	}

	bool TextureSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		if (metadata.Type == AssetType::EnvMap)
		{
			if (!std::filesystem::exists(metadata.FilePath))
				return false;

			auto [radianceMap, irradianceMap] = Renderer::CreateEnvironmentMap(metadata.FilePath.string());
			if (!radianceMap || !irradianceMap)
				return false;

			asset = Ref<Environment>::Create(radianceMap, irradianceMap);
			return asset != nullptr;
		}

		asset = TextureImporter::ImportTexture(metadata.Handle, metadata);
		return asset != nullptr;
	}
}
