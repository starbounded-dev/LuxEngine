#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Physics/PhysicsTypes.h"

namespace Lux {

	class MeshColliderAsset : public Asset
	{
	public:
		MeshColliderAsset() = default;
		explicit MeshColliderAsset(AssetHandle mesh)
			: Mesh(mesh) {}

		AssetHandle Mesh = 0;
		ECollisionComplexity CollisionComplexity = ECollisionComplexity::Default;

		static AssetType GetStaticType() { return AssetType::MeshCollider; }
		virtual AssetType GetAssetType() const override { return GetStaticType(); }
	};

}
