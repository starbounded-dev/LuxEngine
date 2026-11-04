// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioAssetSerializer.h"

#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioFileUtils.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Project/Project.h"

namespace Lux
{
	void AudioAssetSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		LUX_PROFILE_FUNCTION("AudioAssetSerializer::Serialize");
		(void)metadata;
		(void)asset;
	}

	bool AudioAssetSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		LUX_PROFILE_FUNCTION("AudioAssetSerializer::TryLoadData");

		Ref<AudioFile> audioFile;
		if (auto info = AudioFileUtils::GetFileInfo(metadata))
			audioFile = Ref<AudioFile>::Create(info->Duration, info->SamplingRate, info->BitDepth, info->NumChannels, info->FileSize);
		else
			audioFile = Ref<AudioFile>::Create();

		audioFile->Handle = metadata.Handle;
		audioFile->FilePath = metadata.FilePath.generic_string();
		asset = audioFile;
		return true;
	}

	bool AudioAssetSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		LUX_PROFILE_FUNCTION("AudioAssetSerializer::SerializeToAssetPack");

		if (!Project::GetEditorAssetManager())
			return false;

		const AssetMetadata& metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
		if (!metadata.IsValid())
			return false;

		outInfo.Offset = stream.GetStreamPosition();
		stream.WriteString(metadata.FilePath.generic_string());
		// The audio itself ships: the encoded file follows its path. Packs from FMOD builds kept only the
		// path; the reader takes data only when the entry is longer, so they still load.
		const auto file = Project::GetEditorAssetManager()->GetFileSystemPath(metadata);
		Buffer bytes = FileSystem::ReadBytes(file);
		if (!bytes.Data || bytes.Size == 0)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot pack audio '{}': the file cannot be read", file.string());
			return false;
		}
		stream.WriteRaw<uint64_t>(bytes.Size);
		stream.WriteData(reinterpret_cast<const char*>(bytes.Data), bytes.Size);
		bytes.Release();
		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return true;
	}

	Ref<Asset> AudioAssetSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const
	{
		LUX_PROFILE_FUNCTION("AudioAssetSerializer::DeserializeFromAssetPack");

		stream.SetStreamPosition(assetInfo.PackedOffset);

		Ref<AudioFile> audioFile = Ref<AudioFile>::Create();
		stream.ReadString(audioFile->FilePath);
		const uint64_t consumed = stream.GetStreamPosition() - assetInfo.PackedOffset;
		if (consumed + sizeof(uint64_t) <= assetInfo.PackedSize)
		{
			uint64_t size = 0;
			stream.ReadRaw<uint64_t>(size);
			if (size == 0 || consumed + sizeof(uint64_t) + size > assetInfo.PackedSize)
			{
				LUX_CORE_ERROR_TAG("Audio", "Audio asset {} in the pack is truncated", static_cast<uint64_t>(assetInfo.PackedOffset));
				return nullptr;
			}
			// Left in the pack: the native backend reads it there (NativePackVFS.cpp).
			audioFile->PackFile = stream.GetPath();
			audioFile->PackOffset = stream.GetStreamPosition();
			audioFile->FileSize = size;
		}
		return audioFile;
	}
}
