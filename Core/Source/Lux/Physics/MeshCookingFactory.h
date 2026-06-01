#pragma once

#include "Lux/Core/Buffer.h"
#include "Lux/Physics/MeshColliderAsset.h"
#include "Lux/Renderer/Mesh.h"

namespace Lux {

	struct SubmeshColliderData
	{
		Buffer ColliderData;
		glm::mat4 Transform = glm::mat4(1.0f);
	};

	struct MeshColliderData
	{
		std::vector<SubmeshColliderData> Submeshes;
		MeshColliderType Type = MeshColliderType::None;
	};

	struct CachedColliderData
	{
		MeshColliderData SimpleColliderData;
		MeshColliderData ComplexColliderData;
	};

	class MeshCookingFactory : public RefCounted
	{
	public:
		virtual ~MeshCookingFactory() = default;

		virtual void Init() {}
		virtual void Shutdown() {}
		virtual std::pair<ECookingResult, ECookingResult> CookMesh(Ref<MeshColliderAsset> colliderAsset, bool invalidateOld = false);
		std::pair<ECookingResult, ECookingResult> CookMesh(AssetHandle colliderHandle, bool invalidateOld = false);
	};

}
