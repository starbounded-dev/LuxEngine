#pragma once

#include "AssetMetadata.h"

#include "Lux/Renderer/Texture.h"

#include <filesystem>

namespace Lux {

	struct TextureImportSettings
	{
		bool FlipVertically = true;
		bool GenerateMips = true;
		TextureCompressionFormat Compression = TextureCompressionFormat::None;
		TextureMipPolicy MipPolicy = TextureMipPolicy::FullChain;
		float MipBias = 0.0f;
	};

	class TextureImporter
	{
	public:
		static Buffer ToBufferFromFile(const std::filesystem::path& path, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight, bool flipVertically = true);
		static Buffer ToBufferFromFile(const std::filesystem::path& path, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight, const TextureImportSettings& settings);
		static Buffer ToBufferFromMemory(Buffer buffer, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight, bool flipVertically = true);
		static Buffer ToBufferFromMemory(Buffer buffer, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight, const TextureImportSettings& settings);
		static bool SupportsBlockCompression(TextureCompressionFormat compression);
		static bool CanEncodeBlockCompression(TextureCompressionFormat compression);
		static ImageFormat CompressionToImageFormat(TextureCompressionFormat compression, bool srgb);
		static const char* CompressionFormatToString(TextureCompressionFormat compression);
	private:
		const std::filesystem::path m_Path;
	};
}
