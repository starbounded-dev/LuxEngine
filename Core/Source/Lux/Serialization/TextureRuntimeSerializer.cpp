// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "TextureRuntimeSerializer.h"

#include "Lux/Asset/TextureImporter.h"

namespace Lux {

	uint64_t TextureRuntimeSerializer::SerializeToFile(Ref<TextureCube> textureCube, FileStreamWriter& stream)
	{
		struct TextureCubeMetadata
		{
			uint32_t Width, Height;
			uint16_t Format;
			uint8_t Mips;
		};

		uint64_t startPosition = stream.GetStreamPosition();

		// CopyToHostBuffer waits for the GPU copy itself; no settle delay is needed.
		Buffer buffer;
		textureCube->CopyToHostBuffer(buffer);

		TextureCubeMetadata metadata;
		metadata.Width = textureCube->GetWidth();
		metadata.Height = textureCube->GetHeight();
		metadata.Format = (uint16_t)textureCube->GetFormat();
		// The image's real mip count, which is what the buffer holds. GetMipLevelCount() is the
		// full chain even for a cube created without mips.
		metadata.Mips = (uint8_t)textureCube->GetImage()->GetSpecification().Mips;

		stream.WriteRaw(metadata);
		stream.WriteBuffer(buffer);

		buffer.Release();

		return stream.GetStreamPosition() - startPosition;
	}

	Ref<TextureCube> TextureRuntimeSerializer::DeserializeTextureCube(FileStreamReader& stream)
	{
		struct TextureCubeMetadata
		{
			uint32_t Width, Height;
			uint16_t Format;
			uint8_t Mips;
		};

		uint64_t startPosition = stream.GetStreamPosition();
		TextureCubeMetadata metadata;
		stream.ReadRaw<TextureCubeMetadata>(metadata);

		Buffer buffer;
		stream.ReadBuffer(buffer);

		TextureSpecification spec;
		spec.Width = metadata.Width;
		spec.Height = metadata.Height;
		spec.Format = (ImageFormat)metadata.Format;
		// A single stored mip means the source had no chain; match it instead of leaving the
		// rest of a full chain undefined.
		spec.GenerateMips = metadata.Mips > 1;

		Ref<TextureCube> textureCube = TextureCube::Create(spec);
		textureCube->CopyFromBuffer(buffer, metadata.Mips);
		buffer.Release();
		return textureCube;
	}

	uint64_t TextureRuntimeSerializer::SerializeTexture2DToFile(const std::filesystem::path& filepath, FileStreamWriter& stream)
	{
		// NOTE(Yan): serializing/deserializing mips is not support atm
		Texture2DMetadata metadata;
		metadata.Mips = 1;
		Buffer imageBuffer = TextureImporter::ToBufferFromFile(filepath, (ImageFormat&)metadata.Format, metadata.Width, metadata.Height);

		uint64_t writtenSize = SerializeTexture2DToFile(imageBuffer, metadata, stream);
		imageBuffer.Release();
		return writtenSize;
	}

	uint64_t TextureRuntimeSerializer::SerializeTexture2DToFile(Ref<Texture2D> texture, FileStreamWriter& stream)
	{
		// NOTE(Yan): serializing/deserializing mips is not support atm
		Texture2DMetadata metadata;
		metadata.Width = texture->GetWidth();
		metadata.Height = texture->GetHeight();
		metadata.Format = (uint16_t)texture->GetFormat();
		metadata.Mips = 1;
		Buffer imageBuffer;
		texture->CopyToHostBuffer(imageBuffer);

		uint64_t writtenSize = SerializeTexture2DToFile(imageBuffer, metadata, stream);
		imageBuffer.Release();
		return writtenSize;
	}

	uint64_t TextureRuntimeSerializer::SerializeTexture2DToFile(Buffer imageBuffer, const Texture2DMetadata& metadata, FileStreamWriter& stream)
	{
		uint64_t startPosition = stream.GetStreamPosition();

		stream.WriteRaw(metadata);
		stream.WriteBuffer(imageBuffer);


		return stream.GetStreamPosition() - startPosition;
	}

	Ref<Texture2D> TextureRuntimeSerializer::DeserializeTexture2D(FileStreamReader& stream)
	{
		Texture2DMetadata metadata;
		stream.ReadRaw<Texture2DMetadata>(metadata);

		Buffer buffer;
		stream.ReadBuffer(buffer);

		TextureSpecification spec;
		spec.Width = metadata.Width;
		spec.Height = metadata.Height;
		spec.Format = (ImageFormat)metadata.Format;
		spec.GenerateMips = true;

		Ref<Texture2D> texture = Texture2D::Create(spec, buffer);
		buffer.Release();
		return texture;
	}

}
