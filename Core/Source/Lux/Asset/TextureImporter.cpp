#include "lpch.h"
#include "TextureImporter.h"

#include "stb_image.h"

#include "Lux/Utilities/FileSystem.h"
#include "Lux/Renderer/Texture.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>

namespace Lux {
	namespace {
		constexpr uint32_t FourCC(char a, char b, char c, char d)
		{
			return uint32_t(a) | (uint32_t(b) << 8) | (uint32_t(c) << 16) | (uint32_t(d) << 24);
		}

		bool IsDDSFile(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return extension == ".dds";
		}

		ImageFormat DXGIFormatToImageFormat(uint32_t dxgiFormat)
		{
			switch (dxgiFormat)
			{
				case 71: return ImageFormat::BC1;
				case 72: return ImageFormat::BC1_SRGB;
				case 77: return ImageFormat::BC3;
				case 78: return ImageFormat::BC3_SRGB;
				case 83: return ImageFormat::BC5;
				case 84: return ImageFormat::BC5_SNORM;
				case 98: return ImageFormat::BC7;
				case 99: return ImageFormat::BC7_SRGB;
				default: return ImageFormat::None;
			}
		}

		ImageFormat DDSFourCCToImageFormat(uint32_t fourCC)
		{
			switch (fourCC)
			{
				case FourCC('D', 'X', 'T', '1'): return ImageFormat::BC1;
				case FourCC('D', 'X', 'T', '5'): return ImageFormat::BC3;
				case FourCC('A', 'T', 'I', '2'): return ImageFormat::BC5;
				case FourCC('B', 'C', '5', 'U'): return ImageFormat::BC5;
				default: return ImageFormat::None;
			}
		}

		uint32_t ReadU32(const std::vector<byte>& bytes, size_t offset)
		{
			if (offset + sizeof(uint32_t) > bytes.size())
				return 0;

			uint32_t value = 0;
			memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
			return value;
		}

		Buffer TryLoadDDSFromFile(const std::filesystem::path& path, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight)
		{
			std::ifstream stream(path, std::ios::binary | std::ios::ate);
			if (!stream)
				return {};

			const std::streamsize fileSize = stream.tellg();
			if (fileSize < 128)
				return {};

			stream.seekg(0, std::ios::beg);
			std::vector<byte> bytes(static_cast<size_t>(fileSize));
			stream.read(reinterpret_cast<char*>(bytes.data()), fileSize);
			if (!stream)
				return {};

			if (ReadU32(bytes, 0) != FourCC('D', 'D', 'S', ' '))
				return {};

			const uint32_t headerSize = ReadU32(bytes, 4);
			const uint32_t height = ReadU32(bytes, 12);
			const uint32_t width = ReadU32(bytes, 16);
			const uint32_t pixelFormatSize = ReadU32(bytes, 76);
			const uint32_t fourCC = ReadU32(bytes, 84);
			if (headerSize != 124 || pixelFormatSize != 32 || width == 0 || height == 0)
				return {};

			size_t dataOffset = 128;
			ImageFormat format = ImageFormat::None;
			if (fourCC == FourCC('D', 'X', '1', '0'))
			{
				if (bytes.size() < 148)
					return {};

				format = DXGIFormatToImageFormat(ReadU32(bytes, 128));
				dataOffset = 148;
			}
			else
			{
				format = DDSFourCCToImageFormat(fourCC);
			}

			if (format == ImageFormat::None)
				return {};

			const uint32_t mip0Size = Utils::GetImageMemorySize(format, width, height);
			if (dataOffset + mip0Size > bytes.size())
				return {};

			Buffer imageBuffer;
			imageBuffer.Data = new byte[mip0Size];
			imageBuffer.Size = mip0Size;
			memcpy(imageBuffer.Data, bytes.data() + dataOffset, mip0Size);

			outFormat = format;
			outWidth = width;
			outHeight = height;
			return imageBuffer;
		}
	}

	Buffer TextureImporter::ToBufferFromFile(const std::filesystem::path& path, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight, const TextureImportSettings& settings)
	{
		if (IsDDSFile(path))
		{
			Buffer ddsBuffer = TryLoadDDSFromFile(path, outFormat, outWidth, outHeight);
			if (ddsBuffer)
				return ddsBuffer;
		}

		if (settings.Compression != TextureCompressionFormat::None)
			LUX_CORE_WARN("Texture compression {} requested for '{}'. GPU BC formats are supported, but the editor BC encoder is not wired yet. Loading source pixels for now.", CompressionFormatToString(settings.Compression), path.string());

		return ToBufferFromFile(path, outFormat, outWidth, outHeight, settings.FlipVertically);
	}

	Buffer TextureImporter::ToBufferFromFile(const std::filesystem::path& path, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight, bool flipVertically)
	{
		if (IsDDSFile(path))
		{
			Buffer ddsBuffer = TryLoadDDSFromFile(path, outFormat, outWidth, outHeight);
			if (ddsBuffer)
				return ddsBuffer;
		}

		FileStatus fileStatus = FileSystem::TryOpenFileAndWait(path, 100);
		Buffer imageBuffer;
		std::string pathString = path.string();
		bool isSRGB = (outFormat == ImageFormat::SRGB) || (outFormat == ImageFormat::SRGBA);

		int width, height, channels;
		void* tmp;
		size_t size = 0;

		stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
		if (stbi_is_hdr(pathString.c_str()))
		{
			tmp = stbi_loadf(pathString.c_str(), &width, &height, &channels, 4);
			if (tmp)
			{
				size = width * height * 4 * sizeof(float);
				outFormat = ImageFormat::RGBA32F;
			}
		}
		else
		{
			tmp = stbi_load(pathString.c_str(), &width, &height, &channels, 4);
			if (tmp)
			{
				size = width * height * 4;
				outFormat = isSRGB ? ImageFormat::SRGBA : ImageFormat::RGBA;
			}
		}

		if (!tmp)
		{
			return {};
		}

		LUX_CORE_ASSERT(size > 0);
		imageBuffer.Data = new byte[size]; // avoid `malloc+delete[]` mismatch.
		imageBuffer.Size = size;
		memcpy(imageBuffer.Data, tmp, size);
		stbi_image_free(tmp);

		outWidth = width;
		outHeight = height;
		return imageBuffer;
	}

	Buffer TextureImporter::ToBufferFromMemory(Buffer buffer, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight, const TextureImportSettings& settings)
	{
		if (settings.Compression != TextureCompressionFormat::None)
			LUX_CORE_WARN("Texture compression {} requested from memory. GPU BC formats are supported, but the editor BC encoder is not wired yet. Loading source pixels for now.", CompressionFormatToString(settings.Compression));

		return ToBufferFromMemory(buffer, outFormat, outWidth, outHeight, settings.FlipVertically);
	}

	Buffer TextureImporter::ToBufferFromMemory(Buffer buffer, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight, bool flipVertically)
	{
		Buffer imageBuffer;

		bool isSRGB = (outFormat == ImageFormat::SRGB) || (outFormat == ImageFormat::SRGBA);

		int width, height, channels;
		void* tmp;
		size_t size = 0;

		stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
		if (stbi_is_hdr_from_memory((const stbi_uc*)buffer.Data, (int)buffer.Size))
		{
			tmp = (byte*)stbi_loadf_from_memory((const stbi_uc*)buffer.Data, (int)buffer.Size, &width, &height, &channels, STBI_rgb_alpha);
			if (tmp)
			{
				size = width * height * 4 * sizeof(float);
				outFormat = ImageFormat::RGBA32F;
			}
		}
		else
		{
			tmp = stbi_load_from_memory((const stbi_uc*)buffer.Data, (int)buffer.Size, &width, &height, &channels, STBI_rgb_alpha);
			if (tmp)
			{
				size = width * height * 4;
				outFormat = isSRGB ? ImageFormat::SRGBA : ImageFormat::RGBA;
			}
		}

		if (!tmp)
			return {};

		imageBuffer.Data = new byte[size]; // avoid `malloc+delete[]` mismatch.
		imageBuffer.Size = size;
		memcpy(imageBuffer.Data, tmp, size);
		stbi_image_free(tmp);

		outWidth = width;
		outHeight = height;
		return imageBuffer;
	}

	bool TextureImporter::SupportsBlockCompression(TextureCompressionFormat compression)
	{
		switch (compression)
		{
			case TextureCompressionFormat::BC1:
			case TextureCompressionFormat::BC3:
			case TextureCompressionFormat::BC5:
			case TextureCompressionFormat::BC7:
				return true;
			case TextureCompressionFormat::None:
			default:
				return false;
		}
	}

	bool TextureImporter::CanEncodeBlockCompression(TextureCompressionFormat compression)
	{
		if (compression == TextureCompressionFormat::None)
			return true;

		return false;
	}

	ImageFormat TextureImporter::CompressionToImageFormat(TextureCompressionFormat compression, bool srgb)
	{
		switch (compression)
		{
			case TextureCompressionFormat::BC1: return srgb ? ImageFormat::BC1_SRGB : ImageFormat::BC1;
			case TextureCompressionFormat::BC3: return srgb ? ImageFormat::BC3_SRGB : ImageFormat::BC3;
			case TextureCompressionFormat::BC5: return ImageFormat::BC5;
			case TextureCompressionFormat::BC7: return srgb ? ImageFormat::BC7_SRGB : ImageFormat::BC7;
			case TextureCompressionFormat::None:
			default:
				return ImageFormat::None;
		}
	}

	const char* TextureImporter::CompressionFormatToString(TextureCompressionFormat compression)
	{
		switch (compression)
		{
			case TextureCompressionFormat::BC1: return "BC1";
			case TextureCompressionFormat::BC3: return "BC3";
			case TextureCompressionFormat::BC5: return "BC5";
			case TextureCompressionFormat::BC7: return "BC7";
			case TextureCompressionFormat::None:
			default:
				return "None";
		}
	}

}
