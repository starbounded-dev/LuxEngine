#pragma once

#include "Asset.h"
#include "AssetMetadata.h"

#include "StarEngine/Renderer/Texture.h"

namespace StarEngine {

	class TextureImporter
	{
	public:
		static Ref<Texture2D> LoadTexture2D(const std::filesystem::path& path, bool srgb = true);

		static Buffer ToBufferFromFile(const std::filesystem::path& path, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight);
		static Buffer ToBufferFromMemory(Buffer buffer, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight);
	private:
		const std::filesystem::path m_Path;
	};
}
