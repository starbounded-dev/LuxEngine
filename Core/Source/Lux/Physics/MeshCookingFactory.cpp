// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

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
