#include "lpch.h"
#include "AudioAssetSerializer.h"

#include "Lux/Audio/AudioFileUtils.h"
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
		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return true;
	}

	Ref<Asset> AudioAssetSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const
	{
		LUX_PROFILE_FUNCTION("AudioAssetSerializer::DeserializeFromAssetPack");

		stream.SetStreamPosition(assetInfo.PackedOffset);

		Ref<AudioFile> audioFile = Ref<AudioFile>::Create();
		stream.ReadString(audioFile->FilePath);
		return audioFile;
	}
}
