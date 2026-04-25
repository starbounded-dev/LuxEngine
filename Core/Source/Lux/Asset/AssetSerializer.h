#pragma once

#include "AssetMetadata.h"

#include "Lux/Serialization/AssetPackFile.h"
#include "Lux/Serialization/FileStream.h"

namespace Lux
{
	struct AssetSerializationInfo
	{
		uint64_t Offset = 0;
		uint64_t Size = 0;
	};

	// Abstract interface that every asset type's serializer must satisfy.
	// Editor serializers load data from disk (e.g. via YAML / Assimp).
	// Runtime serializers read from packed binary streams produced at build time.
	class AssetSerializer
	{
	public:
		virtual ~AssetSerializer() = default;

		// Serialize the asset back to its source representation (YAML, binary, etc.)
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const = 0;

		// Attempt to load the asset described by metadata.
		// Returns true and sets asset on success; returns false on failure.
		virtual bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const = 0;

		virtual void RegisterDependencies(const AssetMetadata& metadata) const {}
		virtual bool SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const { return false; }
		virtual Ref<Asset> DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const { return nullptr; }
	};
}
