// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SpriteSheetSerializer.h"

#include "AssetManager.h"
#include "AssetReference.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/SpriteSheet.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/StringUtils.h"

#include <fstream>

namespace Lux
{
	namespace
	{
		bool ReadFile(const AssetMetadata& metadata, std::string& outText)
		{
			std::ifstream file(Project::GetActiveAssetDirectory() / metadata.FilePath, std::ios::binary | std::ios::ate);
			if (!file || file.tellg() < 0 || static_cast<uint64_t>(file.tellg()) > SpriteSheet::MaxFileBytes)
			{
				LUX_CORE_ERROR_TAG("AssetManager", "Cannot read sprite sheet '{}' (missing or larger than 4 MiB)", metadata.FilePath.string());
				return false;
			}
			outText.assign(static_cast<size_t>(file.tellg()), '\0');
			file.seekg(0);
			if (!file.read(outText.data(), outText.size()))
			{
				LUX_CORE_ERROR_TAG("AssetManager", "Failed reading sprite sheet '{}'", metadata.FilePath.string());
				return false;
			}
			return true;
		}

		// A sheet depends on its texture.
		void RegisterSheetDependencies(const SpriteSheet& sheet, AssetHandle handle)
		{
			AssetManager::DeregisterDependencies(handle);
			if (sheet.Texture)
				AssetManager::RegisterDependency(sheet.Texture, handle);
		}
	}

	bool SpriteSheetSerializer::TrySerialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		const auto sheet = asset.As<SpriteSheet>();
		const auto path = Utils::PathToUTF8(metadata.FilePath);
		std::string error;
		if (!sheet || !sheet->Validate(&error))
		{
			LUX_CORE_ERROR_TAG("AssetManager", "Cannot save invalid sprite sheet '{}': {}", path, error);
			return false;
		}

		// The file stores asset paths; the packed copy below stays numeric.
		AssetReference::PathModeScope pathMode;
		const std::string text = sheet->ToYAML();
		if (text.size() > SpriteSheet::MaxFileBytes)
		{
			LUX_CORE_ERROR_TAG("AssetManager", "The sprite sheet '{}' exceeds the 4 MiB serialized limit; not saved", path);
			return false;
		}
		if (!FileSystem::WriteFileAtomically(Project::GetActiveAssetDirectory() / metadata.FilePath, text, true))
		{
			LUX_CORE_ERROR_TAG("AssetManager", "Failed to save sprite sheet '{}'", path);
			return false;
		}
		RegisterSheetDependencies(*sheet, metadata.Handle);
		return true;
	}

	void SpriteSheetSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		TrySerialize(metadata, asset);
	}

	bool SpriteSheetSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		std::string text;
		if (!ReadFile(metadata, text))
			return false;
		auto sheet = Ref<SpriteSheet>::Create();
		if (!sheet->FromYAML(text))
			return false;
		sheet->Handle = metadata.Handle;
		RegisterSheetDependencies(*sheet, metadata.Handle);
		asset = sheet;
		return true;
	}

	void SpriteSheetSerializer::RegisterDependencies(const AssetMetadata& metadata) const
	{
		std::string text;
		SpriteSheet sheet;
		if (ReadFile(metadata, text) && sheet.FromYAML(text))
			RegisterSheetDependencies(sheet, metadata.Handle);
	}

	bool SpriteSheetSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		const auto sheet = AssetManager::GetAsset<SpriteSheet>(handle);
		if (!sheet || !sheet->Validate())
			return false;
		const std::string text = sheet->ToYAML();
		if (text.size() > SpriteSheet::MaxFileBytes)
			return false;
		outInfo.Offset = stream.GetStreamPosition();
		stream.WriteRaw<uint64_t>(text.size());
		stream.WriteData(text.data(), text.size());
		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return stream.IsStreamGood();
	}

	Ref<Asset> SpriteSheetSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info) const
	{
		stream.SetStreamPosition(info.PackedOffset);
		uint64_t size = 0;
		if (info.PackedSize < sizeof(size) || !stream.ReadData(reinterpret_cast<char*>(&size), sizeof(size))
			|| size > SpriteSheet::MaxFileBytes || size != info.PackedSize - sizeof(size))
		{
			LUX_CORE_ERROR_TAG("AssetManager", "Invalid packed sprite sheet size");
			return nullptr;
		}
		std::string text(static_cast<size_t>(size), '\0');
		if (!stream.ReadData(text.data(), text.size()))
		{
			LUX_CORE_ERROR_TAG("AssetManager", "Truncated packed sprite sheet");
			return nullptr;
		}
		auto sheet = Ref<SpriteSheet>::Create();
		return sheet->FromYAML(text) ? sheet : nullptr;
	}
}
