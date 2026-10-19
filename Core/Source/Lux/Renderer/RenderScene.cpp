// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"

#include "Lux/Renderer/RenderScene.h"

#include "Lux/Asset/AssetManager.h"

#include <algorithm>
#include <atomic>
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

		uint64_t NextRenderSceneGeneration()
		{
			static std::atomic<uint64_t> s_NextGeneration{ 1 };
			return s_NextGeneration.fetch_add(1, std::memory_order_relaxed);
		}

		uint64_t HashCombine(uint64_t seed, uint64_t value)
		{
			return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
		}

		uint64_t HashMaterialTable(const MaterialTable* materialTable)
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

	RenderScene::RenderScene()
		: m_Generation(NextRenderSceneGeneration())
	{
		m_MaterialScene.SetTextureScene(&m_TextureScene);
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
		m_StaticMeshProxies.clear();
		m_ChangedPrimitives.clear();
		m_RemovedPrimitives.clear();
		m_SyncOrder.clear();
		m_SyncCursor = 0;
		m_SyncedProxyCount = 0;
		const uint64_t materialTableGlobalRevision = MaterialTable::GetGlobalRevision();
		m_MaterialTablesChanged = materialTableGlobalRevision != m_MaterialTableGlobalRevision;
		m_MaterialTableGlobalRevision = materialTableGlobalRevision;
		m_GPUScene.BeginSync(m_FrameIndex);
		m_TextureScene.BeginSync();
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

	RenderMaterialID RenderScene::AcquireRenderMaterialID(const StaticMeshRenderProxy& proxy, uint32_t submeshIndex)
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

		return m_MaterialScene.AcquireMaterial(materialHandle);
	}

	void RenderScene::ReleaseInstanceMaterials(const StaticMeshRenderProxy& proxy)
	{
		for (const GPUSceneInstanceRef& instanceRef : proxy.SubmeshInstances)
			m_MaterialScene.ReleaseMaterial(instanceRef.Data.Metadata.z);
	}

	const RenderScene::StaticMeshSyncRecord* RenderScene::FindPreviousSyncRecord(UUID entityID)
	{
		constexpr size_t searchAhead = 8;
		const size_t end = std::min(m_SyncCursor + searchAhead, m_PreviousSyncOrder.size());
		for (size_t index = m_SyncCursor; index < end; index++)
		{
			if (m_PreviousSyncOrder[index].EntityID == entityID)
			{
				m_SyncCursor = index + 1;
				return &m_PreviousSyncOrder[index];
			}
		}

		return nullptr;
	}

	RenderPrimitiveID RenderScene::SyncStaticMesh(UUID entityID, const StaticMeshSyncInputs& inputs)
	{
		if ((uint64_t)entityID == 0 || !inputs.StaticMeshHandle)
			return InvalidRenderPrimitiveID;

		const MaterialTable* componentMaterialTable = inputs.ComponentMaterialTable ? inputs.ComponentMaterialTable->Raw() : nullptr;
		StaticMeshRenderProxy* proxyPtr = nullptr;
		bool created = false;
		if (const StaticMeshSyncRecord* record = FindPreviousSyncRecord(entityID))
		{
			// Unchanged since last frame, which did not change it either: nothing to do, and the
			// proxy is not even read. (EndSync marks it synced if it has to look for removals.)
			// A changed material table anywhere sends every entity down the full path that frame.
			if (!record->Dirty && !m_MaterialTablesChanged
				&& record->StaticMeshHandle == inputs.StaticMeshHandle
				&& record->ComponentMaterialTable == componentMaterialTable
				&& record->Visible == inputs.Visible
				&& record->Selected == inputs.Selected
				&& MatrixEquals(record->WorldTransform, inputs.WorldTransform))
			{
				m_SyncOrder.push_back(*record);
				m_SyncedProxyCount++;
				if (record->Visible)
					m_StaticMeshProxies.push_back(record->Proxy);
				return record->PrimitiveID;
			}

			proxyPtr = record->Proxy;
		}
		else
		{
			auto [proxyIt, inserted] = m_StaticMeshProxiesByEntity.try_emplace(entityID);
			proxyPtr = &proxyIt->second;
			created = inserted;
		}

		StaticMeshRenderProxy& proxy = *proxyPtr;
		RenderProxyDirtyFlags dirtyFlags = created ? RenderProxyDirtyFlags::Created : RenderProxyDirtyFlags::None;

		// Resolve the meshes only when the handle changed (or nothing resolved yet).
		bool meshChanged = false;
		if (created || proxy.StaticMeshHandle != inputs.StaticMeshHandle || !proxy.StaticMesh || !proxy.MeshSource)
		{
			Ref<StaticMesh> staticMesh = StaticMesh::GetOrCreateRuntime(inputs.StaticMeshHandle);
			const AssetHandle meshSourceHandle = staticMesh ? staticMesh->GetMeshSource() : AssetHandle(0);
			Ref<MeshSource> meshSource = staticMesh ? AssetManager::GetAsset<MeshSource>(meshSourceHandle) : nullptr;
			if (!meshSource)
			{
				// Not drawable: an existing proxy is left unsynced, so EndSync drops it.
				if (created)
					m_StaticMeshProxiesByEntity.erase(entityID);
				return InvalidRenderPrimitiveID;
			}

			meshChanged = proxy.StaticMeshHandle != inputs.StaticMeshHandle
				|| proxy.MeshSourceHandle != meshSourceHandle
				|| proxy.StaticMesh.Raw() != staticMesh.Raw()
				|| proxy.MeshSource.Raw() != meshSource.Raw();
			proxy.StaticMeshHandle = inputs.StaticMeshHandle;
			proxy.MeshSourceHandle = meshSourceHandle;
			proxy.StaticMesh = std::move(staticMesh);
			proxy.MeshSource = std::move(meshSource);
			proxy.MeshMaterialTable = proxy.StaticMesh->GetMaterials().Raw();
		}

		if (created)
		{
			proxy.EntityID = entityID;
			proxy.PrimitiveID = GetOrCreatePrimitiveID(entityID);
		}
		else if (meshChanged)
		{
			dirtyFlags |= RenderProxyDirtyFlags::Mesh;
		}

		// The table is hashed only when it is a different table or it was edited.
		const bool useComponentTable = inputs.ComponentMaterialTable && *inputs.ComponentMaterialTable
			&& !(*inputs.ComponentMaterialTable)->GetMaterials().empty();
		const MaterialTable* materialTable = useComponentTable ? inputs.ComponentMaterialTable->Raw() : proxy.MeshMaterialTable;
		const uint64_t materialTableRevision = materialTable ? materialTable->GetRevision() : 0;
		if (created || materialTable != proxy.MaterialTable.Raw() || materialTableRevision != proxy.MaterialTableRevision)
		{
			const uint64_t materialHash = HashMaterialTable(materialTable);
			if (!created && materialHash != proxy.MaterialHash)
				dirtyFlags |= RenderProxyDirtyFlags::Material;

			proxy.MaterialTable = useComponentTable ? *inputs.ComponentMaterialTable : proxy.StaticMesh->GetMaterials();
			proxy.MaterialTableRevision = materialTableRevision;
			proxy.MaterialHash = materialHash;
		}

		glm::mat4 previousWorldTransform = created ? inputs.WorldTransform : proxy.WorldTransform;
		if (created || meshChanged || !MatrixEquals(proxy.WorldTransform, inputs.WorldTransform))
		{
			const BoundingSphere localBounds = proxy.MeshSource->GetBoundingBox().ToBoundingSphere();
			const BoundingSphere worldBounds = BoundingSphere::Transform(localBounds, inputs.WorldTransform);
			if (!created && (!MatrixEquals(proxy.WorldTransform, inputs.WorldTransform) || !BoundsEquals(proxy.WorldBounds, worldBounds)))
				dirtyFlags |= RenderProxyDirtyFlags::Transform;

			proxy.WorldTransform = inputs.WorldTransform;
			proxy.WorldBounds = worldBounds;
		}

		if (!created && proxy.Visible != inputs.Visible)
			dirtyFlags |= RenderProxyDirtyFlags::Visibility;
		if (!created && proxy.Selected != inputs.Selected)
			dirtyFlags |= RenderProxyDirtyFlags::Selection;
		proxy.Visible = inputs.Visible;
		proxy.Selected = inputs.Selected;

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
		else if (dirtyFlags != RenderProxyDirtyFlags::None || proxy.DirtyFlags != RenderProxyDirtyFlags::None)
		{
			// The GPUScene is persistent — EndSync only re-uploads instances that were marked
			// dirty, and never prunes untouched ones. So a fully static proxy needs no refresh.
			// Refresh only when the proxy changed this frame, OR changed last frame: that one
			// settle frame lets a mesh that just stopped moving zero its motion-vector
			// previous-transform before we stop refreshing it.
			RefreshStaticMeshGPUInstances(proxy);
		}
		// An unchanged proxy does nothing: its material rows stay alive through the references its
		// instances hold, and MaterialScene rebuilds a row itself when the material asset changes.

		if (created || HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Mesh) || HasDirtyFlag(dirtyFlags, RenderProxyDirtyFlags::Transform))
			proxy.LastMovedFrame = m_FrameIndex;
		if (dirtyFlags != RenderProxyDirtyFlags::None)
		{
			proxy.LastDirtyFrame = m_FrameIndex;
			proxy.Revision++;
			m_ChangedPrimitives.push_back(proxy.PrimitiveID);
		}
		proxy.DirtyFlags = dirtyFlags;
		if (proxy.LastTouchedFrame != m_FrameIndex)
			m_SyncedProxyCount++;
		proxy.LastTouchedFrame = m_FrameIndex;

		StaticMeshSyncRecord& record = m_SyncOrder.emplace_back();
		record.Proxy = &proxy;
		record.EntityID = entityID;
		record.PrimitiveID = proxy.PrimitiveID;
		record.StaticMeshHandle = inputs.StaticMeshHandle;
		record.ComponentMaterialTable = componentMaterialTable;
		record.WorldTransform = inputs.WorldTransform;
		record.Visible = inputs.Visible;
		record.Selected = inputs.Selected;
		record.Dirty = dirtyFlags != RenderProxyDirtyFlags::None;

		AccumulateDirtyStats(m_LastSyncStats, dirtyFlags);

		if (proxy.Visible)
			m_StaticMeshProxies.push_back(&proxy);

		return proxy.PrimitiveID;
	}

	void RenderScene::EndSync()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// Only walk the proxies when some were not synced this frame. Unchanged proxies were not
		// stamped during the sync, so stamp every synced one first.
		if (m_SyncedProxyCount < m_StaticMeshProxiesByEntity.size())
		{
			for (const StaticMeshSyncRecord& record : m_SyncOrder)
				record.Proxy->LastTouchedFrame = m_FrameIndex;
		}

		for (auto it = m_StaticMeshProxiesByEntity.begin(); m_SyncedProxyCount < m_StaticMeshProxiesByEntity.size() && it != m_StaticMeshProxiesByEntity.end();)
		{
			if (it->second.LastTouchedFrame != m_FrameIndex)
			{
				ReleaseInstanceMaterials(it->second);
				m_GPUScene.RemovePrimitive(it->second.PrimitiveID);
				m_RemovedPrimitives.push_back(it->second.PrimitiveID);
				m_PrimitiveIDsByEntity.erase(it->first);
				it = m_StaticMeshProxiesByEntity.erase(it);
				m_LastSyncStats.RemovedProxies++;
			}
			else
			{
				++it;
			}
		}

		std::swap(m_SyncOrder, m_PreviousSyncOrder);

		m_GPUScene.EndSync();
		// MaterialScene first: freeing its rows releases texture slots the TextureScene then frees.
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
		m_Generation = NextRenderSceneGeneration();
		m_NextPrimitiveID = 1;
		m_PrimitiveIDsByEntity.clear();
		m_StaticMeshProxiesByEntity.clear();
		m_StaticMeshProxies.clear();
		m_ChangedPrimitives.clear();
		m_RemovedPrimitives.clear();
		m_SyncOrder.clear();
		m_PreviousSyncOrder.clear();
		m_SyncCursor = 0;
		m_SyncedProxyCount = 0;
		m_MaterialTablesChanged = true;
		m_GPUScene.Clear();
		m_MaterialScene.Clear();
		m_TextureScene.Clear();
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
		// Release the old instances' materials after acquiring the new ones, so a shared row is kept.
		m_MaterialReleaseScratch.clear();
		for (const GPUSceneInstanceRef& instanceRef : proxy.SubmeshInstances)
			m_MaterialReleaseScratch.push_back(instanceRef.Data.Metadata.z);
		proxy.SubmeshInstances.clear();

		if (proxy.StaticMesh && proxy.MeshSource)
		{
			const auto& submeshData = proxy.MeshSource->GetSubmeshes();
			const GPUSceneInstanceFlags flags = BuildGPUSceneInstanceFlags(proxy);

			for (uint32_t submeshIndex : proxy.StaticMesh->GetSubmeshes())
			{
				if (submeshIndex >= submeshData.size())
					continue;

				const Submesh& submesh = submeshData[submeshIndex];
				const glm::mat4 submeshTransform = proxy.WorldTransform * submesh.Transform;
				const glm::mat4 previousSubmeshTransform = previousWorldTransform * submesh.Transform;
				const RenderMaterialID materialID = AcquireRenderMaterialID(proxy, submeshIndex);

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

		for (RenderMaterialID materialID : m_MaterialReleaseScratch)
			m_MaterialScene.ReleaseMaterial(materialID);
	}

	void RenderScene::RefreshStaticMeshGPUInstances(StaticMeshRenderProxy& proxy)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const GPUSceneInstanceFlags flags = BuildGPUSceneInstanceFlags(proxy);
		for (GPUSceneInstanceRef& instanceRef : proxy.SubmeshInstances)
		{
			const uint32_t submeshIndex = GetGPUSceneSubmeshIndex(instanceRef.Data);
			const RenderMaterialID materialID = AcquireRenderMaterialID(proxy, submeshIndex);
			m_MaterialScene.ReleaseMaterial(instanceRef.Data.Metadata.z);
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
