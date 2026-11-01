// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SoundSerializer.h"

#include "AssetManager.h"
#include "Lux/Audio/Sound.h"
#include "Lux/Project/Project.h"
#include <fstream>

namespace Lux
{
	namespace
	{
		// A Sound is a few hundred bytes; the cap only stops a damaged or hostile file.
		constexpr uint64_t k_MaxSoundBytes = 64 * 1024;

		bool ReadSoundFile(const AssetMetadata& metadata, std::string& outText)
		{
			std::ifstream file(Project::GetActiveAssetDirectory() / metadata.FilePath, std::ios::binary | std::ios::ate);
			if (!file || file.tellg() < 0 || static_cast<uint64_t>(file.tellg()) > k_MaxSoundBytes)
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot read sound '{}' (missing or oversized)", metadata.FilePath.string());
				return false;
			}
			outText.assign(static_cast<size_t>(file.tellg()), '\0');
			file.seekg(0);
			if (!file.read(outText.data(), outText.size()))
			{
				LUX_CORE_ERROR_TAG("Audio", "Failed reading sound '{}'", metadata.FilePath.string());
				return false;
			}
			return true;
		}

		void RegisterClips(const Sound& sound, AssetHandle handle)
		{
			AssetManager::DeregisterDependencies(handle);
			for (const AssetHandle clip : sound.GetClips())
				AssetManager::RegisterDependency(clip, handle);
		}
	}

	void SoundSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		const auto sound = asset.As<Sound>();
		std::string error;
		if (!sound || !sound->Validate(&error))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot save invalid sound '{}': {}", metadata.FilePath.string(), error);
			return;
		}
		// Same limit the loader and the asset pack enforce: a larger file would save and then fail
		// to load or export.
		const auto text = sound->ToYAML();
		if (text.size() > k_MaxSoundBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Sound '{}' exceeds the 64 KiB serialized limit; not saved", metadata.FilePath.string());
			return;
		}
		std::ofstream file(Project::GetActiveAssetDirectory() / metadata.FilePath);
		file << text;
		file.flush();
		if (!file)
			LUX_CORE_ERROR_TAG("Audio", "Failed to save sound '{}'", metadata.FilePath.string());
	}

	bool SoundSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		std::string text;
		if (!ReadSoundFile(metadata, text))
			return false;
		auto sound = Ref<Sound>::Create();
		if (!sound->FromYAML(text))
			return false;
		sound->Handle = metadata.Handle;
		RegisterClips(*sound, metadata.Handle);
		asset = sound;
		return true;
	}

	void SoundSerializer::RegisterDependencies(const AssetMetadata& metadata) const
	{
		std::string text;
		Sound sound;
		if (ReadSoundFile(metadata, text) && sound.FromYAML(text))
			RegisterClips(sound, metadata.Handle);
	}

	bool SoundSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		const auto sound = AssetManager::GetAsset<Sound>(handle);
		if (!sound || !sound->Validate())
			return false;
		const auto text = sound->ToYAML();
		if (text.size() > k_MaxSoundBytes)
			return false;
		outInfo.Offset = stream.GetStreamPosition();
		stream.WriteRaw<uint64_t>(text.size());
		stream.WriteData(text.data(), text.size());
		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return stream.IsStreamGood();
	}

	Ref<Asset> SoundSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info) const
	{
		stream.SetStreamPosition(info.PackedOffset);
		uint64_t size = 0;
		if (info.PackedSize < sizeof(size) || !stream.ReadData(reinterpret_cast<char*>(&size), sizeof(size))
			|| size > k_MaxSoundBytes || size != info.PackedSize - sizeof(size))
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid packed sound size");
			return nullptr;
		}
		std::string text(static_cast<size_t>(size), '\0');
		if (!stream.ReadData(text.data(), text.size()))
		{
			LUX_CORE_ERROR_TAG("Audio", "Truncated packed sound");
			return nullptr;
		}
		auto sound = Ref<Sound>::Create();
		return sound->FromYAML(text) ? sound : nullptr;
	}
}
