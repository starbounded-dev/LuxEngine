// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

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
		uint64_t MaterialTableRevision = 0; // MaterialTable->GetRevision() when MaterialHash was taken

		Ref<StaticMesh> StaticMesh;
		Ref<MeshSource> MeshSource;
		Ref<MaterialTable> MaterialTable;
		// StaticMesh's own table. A StaticMesh sets it only when constructed, and StaticMesh keeps it alive.
		const ::Lux::MaterialTable* MeshMaterialTable = nullptr;

		glm::mat4 WorldTransform{ 1.0f };
		BoundingSphere WorldBounds;
		std::vector<GPUSceneInstanceRef> SubmeshInstances;

		bool Visible = true;
		bool Selected = false;

		RenderProxyDirtyFlags DirtyFlags = RenderProxyDirtyFlags::None;
		uint32_t LastDirtyFrame = 0;
		uint32_t LastMovedFrame = 0; // last frame created, given a new mesh or moved (a shadow caster settles from here)
		uint32_t LastTouchedFrame = 0;
		uint64_t Revision = 0; // bumped on every frame the proxy changed (DirtyFlags != None)
	};

	// What the scene knows about one static-mesh entity this frame. SyncStaticMesh compares it with
	// the retained proxy in place, so an unchanged entity costs a few compares.
	struct StaticMeshSyncInputs
	{
		AssetHandle StaticMeshHandle = 0;
		// The component's material overrides. Null or empty uses the static mesh's own table.
		const Ref<MaterialTable>* ComponentMaterialTable = nullptr;
		glm::mat4 WorldTransform{ 1.0f };
		bool Visible = true;
		bool Selected = false;
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

	// The renderer's retained copy of a scene's static meshes. Each frame the scene calls BeginSync,
	// SyncStaticMesh for every drawable entity, then EndSync, which drops the proxies not synced.
	// Proxies, material rows and texture slots persist across frames and are only rebuilt when their
	// inputs change. Main thread.
	class RenderScene : public RefCounted
	{
	public:
		RenderScene();
		RenderScene(const RenderScene&) = delete;
		RenderScene& operator=(const RenderScene&) = delete;

		void BeginSync();
		// Returns InvalidRenderPrimitiveID, and leaves the entity to be dropped, when its mesh does not resolve.
		RenderPrimitiveID SyncStaticMesh(UUID entityID, const StaticMeshSyncInputs& inputs);
		void EndSync();
		void Clear();

		// The visible proxies synced this frame, in sync order.
		const std::vector<const StaticMeshRenderProxy*>& GetStaticMeshProxies() const { return m_StaticMeshProxies; }
		// Primitives created or changed this frame, and primitives removed this frame.
		const std::vector<RenderPrimitiveID>& GetChangedPrimitives() const { return m_ChangedPrimitives; }
		const std::vector<RenderPrimitiveID>& GetRemovedPrimitives() const { return m_RemovedPrimitives; }
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
		// Unique across every RenderScene and renewed by Clear (which restarts primitive IDs), so a
		// cache keyed by RenderPrimitiveID can tell a cleared or reallocated scene from the one it saw.
		uint64_t GetGeneration() const { return m_Generation; }

	private:
		RenderPrimitiveID GetOrCreatePrimitiveID(UUID entityID);
		// Takes a reference on the submesh's material row; the instance's Metadata.z holds it.
		RenderMaterialID AcquireRenderMaterialID(const StaticMeshRenderProxy& proxy, uint32_t submeshIndex);
		void ReleaseInstanceMaterials(const StaticMeshRenderProxy& proxy);
		// The inputs an entity was last synced from, kept densely in sync order so an unchanged entity
		// is a sequential compare that never touches its proxy.
		struct StaticMeshSyncRecord
		{
			StaticMeshRenderProxy* Proxy = nullptr;
			UUID EntityID = 0;
			RenderPrimitiveID PrimitiveID = InvalidRenderPrimitiveID;
			AssetHandle StaticMeshHandle = 0;
			const MaterialTable* ComponentMaterialTable = nullptr;
			glm::mat4 WorldTransform{ 1.0f };
			bool Visible = true;
			bool Selected = false;
			bool Dirty = false; // changed that frame, so the next frame must refresh it (settle frame)
		};

		// Last frame's record for this entity, searched just ahead of the cursor: the scene syncs in
		// the same order every frame, so this finds it without hashing, and skips a few removed ones.
		const StaticMeshSyncRecord* FindPreviousSyncRecord(UUID entityID);
		void RebuildStaticMeshGPUInstances(StaticMeshRenderProxy& proxy, bool forceDirty, const glm::mat4& previousWorldTransform);
		void RefreshStaticMeshGPUInstances(StaticMeshRenderProxy& proxy);

	private:
		uint32_t m_FrameIndex = 0;
		uint64_t m_Generation = 0;
		RenderPrimitiveID m_NextPrimitiveID = 1;

		GPUScene m_GPUScene;
		TextureScene m_TextureScene;
		MaterialScene m_MaterialScene;
		RenderSceneSyncStats m_LastSyncStats;

		std::unordered_map<UUID, RenderPrimitiveID> m_PrimitiveIDsByEntity;
		std::unordered_map<UUID, StaticMeshRenderProxy> m_StaticMeshProxiesByEntity;

		// Pointers into m_StaticMeshProxiesByEntity: its nodes do not move on insert, and EndSync only
		// erases proxies that were not synced (so are not in the list).
		std::vector<const StaticMeshRenderProxy*> m_StaticMeshProxies;
		std::vector<RenderPrimitiveID> m_ChangedPrimitives;
		std::vector<RenderPrimitiveID> m_RemovedPrimitives;
		std::vector<RenderMaterialID> m_MaterialReleaseScratch;

		// This frame's and last frame's records, in sync order. Last frame's proxies were all synced,
		// so EndSync kept them and the Proxy pointers are valid.
		std::vector<StaticMeshSyncRecord> m_SyncOrder;
		std::vector<StaticMeshSyncRecord> m_PreviousSyncOrder;
		size_t m_SyncCursor = 0;
		// Proxies synced this frame: EndSync skips its removal walk when that is all of them.
		size_t m_SyncedProxyCount = 0;
		// MaterialTable::GetGlobalRevision() at the last sync; when it moved, every entity re-reads its table.
		uint64_t m_MaterialTableGlobalRevision = 0;
		bool m_MaterialTablesChanged = true;
		// AssetManager::GetDataGeneration() at the last sync; when it moved (an asset was reloaded), every
		// entity looks its mesh up again, so a reloaded mesh replaces the one its proxy holds.
		uint64_t m_AssetDataGeneration = 0;
		bool m_AssetDataChanged = true;
	};

}
