// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include "AssetSerializer.h"

namespace Lux
{
	// .lsound YAML, and the same YAML in asset packs. Bounded at save, load, pack write and pack read.
	class SoundSerializer : public AssetSerializer
	{
	public:
		void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const override;
		void RegisterDependencies(const AssetMetadata& metadata) const override;
		bool SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const override;
		Ref<Asset> DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const override;
	};
}
