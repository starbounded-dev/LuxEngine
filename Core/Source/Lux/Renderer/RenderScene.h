#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Core/Math/Sphere.h"
#include "Lux/Renderer/GPUScene.h"
#include "Lux/Renderer/MaterialScene.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/TextureScene.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace Lux {

	struct StaticMeshRenderProxy
	{
		RenderPrimitiveID PrimitiveID = InvalidRenderPrimitiveID;
		UUID EntityID = 0;

		AssetHandle StaticMeshHandle = 0;
		AssetHandle MeshSourceHandle = 0;
		uint64_t MaterialHash = 0;

		Ref<StaticMesh> StaticMesh;
		Ref<MeshSource> MeshSource;
		Ref<MaterialTable> MaterialTable;

		glm::mat4 WorldTransform{ 1.0f };
		BoundingSphere WorldBounds;
		std::vector<GPUSceneInstanceRef> SubmeshInstances;

		bool Visible = true;
		bool Selected = false;

		RenderProxyDirtyFlags DirtyFlags = RenderProxyDirtyFlags::None;
		uint32_t LastDirtyFrame = 0;
		uint32_t LastTouchedFrame = 0;
	};

	struct RenderSceneSyncStats
	{
		uint32_t StaticMeshProxyCount = 0;
		uint32_t VisibleStaticMeshProxyCount = 0;
		uint32_t CreatedProxies = 0;
		uint32_t RemovedProxies = 0;
		uint32_t UpdatedProxies = 0;
		uint32_t TransformDirtyProxies = 0;
		uint32_t MeshDirtyProxies = 0;
		uint32_t MaterialDirtyProxies = 0;
		uint32_t VisibilityDirtyProxies = 0;
		uint32_t SelectionDirtyProxies = 0;
		uint32_t GPUSceneDirtyInstances = 0;
		uint32_t MaterialSceneDirtyMaterials = 0;
		uint32_t TextureSceneDirtyTextures = 0;
	};

	class RenderScene : public RefCounted
	{
	public:
		void BeginSync();
		RenderPrimitiveID UpsertStaticMesh(StaticMeshRenderProxy proxy);
		void EndSync();
		void Clear();

		const std::vector<const StaticMeshRenderProxy*>& GetStaticMeshProxies() const { return m_StaticMeshProxies; }
		const StaticMeshRenderProxy* FindStaticMeshProxy(UUID entityID) const;
		const GPUScene& GetGPUScene() const { return m_GPUScene; }
		GPUScene& GetGPUScene() { return m_GPUScene; }
		const MaterialScene& GetMaterialScene() const { return m_MaterialScene; }
		MaterialScene& GetMaterialScene() { return m_MaterialScene; }
		const TextureScene& GetTextureScene() const { return m_TextureScene; }
		TextureScene& GetTextureScene() { return m_TextureScene; }
		const RenderSceneSyncStats& GetLastSyncStats() const { return m_LastSyncStats; }
		size_t GetStaticMeshProxyCount() const { return m_StaticMeshProxiesByEntity.size(); }
		size_t GetVisibleStaticMeshProxyCount() const { return m_StaticMeshProxies.size(); }
		uint32_t GetFrameIndex() const { return m_FrameIndex; }

	private:
		RenderPrimitiveID GetOrCreatePrimitiveID(UUID entityID);
		RenderMaterialID ResolveRenderMaterialID(const StaticMeshRenderProxy& proxy, uint32_t submeshIndex);
		void RebuildStaticMeshGPUInstances(StaticMeshRenderProxy& proxy, bool forceDirty, const glm::mat4& previousWorldTransform);
		void RefreshStaticMeshGPUInstances(StaticMeshRenderProxy& proxy);
		void TouchStaticMeshMaterials(StaticMeshRenderProxy& proxy);

	private:
		uint32_t m_FrameIndex = 0;
		RenderPrimitiveID m_NextPrimitiveID = 1;

		GPUScene m_GPUScene;
		TextureScene m_TextureScene;
		MaterialScene m_MaterialScene;
		RenderSceneSyncStats m_LastSyncStats;

		std::unordered_map<UUID, RenderPrimitiveID> m_PrimitiveIDsByEntity;
		std::unordered_map<UUID, StaticMeshRenderProxy> m_StaticMeshProxiesByEntity;

		std::vector<UUID> m_FrameStaticMeshProxyOrder;
		std::vector<const StaticMeshRenderProxy*> m_StaticMeshProxies;
	};

}
