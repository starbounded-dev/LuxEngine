#pragma once

#include "Lux/Physics/MeshCookingFactory.h"

#include <unordered_map>

namespace Lux {

	class MeshColliderCache
	{
	public:
		void Init();
		const CachedColliderData& GetMeshData(const Ref<MeshColliderAsset>& colliderAsset);
		Ref<StaticMesh> GetDebugStaticMesh(const Ref<MeshColliderAsset>& colliderAsset);
		AssetHandle GetBoxDebugMesh() const { return m_BoxMesh; }
		AssetHandle GetSphereDebugMesh() const { return m_SphereMesh; }
		AssetHandle GetCapsuleDebugMesh() const { return m_CapsuleMesh; }
		bool Exists(const Ref<MeshColliderAsset>& colliderAsset) const;
		void Rebuild();
		void Clear();

	private:
		std::unordered_map<AssetHandle, CachedColliderData> m_MeshData;
		AssetHandle m_BoxMesh = 0;
		AssetHandle m_SphereMesh = 0;
		AssetHandle m_CapsuleMesh = 0;
		inline static CachedColliderData s_EmptyData;
	};

}
