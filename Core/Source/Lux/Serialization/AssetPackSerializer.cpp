// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AssetPackSerializer.h"
#include "Lux/Asset/AssetImporter.h"

#include "Lux/Serialization/FileStream.h"

#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace Lux {

	static void CreateDirectoriesIfNeeded(const std::filesystem::path& path)
	{
		std::filesystem::path directory = path.parent_path();
		if (!std::filesystem::exists(directory))
			std::filesystem::create_directories(directory);
	}

	bool AssetPackSerializer::Serialize(const std::filesystem::path& path, AssetPackFile& file, Buffer appBinary, std::atomic<float>& progress)
	{
		ScopedBuffer ownedBinary(appBinary);
		// Print Info
		LUX_CORE_TRACE("Serializing AssetPack to {}", path.string());
		LUX_CORE_TRACE("  {} scenes", file.Index.Scenes.size());
		uint32_t assetCount = 0;
		for (const auto& [sceneHandle, sceneInfo] : file.Index.Scenes)
			assetCount += uint32_t(sceneInfo.Assets.size());
		LUX_CORE_TRACE("  {} assets (including duplicates)", assetCount);

		FileStreamWriter serializer(path);
		if (!serializer.IsStreamGood() || file.Index.Scenes.empty())
		{
			LUX_CORE_ERROR_TAG("Asset Pack", "Cannot write asset pack '{}' (unwritable file or no scenes)", path.string());
			return false;
		}

		serializer.WriteRaw<AssetPackFile::FileHeader>(file.Header);

		// ===============
		// Write index
		// ===============
		// Write dummy data for index (come back later to fill in)
		uint64_t indexPos = serializer.GetStreamPosition();
		uint64_t indexTableSize = CalculateIndexTableSize(file);
		serializer.WriteZero(indexTableSize);

		std::unordered_map<AssetHandle, AssetSerializationInfo> serializedAssets;

		float progressIncrement = 0.4f / (float)file.Index.Scenes.size();

		// Write app binary data
		file.Index.PackedAppBinaryOffset = serializer.GetStreamPosition();
		serializer.WriteBuffer(appBinary);
		file.Index.PackedAppBinarySize = serializer.GetStreamPosition() - file.Index.PackedAppBinaryOffset;

		// Write asset data + fill in offset + size
		for (auto& [sceneHandle, sceneInfo] : file.Index.Scenes)
		{
			// Serialize Scene
			AssetSerializationInfo serializationInfo;
			if (!AssetImporter::SerializeToAssetPack(sceneHandle, serializer, serializationInfo) || !serializer.IsStreamGood())
			{
				LUX_CORE_ERROR_TAG("Asset Pack", "Failed to pack scene {}", static_cast<uint64_t>(sceneHandle));
				return false;
			}
			file.Index.Scenes[sceneHandle].PackedOffset = serializationInfo.Offset;
			file.Index.Scenes[sceneHandle].PackedSize = serializationInfo.Size;

			// Serialize Assets
			for (auto& [assetHandle, assetInfo] : sceneInfo.Assets)
			{
				if (serializedAssets.find(assetHandle) != serializedAssets.end())
				{
					// Has already been serialized
					serializationInfo = serializedAssets.at(assetHandle);
					file.Index.Scenes[sceneHandle].Assets[assetHandle].PackedOffset = serializationInfo.Offset;
					file.Index.Scenes[sceneHandle].Assets[assetHandle].PackedSize = serializationInfo.Size;
				}
				else
				{
					// Serialize asset
					if (AssetImporter::SerializeToAssetPack(assetHandle, serializer, serializationInfo) && serializer.IsStreamGood())
					{
						file.Index.Scenes[sceneHandle].Assets[assetHandle].PackedOffset = serializationInfo.Offset;
						file.Index.Scenes[sceneHandle].Assets[assetHandle].PackedSize = serializationInfo.Size;
						serializedAssets[assetHandle] = serializationInfo;
					}
					else
					{
						LUX_CORE_ERROR_TAG("Asset Pack", "Failed to pack asset {}", static_cast<uint64_t>(assetHandle));
						return false;
					}
				}
			}

			progress = progress + progressIncrement;
		}

		LUX_CORE_TRACE("Serialized {} assets into AssetPack", serializedAssets.size());

		serializer.SetStreamPosition(indexPos);
		serializer.WriteRaw<uint64_t>(file.Index.PackedAppBinaryOffset);
		serializer.WriteRaw<uint64_t>(file.Index.PackedAppBinarySize);

		uint64_t begin = indexPos;
		serializer.WriteRaw<uint32_t>((uint32_t)file.Index.Scenes.size()); // Write scene map size
		for (auto& [sceneHandle, sceneInfo] : file.Index.Scenes)
		{
			serializer.WriteRaw<uint64_t>(sceneHandle);
			serializer.WriteRaw<uint64_t>(sceneInfo.PackedOffset);
			serializer.WriteRaw<uint64_t>(sceneInfo.PackedSize);
			serializer.WriteRaw<uint16_t>(sceneInfo.Flags);

			serializer.WriteMap(file.Index.Scenes[sceneHandle].Assets);
		}

		if (!serializer.Flush())
		{
			LUX_CORE_ERROR_TAG("Asset Pack", "Failed writing asset pack '{}'", path.string());
			return false;
		}
		progress = progress + 0.1f;
		return true;
	}

	bool AssetPackSerializer::DeserializeIndex(const std::filesystem::path& path, AssetPackFile& file)
	{
		// Print Info
		LUX_CORE_TRACE_TAG("Asset Pack", "Deserializing Asset Pack from {}", path.string());

		FileStreamReader stream(path);
		if (!stream.IsStreamGood())
			return false;

		// Everything below is read from a file that may be damaged: each failure is an error the
		// runtime reports, never an assert.
		auto invalid = [&](const char* what)
		{
			LUX_CORE_ERROR_TAG("Asset Pack", "'{}' is damaged: {}", path.string(), what);
			file.Index = {};
			return false;
		};

		if (stream.GetStreamSize() < sizeof(AssetPackFile::FileHeader) || !stream.ReadData(reinterpret_cast<char*>(&file.Header), sizeof(file.Header)))
			return invalid("the header is truncated");
		if (memcmp(file.Header.HEADER, "LZAP", 4) != 0)
			return invalid("it is not an asset pack (bad magic)");

		AssetPackFile current;
		if (file.Header.Version != current.Header.Version)
		{
			LUX_CORE_ERROR("AssetPack version {} is not compatible with current version {}", file.Header.Version, current.Header.Version);
			return false;
		}

		// A packed range must lie inside the file.
		const uint64_t fileSize = stream.GetStreamSize();
		auto inFile = [fileSize](uint64_t offset, uint64_t size) { return offset <= fileSize && size <= fileSize - offset; };

		// Read app binary info
		uint32_t sceneCount = 0;
		if (!stream.ReadData(reinterpret_cast<char*>(&file.Index.PackedAppBinaryOffset), sizeof(uint64_t))
			|| !stream.ReadData(reinterpret_cast<char*>(&file.Index.PackedAppBinarySize), sizeof(uint64_t))
			|| !stream.ReadData(reinterpret_cast<char*>(&sceneCount), sizeof(sceneCount)))
			return invalid("the index is truncated");
		if (!inFile(file.Index.PackedAppBinaryOffset, file.Index.PackedAppBinarySize))
			return invalid("the app binary lies outside the file");

		// Smallest scene entry: handle, offset, size, flags and an empty asset map.
		constexpr uint64_t k_MinimumSceneEntrySize = sizeof(uint64_t) * 3 + sizeof(uint16_t) + sizeof(uint32_t);
		if (sceneCount == 0 || sceneCount > stream.GetRemainingBytes() / k_MinimumSceneEntrySize)
			return invalid("the scene count does not fit the file");

		for (uint32_t i = 0; i < sceneCount; i++)
		{
			uint64_t sceneHandle = 0;
			AssetPackFile::SceneInfo sceneInfo;
			if (!stream.ReadData(reinterpret_cast<char*>(&sceneHandle), sizeof(sceneHandle))
				|| !stream.ReadData(reinterpret_cast<char*>(&sceneInfo.PackedOffset), sizeof(uint64_t))
				|| !stream.ReadData(reinterpret_cast<char*>(&sceneInfo.PackedSize), sizeof(uint64_t))
				|| !stream.ReadData(reinterpret_cast<char*>(&sceneInfo.Flags), sizeof(uint16_t))
				|| !stream.ReadMap(sceneInfo.Assets))
				return invalid("the scene table is truncated");
			if (sceneHandle == 0 || file.Index.Scenes.contains(sceneHandle))
				return invalid("a scene handle is null or listed twice");
			if (!inFile(sceneInfo.PackedOffset, sceneInfo.PackedSize))
				return invalid("a scene lies outside the file");
			for (const auto& [assetHandle, assetInfo] : sceneInfo.Assets)
			{
				if (assetHandle == 0 || !inFile(assetInfo.PackedOffset, assetInfo.PackedSize))
					return invalid("an asset is null or lies outside the file");
			}

			file.Index.Scenes.emplace(sceneHandle, std::move(sceneInfo));
		}

		LUX_CORE_TRACE_TAG("Asset Pack", "Deserialized index with {} scenes from AssetPack", sceneCount);
		return true;
	}

	uint64_t AssetPackSerializer::CalculateIndexTableSize(const AssetPackFile& file)
	{
		uint64_t appInfoSize = sizeof(uint64_t) * 2;
		uint64_t sceneMapSize = sizeof(uint32_t) + (sizeof(AssetHandle) + sizeof(uint64_t) * 2 + sizeof(uint16_t)) * file.Index.Scenes.size();
		uint64_t assetMapSize = 0;
		for (const auto& [sceneHandle, sceneInfo] : file.Index.Scenes)
			assetMapSize += sizeof(uint32_t) + (sizeof(AssetHandle) + sizeof(AssetPackFile::AssetInfo)) * sceneInfo.Assets.size();

		return appInfoSize + sceneMapSize + assetMapSize;
	}

}

