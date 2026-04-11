#pragma once

#include "AssetSerializer.h"

namespace Lux
{
	// Serializes / deserializes MeshSource assets.
	// Editor path  – loads via AssimpMeshImporter (Assimp).
	// Serialize    – no-op; source files are the canonical representation.
	class MeshSourceSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		virtual bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const override;
	};

	// Serializes / deserializes Mesh assets (YAML – stores MeshSource handle + submesh list).
	class MeshSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		virtual bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const override;
	};

	// Serializes / deserializes StaticMesh assets (YAML – stores MeshSource handle + submesh list).
	class StaticMeshSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const override;
		virtual bool TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const override;
	};
}
