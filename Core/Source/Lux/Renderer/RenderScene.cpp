#include "lpch.h"

#include "Lux/Renderer/RenderScene.h"

#include "Lux/Asset/AssetManager.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace Lux {

	namespace
	{
		bool MatrixEquals(const glm::mat4& lhs, const glm::mat4& rhs)
		{
			return std::memcmp(&lhs, &rhs, sizeof(glm::mat4)) == 0;
		}

		bool BoundsEquals(const BoundingSphere& lhs, const BoundingSphere& rhs)
		{
			return std::memcmp(&lhs.Center, &rhs.Center, sizeof(glm::vec3)) == 0
				&& lhs.Radius == rhs.Radius;
		}

		uint64_t HashCombine(uint64_t seed, uint64_t value)
		{
			return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
		}

		uint64_t HashMaterialTable(const Ref<MaterialTable>& materialTable)
		{
			if (!materialTable)
				return 0;

			uint64_t hash = 1469598103934665603ull;
			hash = HashCombine(hash, materialTable->GetMaterialCount());

			for (const auto& [materialIndex, materialHandle] : materialTable->GetMaterials())
			{
				hash = HashCombine(hash, materialIndex);
				hash = HashCombine(hash, (uint64_t)materialHandle);
			}

			return hash;
		}

		glm::vec4 CalculateWorldBoundsSphere(const AABB& localBounds, const glm::mat4& worldTransform)
		{
			const BoundingSphere localSphere = localBounds.ToBoundingSphere();
			const BoundingSphere worldSphere = BoundingSphere::Transform(localSphere, worldTransform);
			return { worldSphere.Center, worldSphere.Radius };
		}

		GPUSceneInstanceFlags BuildGPUSceneInstanceFlags(const StaticMeshRenderProxy& proxy)
		{
			GPUSceneInstanceFlags flags = GPUSceneInstanceFlags::None;
			if (proxy.Visible)
				flags |= GPUSceneInstanceFlags::Visible;
			if (proxy.Selected)
				flags |= GPUSceneInstanceFlags::Selected;

			return flags;
		}

		void WriteTransformRows(glm::vec4 (&rows)[3], const glm::mat4& transform)
		{
			rows[0] = { transform[0][0], transform[1][0], transform[2][0], transform[3][0] };
			rows[1] = { transform[0][1], transform[1][1], transform[2][1], transform[3][1] };
			rows[2] = { transform[0][2], transform[1][2], transform[2][2], transform[3][2] };
		}

		glm::uvec4 BuildGPUSceneObjectData(UUID entityID)
		{
			const uint64_t value = (uint64_t)entityID;
			return glm::uvec4((uint32_t)value, (uint32_t)(value >> 32), 0, 0);
		}

		void AccumulateDirtyStats(RenderSceneSyncStats& stats, RenderProxyDirtyFlags dirtyFlags)
		{
			if (dirtyFlags == RenderProxyDirtyFlags::None)
				return;

			stats.UpdatedProxies++;
			if (HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Created))
				stats.CreatedProxies++;
			if (HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Transform))
				stats.TransformDirtyProxies++;
			if (HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Mesh))
				stats.MeshDirtyProxies++;
			if (HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Material))
				stats.MaterialDirtyProxies++;
			if (HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Visibility))
				stats.VisibilityDirtyProxies++;
			if (HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Selection))
				stats.SelectionDirtyProxies++;
		}
	}

	void RenderScene::BeginSync()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_FrameIndex++;
		if (m_FrameIndex == 0)
		{
			m_FrameIndex = 1;
			for (auto& [entityID, proxy] : m_StaticMeshProxiesByEntity)
				proxy.LastTouchedFrame = 0;
		}

		m_LastSyncStats = {};
		m_FrameStaticMeshProxyOrder.clear();
		m_GPUScene.BeginSync(m_FrameIndex);
		m_TextureScene.BeginSync(m_FrameIndex);
		m_MaterialScene.SetTextureResolver([this](AssetHandle textureHandle) { return m_TextureScene.UpsertTexture(textureHandle); });
		m_MaterialScene.BeginSync(m_FrameIndex);
	}

	RenderPrimitiveID RenderScene::GetOrCreatePrimitiveID(UUID entityID)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if ((uint64_t)entityID == 0)
			return InvalidRenderPrimitiveID;

		auto [it, inserted] = m_PrimitiveIDsByEntity.try_emplace(entityID, InvalidRenderPrimitiveID);
		if (inserted || it->second == InvalidRenderPrimitiveID)
			it->second = m_NextPrimitiveID++;

		return it->second;
	}

	RenderMaterialID RenderScene::ResolveRenderMaterialID(const StaticMeshRenderProxy& proxy, uint32_t submeshIndex)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!proxy.MeshSource)
			return InvalidRenderMaterialID;

		const auto& submeshData = proxy.MeshSource->GetSubmeshes();
		if (submeshIndex >= submeshData.size())
			return InvalidRenderMaterialID;

		const uint32_t materialIndex = submeshData[submeshIndex].MaterialIndex;
		AssetHandle materialHandle = 0;

		if (proxy.MaterialTable)
		{
			if (proxy.MaterialTable->HasMaterial(materialIndex))
				materialHandle = proxy.MaterialTable->GetMaterial(materialIndex);
			else if (proxy.MaterialTable->GetMaterials().size() == 1 && proxy.MaterialTable->HasMaterial(0))
				materialHandle = proxy.MaterialTable->GetMaterial(0);
		}

		if (!materialHandle)
		{
			Ref<MaterialTable> staticMeshMaterials = proxy.StaticMesh ? proxy.StaticMesh->GetMaterials() : nullptr;
			if (staticMeshMaterials && staticMeshMaterials->HasMaterial(materialIndex))
				materialHandle = staticMeshMaterials->GetMaterial(materialIndex);
		}

		if (!materialHandle && materialIndex < proxy.MeshSource->GetMaterials().size())
			materialHandle = proxy.MeshSource->GetMaterials()[materialIndex];

		return m_MaterialScene.UpsertMaterial(materialHandle);
	}

	RenderPrimitiveID RenderScene::UpsertStaticMesh(StaticMeshRenderProxy proxy)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if ((uint64_t)proxy.EntityID == 0)
			return InvalidRenderPrimitiveID;

		proxy.PrimitiveID = GetOrCreatePrimitiveID(proxy.EntityID);
		proxy.LastTouchedFrame = m_FrameIndex;
		proxy.MaterialHash = HashMaterialTable(proxy.MaterialTable);

		const UUID entityID = proxy.EntityID;
		const auto existingIt = m_StaticMeshProxiesByEntity.find(entityID);
		const bool created = existingIt == m_StaticMeshProxiesByEntity.end();
		glm::mat4 previousWorldTransform = proxy.WorldTransform;

		RenderProxyDirtyFlags dirtyFlags = created ? RenderProxyDirtyFlags::Created : RenderProxyDirtyFlags::None;
		if (!created)
		{
			const StaticMeshRenderProxy& previousProxy = existingIt->second;
			previousWorldTransform = previousProxy.WorldTransform;
			if (previousProxy.StaticMeshHandle != proxy.StaticMeshHandle
				|| previousProxy.MeshSourceHandle != proxy.MeshSourceHandle
				|| previousProxy.StaticMesh.Raw() != proxy.StaticMesh.Raw()
				|| previousProxy.MeshSource.Raw() != proxy.MeshSource.Raw())
			{
				dirtyFlags |= RenderProxyDirtyFlags::Mesh;
			}

			if (previousProxy.MaterialHash != proxy.MaterialHash)
				dirtyFlags |= RenderProxyDirtyFlags::Material;

			if (!MatrixEquals(previousProxy.WorldTransform, proxy.WorldTransform) || !BoundsEquals(previousProxy.WorldBounds, proxy.WorldBounds))
				dirtyFlags |= RenderProxyDirtyFlags::Transform;

			if (previousProxy.Visible != proxy.Visible)
				dirtyFlags |= RenderProxyDirtyFlags::Visibility;

			if (previousProxy.Selected != proxy.Selected)
				dirtyFlags |= RenderProxyDirtyFlags::Selection;
		}

		const bool gpuInstanceTopologyDirty = created
			|| HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Mesh)
			|| HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Transform);

		if (!created && HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Mesh))
			m_GPUScene.RemovePrimitive(proxy.PrimitiveID);

		if (gpuInstanceTopologyDirty)
		{
			if (HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Mesh))
				previousWorldTransform = proxy.WorldTransform;

			RebuildStaticMeshGPUInstances(proxy, true, previousWorldTransform);
		}
		else if (!created)
		{
			proxy.SubmeshInstances = existingIt->second.SubmeshInstances;
			RefreshStaticMeshGPUInstances(proxy);
		}

		proxy.DirtyFlags = dirtyFlags;
		proxy.LastDirtyFrame = dirtyFlags != RenderProxyDirtyFlags::None
			? m_FrameIndex
			: (created ? m_FrameIndex : existingIt->second.LastDirtyFrame);

		AccumulateDirtyStats(m_LastSyncStats, dirtyFlags);

		auto [it, inserted] = m_StaticMeshProxiesByEntity.insert_or_assign(entityID, std::move(proxy));
		m_FrameStaticMeshProxyOrder.push_back(entityID);
		return it->second.PrimitiveID;
	}

	void RenderScene::EndSync()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		for (auto it = m_StaticMeshProxiesByEntity.begin(); it != m_StaticMeshProxiesByEntity.end();)
		{
			if (it->second.LastTouchedFrame != m_FrameIndex)
			{
				m_GPUScene.RemovePrimitive(it->second.PrimitiveID);
				m_PrimitiveIDsByEntity.erase(it->first);
				it = m_StaticMeshProxiesByEntity.erase(it);
				m_LastSyncStats.RemovedProxies++;
			}
			else
			{
				++it;
			}
		}

		m_StaticMeshProxies.clear();
		m_StaticMeshProxies.reserve(m_FrameStaticMeshProxyOrder.size());

		for (UUID entityID : m_FrameStaticMeshProxyOrder)
		{
			auto it = m_StaticMeshProxiesByEntity.find(entityID);
			if (it == m_StaticMeshProxiesByEntity.end())
				continue;

			const StaticMeshRenderProxy& proxy = it->second;
			if (proxy.LastTouchedFrame != m_FrameIndex || !proxy.Visible || !proxy.StaticMesh || !proxy.MeshSource)
				continue;

			m_StaticMeshProxies.push_back(&proxy);
		}

		m_GPUScene.EndSync();
		m_MaterialScene.EndSync();
		m_TextureScene.EndSync();
		m_LastSyncStats.StaticMeshProxyCount = (uint32_t)m_StaticMeshProxiesByEntity.size();
		m_LastSyncStats.VisibleStaticMeshProxyCount = (uint32_t)m_StaticMeshProxies.size();
		m_LastSyncStats.GPUSceneDirtyInstances = m_GPUScene.GetDirtyInstanceCount();
		m_LastSyncStats.MaterialSceneDirtyMaterials = m_MaterialScene.GetDirtyMaterialCount();
		m_LastSyncStats.TextureSceneDirtyTextures = m_TextureScene.GetDirtyTextureCount();
	}

	void RenderScene::Clear()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_FrameIndex = 0;
		m_NextPrimitiveID = 1;
		m_PrimitiveIDsByEntity.clear();
		m_StaticMeshProxiesByEntity.clear();
		m_FrameStaticMeshProxyOrder.clear();
		m_StaticMeshProxies.clear();
		m_GPUScene.Clear();
		m_TextureScene.Clear();
		m_MaterialScene.Clear();
		m_LastSyncStats = {};
	}

	const StaticMeshRenderProxy* RenderScene::FindStaticMeshProxy(UUID entityID) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		auto it = m_StaticMeshProxiesByEntity.find(entityID);
		if (it == m_StaticMeshProxiesByEntity.end())
			return nullptr;

		return &it->second;
	}

	void RenderScene::RebuildStaticMeshGPUInstances(StaticMeshRenderProxy& proxy, bool forceDirty, const glm::mat4& previousWorldTransform)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		proxy.SubmeshInstances.clear();
		if (!proxy.StaticMesh || !proxy.MeshSource)
			return;

		const auto& submeshData = proxy.MeshSource->GetSubmeshes();
		const GPUSceneInstanceFlags flags = BuildGPUSceneInstanceFlags(proxy);

		for (uint32_t submeshIndex : proxy.StaticMesh->GetSubmeshes())
		{
			if (submeshIndex >= submeshData.size())
				continue;

			const Submesh& submesh = submeshData[submeshIndex];
			const glm::mat4 submeshTransform = proxy.WorldTransform * submesh.Transform;
			const glm::mat4 previousSubmeshTransform = previousWorldTransform * submesh.Transform;
			const RenderMaterialID materialID = ResolveRenderMaterialID(proxy, submeshIndex);

			GPUSceneInstanceData instanceData;
			WriteTransformRows(instanceData.TransformRows, submeshTransform);
			WriteTransformRows(instanceData.PreviousTransformRows, previousSubmeshTransform);
			instanceData.BoundsSphere = CalculateWorldBoundsSphere(submesh.BoundingBox, submeshTransform);
			instanceData.Metadata = glm::uvec4(proxy.PrimitiveID, submeshIndex, materialID, (uint32_t)flags);
			instanceData.ObjectData = BuildGPUSceneObjectData(proxy.EntityID);

			GPUSceneInstanceRef& instanceRef = proxy.SubmeshInstances.emplace_back();
			instanceRef.InstanceID = m_GPUScene.UpsertInstance(instanceData, forceDirty);
			instanceRef.Data = instanceRef.InstanceID != InvalidGPUSceneInstanceID
				? m_GPUScene.GetInstance(instanceRef.InstanceID)
				: instanceData;
		}
	}

	void RenderScene::RefreshStaticMeshGPUInstances(StaticMeshRenderProxy& proxy)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const GPUSceneInstanceFlags flags = BuildGPUSceneInstanceFlags(proxy);
		for (GPUSceneInstanceRef& instanceRef : proxy.SubmeshInstances)
		{
			const uint32_t submeshIndex = GetGPUSceneSubmeshIndex(instanceRef.Data);
			const RenderMaterialID materialID = ResolveRenderMaterialID(proxy, submeshIndex);
			instanceRef.Data.PreviousTransformRows[0] = instanceRef.Data.TransformRows[0];
			instanceRef.Data.PreviousTransformRows[1] = instanceRef.Data.TransformRows[1];
			instanceRef.Data.PreviousTransformRows[2] = instanceRef.Data.TransformRows[2];
			instanceRef.Data.Metadata.z = materialID;
			instanceRef.Data.Metadata.w = (uint32_t)flags;
			instanceRef.InstanceID = m_GPUScene.UpsertInstance(instanceRef.Data);
			if (instanceRef.InstanceID != InvalidGPUSceneInstanceID)
				instanceRef.Data = m_GPUScene.GetInstance(instanceRef.InstanceID);
		}
	}

}
