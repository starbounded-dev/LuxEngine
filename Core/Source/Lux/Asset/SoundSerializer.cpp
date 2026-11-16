// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SoundSerializer.h"

#include "AssetManager.h"
#include "Lux/Audio/Sound.h"
#include "Lux/Audio/SoundBank.h"
#include "Lux/Project/Project.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/StringUtils.h"
#include <fstream>

namespace Lux
{
	namespace
	{
		// Sounds and banks are both small bounded YAML assets; the cap only stops a damaged or hostile
		// file. Kind names the asset in log messages.
		template<typename T> constexpr const char* Kind = "sound";
		template<> constexpr const char* Kind<SoundBank> = "sound bank";

		template<typename T>
		bool ReadFile(const AssetMetadata& metadata, std::string& outText)
		{
			std::ifstream file(Project::GetActiveAssetDirectory() / metadata.FilePath, std::ios::binary | std::ios::ate);
			if (!file || file.tellg() < 0 || static_cast<uint64_t>(file.tellg()) > T::MaxFileBytes)
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot read {} '{}' (missing or oversized)", Kind<T>, metadata.FilePath.string());
				return false;
			}
			outText.assign(static_cast<size_t>(file.tellg()), '\0');
			file.seekg(0);
			if (!file.read(outText.data(), outText.size()))
			{
				LUX_CORE_ERROR_TAG("Audio", "Failed reading {} '{}'", Kind<T>, metadata.FilePath.string());
				return false;
			}
			return true;
		}

		// A Sound depends on its clips, a bank on its Sounds.
		void RegisterDependencies(const Sound& sound, AssetHandle handle)
		{
			AssetManager::DeregisterDependencies(handle);
			for (const AssetHandle clip : sound.GetClips())
				AssetManager::RegisterDependency(clip, handle);
		}

		void RegisterDependencies(const SoundBank& bank, AssetHandle handle)
		{
			AssetManager::DeregisterDependencies(handle);
			for (const AssetHandle sound : bank.Sounds)
				AssetManager::RegisterDependency(sound, handle);
		}

		template <typename T> bool Save(const AssetMetadata& metadata, const Ref<Asset>& asset)
		{
			const auto value = asset.As<T>();
			const auto path = Utils::PathToUTF8(metadata.FilePath);
			std::string error;
			if (!value || !value->Validate(&error))
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot save invalid {} '{}': {}", Kind<T>, path, error);
				return false;
			}
			// Same limit the loader and the asset pack enforce: a larger file would save and then fail
			// to load or export.
			const auto text = value->ToYAML();
			if (text.size() > T::MaxFileBytes)
			{
				LUX_CORE_ERROR_TAG("Audio", "The {} '{}' exceeds the 64 KiB serialized limit; not saved",
								   Kind<T>, path);
				return false;
			}
			// The byte cap applies to the file too: Windows text newline expansion must not
			// make an accepted YAML snapshot larger than the loader's bound.
			if (!FileSystem::WriteFileAtomically(Project::GetActiveAssetDirectory() / metadata.FilePath, text,
												 true))
			{
				LUX_CORE_ERROR_TAG("Audio", "Failed to save {} '{}'", Kind<T>, path);
				return false;
			}
			// An edit may have added or removed what it depends on.
			RegisterDependencies(*value, metadata.Handle);
			return true;
		}

		template<typename T>
		bool Load(const AssetMetadata& metadata, Ref<Asset>& asset)
		{
			std::string text;
			if (!ReadFile<T>(metadata, text))
				return false;
			auto value = Ref<T>::Create();
			if (!value->FromYAML(text))
				return false;
			value->Handle = metadata.Handle;
			RegisterDependencies(*value, metadata.Handle);
			asset = value;
			return true;
		}

		template<typename T>
		void LoadDependencies(const AssetMetadata& metadata)
		{
			std::string text;
			T value;
			if (ReadFile<T>(metadata, text) && value.FromYAML(text))
				RegisterDependencies(value, metadata.Handle);
		}

		template<typename T>
		bool WritePacked(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo)
		{
			const auto value = AssetManager::GetAsset<T>(handle);
			if (!value || !value->Validate())
				return false;
			const auto text = value->ToYAML();
			if (text.size() > T::MaxFileBytes)
				return false;
			outInfo.Offset = stream.GetStreamPosition();
			stream.WriteRaw<uint64_t>(text.size());
			stream.WriteData(text.data(), text.size());
			outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
			return stream.IsStreamGood();
		}

		template<typename T>
		Ref<Asset> ReadPacked(FileStreamReader& stream, const AssetPackFile::AssetInfo& info)
		{
			stream.SetStreamPosition(info.PackedOffset);
			uint64_t size = 0;
			if (info.PackedSize < sizeof(size) || !stream.ReadData(reinterpret_cast<char*>(&size), sizeof(size))
				|| size > T::MaxFileBytes || size != info.PackedSize - sizeof(size))
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid packed {} size", Kind<T>);
				return nullptr;
			}
			std::string text(static_cast<size_t>(size), '\0');
			if (!stream.ReadData(text.data(), text.size()))
			{
				LUX_CORE_ERROR_TAG("Audio", "Truncated packed {}", Kind<T>);
				return nullptr;
			}
			auto value = Ref<T>::Create();
			return value->FromYAML(text) ? value : nullptr;
		}
	}

	void SoundSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const { Save<Sound>(metadata, asset); }
	bool SoundSerializer::TrySerialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		return Save<Sound>(metadata, asset);
	}
	bool SoundSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const { return Load<Sound>(metadata, asset); }
	void SoundSerializer::RegisterDependencies(const AssetMetadata& metadata) const { LoadDependencies<Sound>(metadata); }
	bool SoundSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const { return WritePacked<Sound>(handle, stream, outInfo); }
	Ref<Asset> SoundSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info) const { return ReadPacked<Sound>(stream, info); }

	void SoundBankSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const { Save<SoundBank>(metadata, asset); }
	bool SoundBankSerializer::TrySerialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		return Save<SoundBank>(metadata, asset);
	}
	bool SoundBankSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const { return Load<SoundBank>(metadata, asset); }
	void SoundBankSerializer::RegisterDependencies(const AssetMetadata& metadata) const { LoadDependencies<SoundBank>(metadata); }
	bool SoundBankSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const { return WritePacked<SoundBank>(handle, stream, outInfo); }
	Ref<Asset> SoundBankSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info) const { return ReadPacked<SoundBank>(stream, info); }
}
