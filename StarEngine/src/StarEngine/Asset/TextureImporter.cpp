#include "sepch.h"
#include "TextureImporter.h"

#include "StarEngine/Project/Project.h"

#include <stb_image.h>

namespace StarEngine {

	Ref<Texture2D> TextureImporter::ImportTexture2D(AssetHandle handle, const AssetMetadata& metadata)
	{
		SE_PROFILE_FUNCTION("TextureImporter::ImportTexture2D");

		return LoadTexture2D(Project::GetActiveAssetDirectory() / metadata.FilePath);
	}

	Ref<Texture2D> TextureImporter::LoadTexture2D(const std::filesystem::path& path)
	{
		SE_PROFILE_FUNCTION("TextureImporter::LoadTexture2D");
		int width, height, channels;
		stbi_set_flip_vertically_on_load(1);
		Buffer data;

		{
			SE_PROFILE_SCOPE("stbi_load - TextureImporter::ImportTexture2D");
			std::string pathStr = path.string();
			data.Data = stbi_load(pathStr.c_str(), &width, &height, &channels, 4);
			channels = 4;
		}

		if (data.Data == nullptr)
		{
			SE_CORE_ERROR("TextureImporter::ImportTexture2D - Could not load texture from filepath: {}", path.string());
			return nullptr;
		}

		// TODO: think about this
		data.Size = width * height * channels;

		TextureSpecification spec;
		spec.Width = width;
		spec.Height = height;
		switch (channels)
		{
		case 3:
			spec.Format = ImageFormat::RGB8;
			break;
		case 4:
			spec.Format = ImageFormat::RGBA8;
			break;
		}

		Ref<Texture2D> texture = Texture2D::Create(spec, data);
		data.Release();
		return texture;
	}
}
