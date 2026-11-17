// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AnimationAssetSerializer.h"

#include "AssetManager.h"
#include "Lux/Animation/Animation.h"
#include "Lux/Animation/Skeleton.h"
#include "Lux/Project/Project.h"

namespace Lux
{
	namespace
	{
		template<typename T>
		bool SaveToFile(const AssetMetadata& metadata, const Ref<Asset>& asset)
		{
			const auto typed = asset.As<T>();
			if (!typed)
				return false;

			FileStreamWriter writer(Project::GetActiveAssetDirectory() / metadata.FilePath);
			typed->Serialize(&writer);
			if (!writer.IsStreamGood())
			{
				LUX_CORE_ERROR("Failed to save '{}'", metadata.FilePath.string());
				return false;
			}
			return true;
		}

		template<typename T>
		bool LoadFromFile(const AssetMetadata& metadata, Ref<Asset>& asset)
		{
			FileStreamReader reader(Project::GetActiveAssetDirectory() / metadata.FilePath);
			if (!reader.IsStreamGood())
			{
				LUX_CORE_ERROR("Cannot read '{}'", metadata.FilePath.string());
				return false;
			}
			auto typed = Ref<T>::Create();
			if (!typed->Deserialize(&reader))
			{
				LUX_CORE_ERROR("'{}' is not a valid {} file", metadata.FilePath.string(), AssetTypeToString(T::GetStaticType()));
				return false;
			}
			typed->Handle = metadata.Handle;
			asset = typed;
			return true;
		}

		template<typename T>
		bool WriteToPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo)
		{
			const auto typed = AssetManager::GetAsset<T>(handle);
			if (!typed)
				return false;
			outInfo.Offset = stream.GetStreamPosition();
			typed->Serialize(&stream);
			outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
			return stream.IsStreamGood();
		}

		template<typename T>
		Ref<Asset> ReadFromPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info)
		{
			stream.SetStreamPosition(info.PackedOffset);
			auto typed = Ref<T>::Create();
			if (!typed->Deserialize(&stream) || stream.GetStreamPosition() - info.PackedOffset != info.PackedSize)
			{
				LUX_CORE_ERROR("Invalid packed {}", AssetTypeToString(T::GetStaticType()));
				return nullptr;
			}
			return typed;
		}
	}

	void SkeletonSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const { TrySerialize(metadata, asset); }
	bool SkeletonSerializer::TrySerialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const { return SaveToFile<Skeleton>(metadata, asset); }
	bool SkeletonSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const { return LoadFromFile<Skeleton>(metadata, asset); }
	bool SkeletonSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const { return WriteToPack<Skeleton>(handle, stream, outInfo); }
	Ref<Asset> SkeletonSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info) const { return ReadFromPack<Skeleton>(stream, info); }

	void AnimationSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const { TrySerialize(metadata, asset); }
	bool AnimationSerializer::TrySerialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const { return SaveToFile<Animation>(metadata, asset); }
	bool AnimationSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const { return LoadFromFile<Animation>(metadata, asset); }
	bool AnimationSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const { return WriteToPack<Animation>(handle, stream, outInfo); }
	Ref<Asset> AnimationSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& info) const { return ReadFromPack<Animation>(stream, info); }
}
