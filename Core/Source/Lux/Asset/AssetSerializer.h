#pragma once

#include "AssetMetadata.h"

namespace Lux
{
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
	};
}
