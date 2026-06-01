#include "lpch.h"
#include "MeshCookingFactory.h"

#include "Lux/Asset/AssetManager.h"

namespace Lux {

	std::pair<ECookingResult, ECookingResult> MeshCookingFactory::CookMesh(Ref<MeshColliderAsset> colliderAsset, bool)
	{
		if (!colliderAsset || !colliderAsset->Mesh)
			return { ECookingResult::Failed, ECookingResult::Failed };

		return { ECookingResult::Success, ECookingResult::Success };
	}

	std::pair<ECookingResult, ECookingResult> MeshCookingFactory::CookMesh(AssetHandle colliderHandle, bool invalidateOld)
	{
		return CookMesh(AssetManager::GetAsset<MeshColliderAsset>(colliderHandle), invalidateOld);
	}

}
