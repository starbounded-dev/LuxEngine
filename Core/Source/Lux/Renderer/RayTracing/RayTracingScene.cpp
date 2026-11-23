// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RayTracingScene.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/RenderScene.h"
#include "Lux/Renderer/Renderer.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cstring>
#include <tuple>

namespace Lux {

	namespace {

		constexpr uint64_t k_ScratchAlignment = 256;

		uint64_t HashBytes(uint64_t hash, const void* data, size_t size)
		{
			const uint8_t* bytes = static_cast<const uint8_t*>(data);
			for (size_t i = 0; i < size; i++)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
			return hash;
		}

		template<typename T>
		uint64_t HashValue(uint64_t hash, const T& value) { return HashBytes(hash, &value, sizeof(T)); }

		uint64_t AlignUp(uint64_t value, uint64_t alignment) { return (value + alignment - 1) / alignment * alignment; }

		struct PackedRTVertex
		{
			glm::vec4 Position;
			glm::vec4 Normal;
		};

		nri::Buffer* CreateRawBuffer(uint64_t size, nri::BufferUsageBits usage, nri::MemoryLocation location, const char* name)
		{
			nri::BufferDesc desc = {};
			desc.size = std::max<uint64_t>(size, 256);
			desc.usage = usage;
			nri::Buffer* buffer = nullptr;
			if (RHIDevice::API().CreateCommittedBuffer(RHIDevice::Get(), location, 0.0f, desc, buffer) != nri::Result::SUCCESS)
			{
				LUX_CORE_ERROR_TAG("Renderer", "[RT] Failed to create buffer {} ({} bytes)", name, size);
				return nullptr;
			}
			RHIDevice::API().SetDebugName(buffer, name);
			return buffer;
		}

		// The material a submesh of this proxy draws with, as RenderScene resolves it.
		Ref<MaterialAsset> ResolveMaterial(const StaticMeshRenderProxy& proxy, uint32_t materialIndex)
		{
			AssetHandle handle = 0;
			if (proxy.MaterialTable)
			{
				if (proxy.MaterialTable->HasMaterial(materialIndex))
					handle = proxy.MaterialTable->GetMaterial(materialIndex);
				else if (proxy.MaterialTable->GetMaterials().size() == 1 && proxy.MaterialTable->HasMaterial(0))
					handle = proxy.MaterialTable->GetMaterial(0);
			}
			if (!handle && proxy.StaticMesh)
			{
				Ref<MaterialTable> table = proxy.StaticMesh->GetMaterials();
				if (table && table->HasMaterial(materialIndex))
					handle = table->GetMaterial(materialIndex);
			}
			if (!handle && proxy.MeshSource && materialIndex < proxy.MeshSource->GetMaterials().size())
				handle = proxy.MeshSource->GetMaterials()[materialIndex];
			return handle ? AssetManager::GetAsset<MaterialAsset>(handle) : nullptr;
		}

	}

	RayTracingScene::RayTracingScene()
	{
		// Stable buffer objects: they grow in place (StorageBuffer::Resize) so descriptor sets that hold them
		// rebake on their own when the NRI buffer behind them is replaced.
		StorageBufferSpecification spec;
		spec.GPUOnly = false;
		spec.AccelerationStructureInput = true;
		spec.DebugName = "RT Vertices";
		m_VertexBuffer = StorageBuffer::Create(256, spec);
		spec.DebugName = "RT Indices";
		m_IndexBuffer = StorageBuffer::Create(256, spec);
		spec.AccelerationStructureInput = false;
		spec.DebugName = "RT Instances";
		m_InstanceBuffer = StorageBuffer::Create(256, spec);
	}

	void RayTracingScene::EnsureCapacity(Ref<StorageBuffer> buffer, uint32_t bytes)
	{
		if (buffer->GetSize() < bytes)
			buffer->Resize(std::max<uint32_t>(bytes, 256u));
	}

	RayTracingScene::~RayTracingScene()
	{
		ReleaseGeometry();
		ReleaseTLAS();
	}

	void RayTracingScene::ReleaseGeometry()
	{
		if (m_BLASes.empty())
			return;
		std::vector<nri::AccelerationStructure*> structures;
		for (auto& [key, record] : m_BLASes)
			structures.push_back(record.Structure);
		m_BLASes.clear();
		Renderer::SubmitResourceFree([structures = std::move(structures)]()
		{
			for (nri::AccelerationStructure* structure : structures)
				RHIDevice::API().DestroyAccelerationStructure(structure);
		});
	}

	void RayTracingScene::ReleaseTLAS()
	{
		if (!m_TLAS && !m_TLASInstanceBuffer && !m_TLASScratch)
			return;
		Renderer::SubmitResourceFree([as = m_TLAS, descriptor = m_TLASDescriptor, instances = m_TLASInstanceBuffer, scratch = m_TLASScratch]()
		{
			const NRIInterface& api = RHIDevice::API();
			if (descriptor)
				api.DestroyDescriptor(descriptor);
			if (as)
				api.DestroyAccelerationStructure(as);
			if (instances)
				api.DestroyBuffer(instances);
			if (scratch)
				api.DestroyBuffer(scratch);
		});
		m_TLAS = nullptr;
		m_TLASDescriptor = nullptr;
		m_TLASInstanceBuffer = nullptr;
		m_TLASScratch = nullptr;
		m_TLASCapacity = 0;
	}

	std::vector<RayTracingScene::InstanceInput> RayTracingScene::GatherInstances(const std::vector<const StaticMeshRenderProxy*>& proxies)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::vector<InstanceInput> instances;
		for (const StaticMeshRenderProxy* proxy : proxies)
		{
			if (!proxy || !proxy->Visible || !proxy->MeshSource || !proxy->StaticMesh)
				continue;
			// A skinned proxy is traced from a CPU-skinned copy (its palette is the raster renderer's), when the mesh has skin data.
			const bool skinned = proxy->IsSkinned() && !proxy->MeshSource->GetBoneInfluences().empty();
			if (proxy->IsSkinned() && !skinned)
				continue;

			const auto& submeshes = proxy->MeshSource->GetSubmeshes();
			for (uint32_t submeshIndex : proxy->StaticMesh->GetSubmeshes())
			{
				if (submeshIndex >= submeshes.size())
					continue;
				const Submesh& submesh = submeshes[submeshIndex];
				if (submesh.IndexCount == 0 || submesh.VertexCount == 0)
					continue;

				InstanceInput input;
				input.Source = proxy->MeshSource;
				input.Submesh = submeshIndex;
				// A skinned proxy's rows ignore the submesh node transform (RenderScene).
				input.ObjectToWorld = skinned ? proxy->WorldTransform : proxy->WorldTransform * submesh.Transform;
				if (skinned)
				{
					input.Variant = (proxy->SkinPaletteHash ^ (static_cast<uint64_t>(proxy->EntityID) * 0x9E3779B97F4A7C15ull)) | 1ull;
					input.SkinPalette = proxy->SkinPalette;
				}
				if (Ref<MaterialAsset> material = ResolveMaterial(*proxy, submesh.MaterialIndex))
				{
					input.Albedo = material->GetAlbedoColor();
					input.Metalness = material->GetMetalness();
					input.Roughness = material->GetRoughness();
					input.Emission = input.Albedo * material->GetEmission();
				}
				for (const GPUSceneInstanceRef& ref : proxy->SubmeshInstances)
				{
					if (ref.InstanceID != InvalidGPUSceneInstanceID && ref.Data.Metadata.y == submeshIndex)
					{
						input.MaterialID = ref.Data.Metadata.z;
						break;
					}
				}
				instances.push_back(input);
			}
		}
		return instances;
	}

	bool RayTracingScene::RT_Update(Ref<RenderCommandBuffer> cb, const std::vector<InstanceInput>& instances)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!RHIDevice::SupportsRayQuery())
			return false;

		// Which meshes are present decides the geometry; transforms and materials only the TLAS.
		uint64_t geometrySignature = 1469598103934665603ull;
		{
			std::vector<SubmeshKey> keys;
			for (const InstanceInput& input : instances)
				keys.push_back(KeyOf(input));
			std::sort(keys.begin(), keys.end(), [](const SubmeshKey& a, const SubmeshKey& b) { return std::tie(a.Source, a.Submesh, a.Variant) < std::tie(b.Source, b.Submesh, b.Variant); });
			keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
			for (const SubmeshKey& key : keys)
				geometrySignature = HashValue(HashValue(HashValue(geometrySignature, key.Source), key.Submesh), key.Variant);
		}
		uint64_t instanceSignature = HashValue(1469598103934665603ull, static_cast<uint32_t>(instances.size()));
		for (const InstanceInput& input : instances)
		{
			instanceSignature = HashValue(instanceSignature, input.Source.Raw());
			instanceSignature = HashValue(instanceSignature, input.Submesh);
			instanceSignature = HashValue(instanceSignature, input.Variant);
			instanceSignature = HashValue(instanceSignature, input.ObjectToWorld);
			instanceSignature = HashValue(instanceSignature, input.Albedo);
			instanceSignature = HashValue(instanceSignature, input.Metalness);
			instanceSignature = HashValue(instanceSignature, input.Roughness);
			instanceSignature = HashValue(instanceSignature, input.Emission);
			instanceSignature = HashValue(instanceSignature, input.MaterialID);
		}

		const bool geometryChanged = geometrySignature != m_GeometrySignature;
		const bool instancesChanged = instanceSignature != m_InstanceSignature;
		if (!geometryChanged && !instancesChanged)
			return false;

		if (instances.empty())
		{
			ReleaseGeometry();
			ReleaseTLAS();
			m_InstanceCount = 0;
			m_GeometrySignature = geometrySignature;
			m_InstanceSignature = instanceSignature;
			return true;
		}

		if (geometryChanged)
			BuildGeometry(cb, instances);
		BuildTLAS(cb, instances, geometryChanged);

		m_GeometrySignature = geometrySignature;
		m_InstanceSignature = instanceSignature;
		return true;
	}

	void RayTracingScene::BuildGeometry(Ref<RenderCommandBuffer> cb, const std::vector<InstanceInput>& instances)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const NRIInterface& api = RHIDevice::API();
		ReleaseGeometry();

		// Unique (source, submesh) in first-seen order; vertices and indices concatenated.
		std::vector<SubmeshKey> keys;
		for (const InstanceInput& input : instances)
		{
			if (std::find(keys.begin(), keys.end(), KeyOf(input)) == keys.end())
				keys.push_back(KeyOf(input));
		}

		std::vector<PackedRTVertex> vertices;
		std::vector<uint32_t> indices;
		struct Pending { SubmeshKey Key; uint32_t VertexBase; uint32_t VertexCount; uint32_t IndexBase; uint32_t IndexCount; };
		std::vector<Pending> pending;
		m_TriangleCount = 0;
		for (const SubmeshKey& key : keys)
		{
			const Submesh& submesh = key.Source->GetSubmeshes()[key.Submesh];
			const auto& sourceVertices = key.Source->GetVertices();
			const auto& sourceIndices = key.Source->GetIndices();

			Pending entry = { key, static_cast<uint32_t>(vertices.size()), submesh.VertexCount, static_cast<uint32_t>(indices.size()), submesh.IndexCount };
			// The palette of a skinned instance (the first one with this key).
			const std::vector<glm::mat4>* palette = nullptr;
			if (key.Variant != 0)
			{
				for (const InstanceInput& input : instances)
				{
					if (KeyOf(input) == key)
					{
						palette = &input.SkinPalette;
						break;
					}
				}
			}
			const auto& influences = key.Source->GetBoneInfluences();
			for (uint32_t v = 0; v < submesh.VertexCount; v++)
			{
				const uint32_t source = submesh.BaseVertex + v;
				if (source >= sourceVertices.size())
				{
					vertices.push_back({ glm::vec4(0, 0, 0, 0), glm::vec4(0, 1, 0, 0) });
					continue;
				}
				glm::vec3 position = sourceVertices[source].Position;
				glm::vec3 normal = sourceVertices[source].Normal;
				if (palette && source < influences.size())
				{
					// Linear blend skinning, as Skinning.glsl: the weighted sum of the palette matrices.
					glm::mat4 blended(0.0f);
					for (int i = 0; i < 4; i++)
					{
						const uint32_t bone = influences[source].BoneInfoIndices[i];
						if (bone < palette->size())
							blended += (*palette)[bone] * influences[source].Weights[i];
					}
					position = glm::vec3(blended * glm::vec4(position, 1.0f));
					normal = glm::normalize(glm::mat3(blended) * normal);
				}
				// w components carry the uv the raster shader uses, (u, 1 - v), for material textures.
				vertices.push_back({ glm::vec4(position, sourceVertices[source].Texcoord.x), glm::vec4(normal, 1.0f - sourceVertices[source].Texcoord.y) });
			}
			// Index (triangle) k covers scalar indices 3k..3k+2; BaseIndex is in scalar indices.
			for (uint32_t i = 0; i < submesh.IndexCount; i++)
			{
				const uint32_t scalar = submesh.BaseIndex + i;
				const uint32_t triangle = scalar / 3;
				const uint32_t corner = scalar % 3;
				uint32_t value = 0;
				if (triangle < sourceIndices.size())
				{
					const Index& index = sourceIndices[triangle];
					value = corner == 0 ? index.V1 : (corner == 1 ? index.V2 : index.V3);
				}
				indices.push_back(value);
			}
			m_TriangleCount += submesh.IndexCount / 3;
			pending.push_back(entry);
		}

		EnsureCapacity(m_VertexBuffer, static_cast<uint32_t>(vertices.size() * sizeof(PackedRTVertex)));
		m_VertexBuffer->RT_SetData(cb, vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(PackedRTVertex)));
		EnsureCapacity(m_IndexBuffer, static_cast<uint32_t>(indices.size() * sizeof(uint32_t)));
		m_IndexBuffer->RT_SetData(cb, indices.data(), static_cast<uint32_t>(indices.size() * sizeof(uint32_t)));

		// One BLAS (one geometry) per submesh.
		std::vector<nri::BottomLevelGeometryDesc> geometries(pending.size());
		std::vector<nri::BuildBottomLevelAccelerationStructureDesc> builds(pending.size());
		std::vector<nri::AccelerationStructure*> structures(pending.size(), nullptr);
		std::vector<uint64_t> scratchOffsets(pending.size(), 0);
		uint64_t scratchTotal = 0;
		for (size_t i = 0; i < pending.size(); i++)
		{
			nri::BottomLevelGeometryDesc& geometry = geometries[i];
			geometry = {};
			geometry.flags = nri::BottomLevelGeometryBits::OPAQUE_GEOMETRY;
			geometry.type = nri::BottomLevelGeometryType::TRIANGLES;
			geometry.triangles.vertexBuffer = m_VertexBuffer->GetRHIBuffer();
			geometry.triangles.vertexOffset = static_cast<uint64_t>(pending[i].VertexBase) * sizeof(PackedRTVertex);
			geometry.triangles.vertexNum = pending[i].VertexCount;
			geometry.triangles.vertexStride = sizeof(PackedRTVertex);
			geometry.triangles.vertexFormat = nri::Format::RGB32_SFLOAT;
			geometry.triangles.indexBuffer = m_IndexBuffer->GetRHIBuffer();
			geometry.triangles.indexOffset = static_cast<uint64_t>(pending[i].IndexBase) * sizeof(uint32_t);
			geometry.triangles.indexNum = pending[i].IndexCount;
			geometry.triangles.indexType = nri::IndexType::UINT32;

			nri::AccelerationStructureDesc desc = {};
			desc.geometries = &geometry;
			desc.geometryOrInstanceNum = 1;
			desc.flags = nri::AccelerationStructureBits::PREFER_FAST_TRACE;
			desc.type = nri::AccelerationStructureType::BOTTOM_LEVEL;
			if (api.CreateCommittedAccelerationStructure(RHIDevice::Get(), nri::MemoryLocation::DEVICE, 0.0f, desc, structures[i]) != nri::Result::SUCCESS)
			{
				LUX_CORE_ERROR_TAG("Renderer", "[RT] Failed to create a BLAS");
				structures[i] = nullptr;
				continue;
			}
			scratchOffsets[i] = scratchTotal;
			scratchTotal += AlignUp(api.GetAccelerationStructureBuildScratchBufferSize(*structures[i]), k_ScratchAlignment);
		}

		nri::Buffer* scratch = CreateRawBuffer(scratchTotal, nri::BufferUsageBits::SCRATCH, nri::MemoryLocation::DEVICE, "RT BLAS Scratch");
		std::vector<nri::BuildBottomLevelAccelerationStructureDesc> validBuilds;
		for (size_t i = 0; i < pending.size(); i++)
		{
			if (!structures[i])
				continue;
			nri::BuildBottomLevelAccelerationStructureDesc& build = builds[i];
			build = {};
			build.dst = structures[i];
			build.geometries = &geometries[i];
			build.geometryNum = 1;
			build.scratchBuffer = scratch;
			build.scratchOffset = scratchOffsets[i];
			validBuilds.push_back(build);

			BLASRecord record;
			record.Structure = structures[i];
			record.VertexBase = pending[i].VertexBase;
			record.IndexBase = pending[i].IndexBase;
			m_BLASes[pending[i].Key] = record;
		}

		if (!validBuilds.empty() && scratch)
		{
			if (nri::CommandBuffer* commands = cb->RT_BeginNRITransfer())
			{
				api.CmdBuildBottomLevelAccelerationStructures(*commands, validBuilds.data(), static_cast<uint32_t>(validBuilds.size()));
				// The TLAS build (and the shaders) read what these builds wrote.
				nri::GlobalBarrierDesc global = {};
				global.before = { nri::AccessBits::ACCELERATION_STRUCTURE_WRITE, nri::StageBits::ACCELERATION_STRUCTURE };
				global.after = { nri::AccessBits::ACCELERATION_STRUCTURE_READ, nri::StageBits::ACCELERATION_STRUCTURE | nri::StageBits::COMPUTE_SHADER };
				nri::BarrierDesc barrier = {};
				barrier.globals = &global;
				barrier.globalNum = 1;
				api.CmdBarrier(*commands, barrier);
				cb->RT_EndNRISegment();
			}
		}

		if (scratch)
		{
			Renderer::SubmitResourceFree([scratch]() { RHIDevice::API().DestroyBuffer(scratch); });
		}
	}

	void RayTracingScene::BuildTLAS(Ref<RenderCommandBuffer> cb, const std::vector<InstanceInput>& instances, bool geometryRebuilt)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const NRIInterface& api = RHIDevice::API();
		const uint32_t count = static_cast<uint32_t>(instances.size());

		if (!m_TLAS || count > m_TLASCapacity || geometryRebuilt)
		{
			ReleaseTLAS();
			uint32_t capacity = 16;
			while (capacity < count)
				capacity *= 2;

			nri::AccelerationStructureDesc desc = {};
			desc.geometryOrInstanceNum = capacity;
			desc.flags = nri::AccelerationStructureBits::PREFER_FAST_TRACE;
			desc.type = nri::AccelerationStructureType::TOP_LEVEL;
			if (api.CreateCommittedAccelerationStructure(RHIDevice::Get(), nri::MemoryLocation::DEVICE, 0.0f, desc, m_TLAS) != nri::Result::SUCCESS)
			{
				LUX_CORE_ERROR_TAG("Renderer", "[RT] Failed to create the TLAS");
				m_TLAS = nullptr;
				return;
			}
			if (api.CreateAccelerationStructureDescriptor(*m_TLAS, m_TLASDescriptor) != nri::Result::SUCCESS)
			{
				LUX_CORE_ERROR_TAG("Renderer", "[RT] Failed to create the TLAS descriptor");
				m_TLASDescriptor = nullptr;
			}
			m_TLASCapacity = capacity;
			m_TLASInstanceBuffer = CreateRawBuffer(sizeof(nri::TopLevelInstance) * capacity, nri::BufferUsageBits::ACCELERATION_STRUCTURE_BUILD_INPUT, nri::MemoryLocation::HOST_UPLOAD, "RT TLAS Instances");
			m_TLASScratch = CreateRawBuffer(api.GetAccelerationStructureBuildScratchBufferSize(*m_TLAS), nri::BufferUsageBits::SCRATCH, nri::MemoryLocation::DEVICE, "RT TLAS Scratch");
		}
		if (!m_TLAS || !m_TLASInstanceBuffer || !m_TLASScratch)
			return;

		// TLAS instances and the shader-side instance table, in the same order (instanceId = index).
		std::vector<GPUInstance> gpuInstances(count);
		auto* mapped = static_cast<nri::TopLevelInstance*>(api.MapBuffer(*m_TLASInstanceBuffer, 0, sizeof(nri::TopLevelInstance) * m_TLASCapacity));
		if (!mapped)
			return;
		std::memset(mapped, 0, sizeof(nri::TopLevelInstance) * m_TLASCapacity);
		for (uint32_t i = 0; i < count; i++)
		{
			const InstanceInput& input = instances[i];
			const BLASRecord& blas = m_BLASes[KeyOf(input)];

			nri::TopLevelInstance& instance = mapped[i];
			for (int row = 0; row < 3; row++)
				for (int column = 0; column < 4; column++)
					instance.transform[row][column] = input.ObjectToWorld[column][row];
			instance.instanceId = i;
			instance.mask = 0xFF;
			instance.shaderBindingTableRecordOffset = 0;
			instance.flags = nri::TopLevelInstanceBits::TRIANGLE_CULL_DISABLE;
			instance.accelerationStructureHandle = blas.Structure ? api.GetAccelerationStructureHandle(*blas.Structure) : 0;

			const glm::mat3 inverse = glm::inverse(glm::mat3(input.ObjectToWorld));
			GPUInstance& gpu = gpuInstances[i];
			gpu.NormalRow0 = glm::vec4(inverse[0], 0.0f);
			gpu.NormalRow1 = glm::vec4(inverse[1], 0.0f);
			gpu.NormalRow2 = glm::vec4(inverse[2], 0.0f);
			gpu.WorldRow0 = glm::vec4(input.ObjectToWorld[0][0], input.ObjectToWorld[1][0], input.ObjectToWorld[2][0], input.ObjectToWorld[3][0]);
			gpu.WorldRow1 = glm::vec4(input.ObjectToWorld[0][1], input.ObjectToWorld[1][1], input.ObjectToWorld[2][1], input.ObjectToWorld[3][1]);
			gpu.WorldRow2 = glm::vec4(input.ObjectToWorld[0][2], input.ObjectToWorld[1][2], input.ObjectToWorld[2][2], input.ObjectToWorld[3][2]);
			gpu.Info = glm::uvec4(blas.VertexBase, blas.IndexBase, input.MaterialID, 0);
			gpu.AlbedoMetal = glm::vec4(input.Albedo, input.Metalness);
			gpu.EmissionRough = glm::vec4(input.Emission, input.Roughness);
		}
		api.UnmapBuffer(*m_TLASInstanceBuffer);

		const uint32_t instanceBytes = count * static_cast<uint32_t>(sizeof(GPUInstance));
		EnsureCapacity(m_InstanceBuffer, instanceBytes);
		m_InstanceBuffer->RT_SetData(cb, gpuInstances.data(), instanceBytes);

		nri::BuildTopLevelAccelerationStructureDesc build = {};
		build.dst = m_TLAS;
		build.instanceNum = count;
		build.instanceBuffer = m_TLASInstanceBuffer;
		build.instanceOffset = 0;
		build.scratchBuffer = m_TLASScratch;
		build.scratchOffset = 0;

		if (nri::CommandBuffer* commands = cb->RT_BeginNRITransfer())
		{
			api.CmdBuildTopLevelAccelerationStructures(*commands, &build, 1);
			nri::GlobalBarrierDesc global = {};
			global.before = { nri::AccessBits::ACCELERATION_STRUCTURE_WRITE, nri::StageBits::ACCELERATION_STRUCTURE };
			global.after = { nri::AccessBits::ACCELERATION_STRUCTURE_READ, nri::StageBits::COMPUTE_SHADER };
			nri::BarrierDesc barrier = {};
			barrier.globals = &global;
			barrier.globalNum = 1;
			api.CmdBarrier(*commands, barrier);
			cb->RT_EndNRISegment();
		}
		m_InstanceCount = count;
	}

}
