// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "DialogueTableSerializer.h"

#include "AssetManager.h"
#include "Lux/Audio/DialogueTable.h"
#include "Lux/Project/Project.h"
#include <fstream>

namespace Lux
{
	namespace
	{
		constexpr uint64_t k_MaxTableBytes = 8 * 1024 * 1024;
	}

	void DialogueTableSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		const auto table = asset.As<DialogueTable>();
		if (!table || !table->Validate())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot save invalid dialogue table '{}'", metadata.FilePath.string());
			return;
		}
		const auto text = table->ToYAML();
		if (text.size() > k_MaxTableBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Dialogue table '{}' exceeds the 8 MiB serialized limit", metadata.FilePath.string());
			return;
		}
		std::ofstream file(Project::GetActiveAssetDirectory() / metadata.FilePath);
		file << text;
		file.flush();
		if (!file)
			LUX_CORE_ERROR_TAG("Audio", "Failed to save dialogue table '{}'", metadata.FilePath.string());
	}

	bool DialogueTableSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		std::ifstream file(Project::GetActiveAssetDirectory() / metadata.FilePath, std::ios::binary | std::ios::ate);
		if (!file || file.tellg() < 0 || static_cast<uint64_t>(file.tellg()) > k_MaxTableBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot read dialogue table '{}' (missing or oversized)", metadata.FilePath.string());
			return false;
		}
		std::string text(static_cast<size_t>(file.tellg()), '\0');
		file.seekg(0);
		if (!file.read(text.data(), text.size()))
		{
			LUX_CORE_ERROR_TAG("Audio", "Failed reading dialogue table '{}'", metadata.FilePath.string());
			return false;
		}
		auto table = Ref<DialogueTable>::Create();
		if (!table->FromYAML(text))
			return false;
		table->Handle = metadata.Handle;
		asset = table;
		return true;
	}

	bool DialogueTableSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		const auto table = AssetManager::GetAsset<DialogueTable>(handle);
		if (!table || !table->Validate())
			return false;
		const auto text = table->ToYAML();
		if (text.size() > k_MaxTableBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Dialogue table {} exceeds the 8 MiB packed limit", static_cast<uint64_t>(handle));
			return false;
		}
		outInfo.Offset = stream.GetStreamPosition();
		stream.WriteRaw<uint64_t>(text.size());
		stream.WriteData(text.data(), text.size());
		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return stream.IsStreamGood();
	}

	Ref<Asset> DialogueTableSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info) const
	{
		stream.SetStreamPosition(info.PackedOffset);
		uint64_t size = 0;
		if (info.PackedSize < sizeof(size) || !stream.ReadData(reinterpret_cast<char*>(&size), sizeof(size))
			|| size > k_MaxTableBytes || size != info.PackedSize - sizeof(size))
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid packed dialogue table size");
			return nullptr;
		}
		std::string text(static_cast<size_t>(size), '\0');
		if (!stream.ReadData(text.data(), text.size()))
		{
			LUX_CORE_ERROR_TAG("Audio", "Truncated packed dialogue table");
			return nullptr;
		}
		auto table = Ref<DialogueTable>::Create();
		return table->FromYAML(text) ? table : nullptr;
	}
}
