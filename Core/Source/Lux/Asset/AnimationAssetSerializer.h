// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include "AssetSerializer.h"

namespace Lux
{
	// .lskel: the "LZSK" blob (Skeleton::Serialize), the same bytes in asset packs.
	class SkeletonSerializer : public AssetSerializer
	{
	public:
		void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		bool TrySerialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const override;
		bool SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const override;
		Ref<Asset> DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const override;
	};

	// .lanim: the "LZAN" blob (Animation::Serialize), the same bytes in asset packs.
	class AnimationSerializer : public AssetSerializer
	{
	public:
		void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		bool TrySerialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const override;
		bool SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const override;
		Ref<Asset> DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const override;
	};
}
