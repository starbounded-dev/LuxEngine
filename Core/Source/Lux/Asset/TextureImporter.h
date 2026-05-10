#pragma once

#include "AssetMetadata.h"

#include "Lux/Renderer/Texture.h"

#include <filesystem>

namespace Lux {

	class TextureImporter
	{
	public:
		static Buffer ToBufferFromFile(const std::filesystem::path& path, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight, bool flipVertically = true);
		static Buffer ToBufferFromMemory(Buffer buffer, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight, bool flipVertically = true);
	private:
		const std::filesystem::path m_Path;
	};
}
