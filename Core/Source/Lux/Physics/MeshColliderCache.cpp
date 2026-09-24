// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MeshColliderCache.h"

#include "Lux/Asset/AssetManager.h"

namespace Lux {

	void MeshColliderCache::Init()
	{
	}

	const CachedColliderData& MeshColliderCache::GetMeshData(const Ref<MeshColliderAsset>& colliderAsset)
	{
		if (!colliderAsset)
			return s_EmptyData;

		auto [it, inserted] = m_MeshData.try_emplace(colliderAsset->Handle);
		return it->second;
	}

	Ref<StaticMesh> MeshColliderCache::GetDebugStaticMesh(const Ref<MeshColliderAsset>& colliderAsset)
	{
		return colliderAsset ? StaticMesh::GetOrCreateRuntime(colliderAsset->Mesh) : nullptr;
	}

	bool MeshColliderCache::Exists(const Ref<MeshColliderAsset>& colliderAsset) const
	{
		return colliderAsset && m_MeshData.contains(colliderAsset->Handle);
	}

	void MeshColliderCache::Rebuild()
	{
		m_MeshData.clear();
	}

	void MeshColliderCache::Clear()
	{
		m_MeshData.clear();
	}

}
