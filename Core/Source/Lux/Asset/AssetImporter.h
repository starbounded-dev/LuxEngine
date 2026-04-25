#pragma once

#include "AssetMetadata.h"
#include "AssetSerializer.h"
#include "Lux/Scene/Scene.h"

namespace Lux
{
	class AssetImporter
	{
	public:
		static void Init();
		static void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset);
		static void Serialize(const Ref<Asset>& asset);
		static bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset);
		static void RegisterDependencies(const AssetMetadata& metadata);
		static Ref<Asset> ImportAsset(AssetHandle handle, const AssetMetadata& metadata);

		static bool SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo);
		static Ref<Asset> DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo);
		static Ref<Scene> DeserializeSceneFromAssetPack(FileStreamReader& stream, const AssetPackFile::SceneInfo& sceneInfo);
	};
}
