#include "lpch.h"
#include "TextureImporter.h"

#include "stb_image.h"

#include "Lux/Utilities/FileSystem.h"
#include "Lux/Renderer/Texture.h"

#include <iostream>

namespace Lux {
	Buffer TextureImporter::ToBufferFromFile(const std::filesystem::path& path, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight)
	{
		FileStatus fileStatus = FileSystem::TryOpenFileAndWait(path, 100);
		Buffer imageBuffer;
		std::string pathString = path.string();
		bool isSRGB = (outFormat == ImageFormat::SRGB) || (outFormat == ImageFormat::SRGBA);

		int width, height, channels;
		void* tmp;
		size_t size = 0;

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
		stbi_set_flip_vertically_on_load(1);
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

	Buffer TextureImporter::ToBufferFromMemory(Buffer buffer, ImageFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight)
	{
		Buffer imageBuffer;

		bool isSRGB = (outFormat == ImageFormat::SRGB) || (outFormat == ImageFormat::SRGBA);

		int width, height, channels;
		void* tmp;
		size_t size;

		if (stbi_is_hdr_from_memory((const stbi_uc*)buffer.Data, (int)buffer.Size))
		{
			tmp = (byte*)stbi_loadf_from_memory((const stbi_uc*)buffer.Data, (int)buffer.Size, &width, &height, &channels, STBI_rgb_alpha);
			size = width * height * 4 * sizeof(float);
			outFormat = ImageFormat::RGBA32F;
		}
		else
		{
		stbi_set_flip_vertically_on_load(1);
		tmp = stbi_load_from_memory((const stbi_uc*)buffer.Data, (int)buffer.Size, &width, &height, &channels, STBI_rgb_alpha);
			size = width * height * 4;
			outFormat = isSRGB ? ImageFormat::SRGBA : ImageFormat::RGBA;
		}

		imageBuffer.Data = new byte[size]; // avoid `malloc+delete[]` mismatch.
		imageBuffer.Size = size;
		memcpy(imageBuffer.Data, tmp, size);
		stbi_image_free(tmp);

		if (!imageBuffer.Data)
			return {};

		outWidth = width;
		outHeight = height;
		return imageBuffer;
	}

}
