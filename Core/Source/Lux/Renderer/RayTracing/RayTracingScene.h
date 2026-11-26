// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/RHIAccelerationStructure.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/StorageBuffer.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Lux {

	// What the shared kernel needs to read the scene's environment map (PathTraceCommon.glslh Environment()).
	struct PathTraceEnvParams
	{
		uint32_t Flags = 0; // 2: sample the environment map
		float Intensity = 1.0f;
		float Rotation = 0.0f; // degrees about +Y
		// Global fog, the same for every ray-traced pass (PathTraceCommon.glslh: Medium, MediumAlbedo, MediumCenter).
		glm::vec4 FogMedium = glm::vec4(0.0f);
		glm::vec4 FogAlbedo = glm::vec4(0.0f);
		glm::vec4 FogCenter = glm::vec4(0.0f);
	};

	struct StaticMeshRenderProxy;


	// The scene as ray-tracing geometry: one BLAS per (mesh source, submesh), one TLAS instance per
	// drawn submesh, plus the SSBOs a ray-query shader needs to shade a hit (vertices, indices,
	// per-instance material and normal matrix). Opt-in (path tracer, ray-traced GI); never built while
	// no ray-traced feature is on, and only when RHIDevice::SupportsRayQuery().
	//
	// Geometry is rebuilt when the set of meshes changes and the TLAS when any instance transform or
	// material changes, both from RT_Update on the render thread, recorded on the frame command buffer.
	// Skinned meshes are left out (v1 limit). Memory the GPU may still read is freed through
	// Renderer::SubmitResourceFree.
	class RayTracingScene : public RHIAccelerationStructure
	{
	public:
		// std430 mirror of RTInstance in PathTrace.glsl.
		struct GPUInstance
		{
			glm::vec4 NormalRow0;
			glm::vec4 NormalRow1;
			glm::vec4 NormalRow2;
			glm::vec4 WorldRow0;
			glm::vec4 WorldRow1;
			glm::vec4 WorldRow2;
			glm::uvec4 Info; // first BLAS vertex, first BLAS index, material row, emissive instance ordinal
			glm::vec4 AlbedoMetal;
			glm::vec4 EmissionRough;
		};
		static_assert(sizeof(GPUInstance) == 144);

		struct SubmeshKey
		{
			const MeshSource* Source = nullptr;
			uint32_t Submesh = 0;
			uint64_t Variant = 0; // 0 for a static mesh; for a skinned instance a hash of its entity (its own skinned copy; stable across poses)
			bool operator==(const SubmeshKey& other) const { return Source == other.Source && Submesh == other.Submesh && Variant == other.Variant; }
		};

		// One drawn submesh, resolved on the main thread (materials and meshes are asset-system objects).
		struct InstanceInput
		{
			Ref<MeshSource> Source; // keeps the mesh alive until the render thread has built it
			uint32_t Submesh = 0;
			glm::mat4 ObjectToWorld{ 1.0f };
			glm::vec3 Albedo{ 0.8f };
			float Metalness = 0.0f;
			glm::vec3 Emission{ 0.0f };
			float Roughness = 0.5f;
			uint32_t MaterialID = 0xffffffffu; // the raster renderer's GPU material row (textures), or none
			uint64_t Variant = 0;              // nonzero: a skinned instance, with its own CPU-skinned geometry (stable per entity)
			uint64_t PoseHash = 0;             // hash of the skin palette: a changed pose refits the BLAS instead of rebuilding it
			std::vector<glm::mat4> SkinPalette; // bone model pose * inverse bind, as the skinning shader reads it

			bool IsEmissive() const { return glm::any(glm::greaterThan(Emission, glm::vec3(0.0f))); }
		};

		// Acceleration-structure tuning, process-wide (set from SceneRendererOptions; render thread reads it in RT_Update).
		struct Tuning
		{
			bool CompactBLAS = true;   // static BLASes are built compactable and later copied into right-sized structures
			bool RefitSkinned = true;  // a changed skin pose updates the BLAS in place instead of rebuilding every BLAS
			bool UpdateTLAS = true;    // a transform-only change refits the TLAS instead of rebuilding it
		};
		static void SetTuning(const Tuning& tuning);

		RayTracingScene();
		~RayTracingScene() override;

		// Main thread. One input per drawn submesh of the (unskinned, visible) proxies.
		static std::vector<InstanceInput> GatherInstances(const std::vector<const StaticMeshRenderProxy*>& proxies);

		// Render thread. Rebuilds what changed since the last call and records the builds into `cb`.
		// Returns true when the geometry or any instance changed (a progressive renderer restarts).
		bool RT_Update(Ref<RenderCommandBuffer> cb, const std::vector<InstanceInput>& instances);

		bool IsValid() const { return m_TLAS != nullptr && m_InstanceCount > 0; }
		uint32_t GetInstanceCount() const { return m_InstanceCount; }
		uint32_t GetBLASCount() const { return static_cast<uint32_t>(m_BLASes.size()); }
		uint64_t GetTriangleCount() const { return m_TriangleCount; }
		// Render-thread counters (logged by the checks): how many BLAS refits / TLAS updates / TLAS rebuilds / compactions ran.
		uint32_t GetBLASRefitCount() const { return m_BLASRefits; }
		uint32_t GetTLASUpdateCount() const { return m_TLASUpdates; }
		uint32_t GetTLASRebuildCount() const { return m_TLASRebuilds; }
		uint32_t GetCompactedBLASCount() const { return m_CompactedBLASes; }

		const Ref<StorageBuffer>& GetVertexBuffer() const { return m_VertexBuffer; }
		const Ref<StorageBuffer>& GetIndexBuffer() const { return m_IndexBuffer; }
		const Ref<StorageBuffer>& GetInstanceBuffer() const { return m_InstanceBuffer; }

		nri::Descriptor* GetDescriptor() const override { return m_TLASDescriptor; }
		const void* GetIdentity() const override { return m_TLAS; }

	private:
		struct SubmeshKeyHash
		{
			size_t operator()(const SubmeshKey& key) const { return (std::hash<const void*>()(key.Source) * 31u + key.Submesh) * 1099511628211ull ^ key.Variant; }
		};
		struct BLASRecord
		{
			nri::AccelerationStructure* Structure = nullptr;
			uint32_t VertexBase = 0;
			uint32_t IndexBase = 0;
			uint32_t VertexCount = 0;
			uint32_t IndexCount = 0;
			bool Updatable = false;   // built with ALLOW_UPDATE (skinned)
			uint64_t PoseHash = 0;    // pose the BLAS currently holds
		};
		// Static BLASes whose compacted sizes are being read back (a few frames after the build, never stalling).
		struct CompactionBatch
		{
			nri::QueryPool* Pool = nullptr;
			nri::Buffer* Readback = nullptr;
			std::vector<SubmeshKey> Keys;
			std::vector<nri::AccelerationStructure*> Sources;
			std::vector<nri::BottomLevelGeometryDesc> Geometries;
			uint64_t IssuedAt = 0;
			uint64_t GeometrySignature = 0;
		};
		static SubmeshKey KeyOf(const InstanceInput& input) { return { input.Source.Raw(), input.Submesh, input.Variant }; }
		void EnsureCapacity(Ref<StorageBuffer> buffer, uint32_t bytes);
		void BuildGeometry(Ref<RenderCommandBuffer> cb, const std::vector<InstanceInput>& instances);
		void BuildTLAS(Ref<RenderCommandBuffer> cb, const std::vector<InstanceInput>& instances, bool geometryRebuilt, bool forceRebuild, const Tuning& tuning);
		void RefitSkinned(Ref<RenderCommandBuffer> cb, const std::vector<InstanceInput>& instances);
		bool ProcessCompaction(Ref<RenderCommandBuffer> cb); // true when a BLAS was swapped (the TLAS must be rebuilt)
		void ReleaseGeometry();
		void ReleaseTLAS();

		std::unordered_map<SubmeshKey, BLASRecord, SubmeshKeyHash> m_BLASes;
		uint64_t m_GeometrySignature = 0;
		uint64_t m_InstanceSignature = 0;
		uint64_t m_PoseSignature = 0;
		uint64_t m_UpdateCounter = 0;
		uint32_t m_BLASRefits = 0, m_TLASUpdates = 0, m_TLASRebuilds = 0, m_CompactedBLASes = 0;
		bool m_TLASBuilt = false;
		std::vector<CompactionBatch> m_Compactions;
		uint64_t m_TriangleCount = 0;
		uint32_t m_InstanceCount = 0;
		uint32_t m_TLASCapacity = 0;

		Ref<StorageBuffer> m_VertexBuffer;
		Ref<StorageBuffer> m_IndexBuffer;
		Ref<StorageBuffer> m_InstanceBuffer;

		nri::AccelerationStructure* m_TLAS = nullptr;
		nri::Descriptor* m_TLASDescriptor = nullptr;
		nri::Buffer* m_TLASInstanceBuffer = nullptr; // HOST_UPLOAD, TopLevelInstance entries
		nri::Buffer* m_TLASScratch = nullptr;
	};

}
