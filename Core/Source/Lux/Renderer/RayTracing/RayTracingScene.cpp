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
#include <atomic>
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

		std::atomic<uint32_t> s_TuningBits{ 7u }; // bit 0 compaction, 1 skinned refit, 2 TLAS update
		constexpr uint64_t k_CompactionDelay = 8; // RT_Update calls between a build and reading its compacted sizes back

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
		for (CompactionBatch& batch : m_Compactions)
		{
			Renderer::SubmitResourceFree([pool = batch.Pool, readback = batch.Readback]()
			{
				RHIDevice::API().DestroyQueryPool(pool);
				RHIDevice::API().DestroyBuffer(readback);
			});
		}
		m_Compactions.clear();
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
		m_TLASBuilt = false;
	}

	void RayTracingScene::SetTuning(const Tuning& tuning)
	{
		s_TuningBits = (tuning.CompactBLAS ? 1u : 0u) | (tuning.RefitSkinned ? 2u : 0u) | (tuning.UpdateTLAS ? 4u : 0u);
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
					input.Variant = (static_cast<uint64_t>(proxy->EntityID) * 0x9E3779B97F4A7C15ull) | 1ull;
					input.PoseHash = proxy->SkinPaletteHash;
					input.SkinPalette = proxy->SkinPalette;
				}
				if (Ref<MaterialAsset> material = ResolveMaterial(*proxy, submesh.MaterialIndex))
				{
					input.Albedo = material->GetAlbedoColor();
					input.Metalness = material->GetMetalness();
					input.Roughness = material->GetRoughness();
					const auto& surface = material->GetSurfaceParameters();
					input.Refractive = material->IsTransparent() && surface.Transmission > 0.0f && surface.IOR > 1.001f;
					const glm::vec3 emissionColor = glm::clamp(surface.EmissiveColor, glm::vec3(0.0f), glm::vec3(1.0f));
					input.Emission = glm::pow(emissionColor, glm::vec3(2.2f)) * std::max(material->GetEmission(), 0.0f);
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

	namespace {

		// The vertices of one submesh in the layout the shaders read (position + uv.x, normal + uv.y); skinned when a palette is given.
		void AppendVertices(std::vector<PackedRTVertex>& vertices, const MeshSource& source, const Submesh& submesh, const std::vector<glm::mat4>* palette)
		{
			const auto& sourceVertices = source.GetVertices();
			const auto& influences = source.GetBoneInfluences();
			for (uint32_t v = 0; v < submesh.VertexCount; v++)
			{
				const uint32_t index = submesh.BaseVertex + v;
				if (index >= sourceVertices.size())
				{
					vertices.push_back({ glm::vec4(0, 0, 0, 0), glm::vec4(0, 1, 0, 0) });
					continue;
				}
				glm::vec3 position = sourceVertices[index].Position;
				glm::vec3 normal = sourceVertices[index].Normal;
				if (palette && index < influences.size())
				{
					// Linear blend skinning, as Skinning.glsl: the weighted sum of the palette matrices.
					glm::mat4 blended(0.0f);
					for (int i = 0; i < 4; i++)
					{
						const uint32_t bone = influences[index].BoneInfoIndices[i];
						if (bone < palette->size())
							blended += (*palette)[bone] * influences[index].Weights[i];
					}
					position = glm::vec3(blended * glm::vec4(position, 1.0f));
					normal = glm::normalize(glm::mat3(blended) * normal);
				}
				// w components carry the uv the raster shader uses, (u, 1 - v), for material textures.
				vertices.push_back({ glm::vec4(position, sourceVertices[index].Texcoord.x), glm::vec4(normal, 1.0f - sourceVertices[index].Texcoord.y) });
			}
		}

		nri::BottomLevelGeometryDesc MakeGeometry(nri::Buffer* vertexBuffer, nri::Buffer* indexBuffer, uint32_t vertexBase, uint32_t vertexCount, uint32_t indexBase, uint32_t indexCount)
		{
			nri::BottomLevelGeometryDesc geometry = {};
			geometry.flags = nri::BottomLevelGeometryBits::OPAQUE_GEOMETRY;
			geometry.type = nri::BottomLevelGeometryType::TRIANGLES;
			geometry.triangles.vertexBuffer = vertexBuffer;
			geometry.triangles.vertexOffset = static_cast<uint64_t>(vertexBase) * sizeof(PackedRTVertex);
			geometry.triangles.vertexNum = vertexCount;
			geometry.triangles.vertexStride = sizeof(PackedRTVertex);
			geometry.triangles.vertexFormat = nri::Format::RGB32_SFLOAT;
			geometry.triangles.indexBuffer = indexBuffer;
			geometry.triangles.indexOffset = static_cast<uint64_t>(indexBase) * sizeof(uint32_t);
			geometry.triangles.indexNum = indexCount;
			geometry.triangles.indexType = nri::IndexType::UINT32;
			return geometry;
		}

		void RecordASBarrier(const NRIInterface& api, nri::CommandBuffer* commands)
		{
			// The TLAS build (and the shaders) read what the builds wrote.
			nri::GlobalBarrierDesc global = {};
			global.before = { nri::AccessBits::ACCELERATION_STRUCTURE_WRITE, nri::StageBits::ACCELERATION_STRUCTURE };
			global.after = { nri::AccessBits::ACCELERATION_STRUCTURE_READ,
				nri::StageBits::ACCELERATION_STRUCTURE | nri::StageBits::COMPUTE_SHADER | nri::StageBits::FRAGMENT_SHADER };
			nri::BarrierDesc barrier = {};
			barrier.globals = &global;
			barrier.globalNum = 1;
			api.CmdBarrier(*commands, barrier);
		}

	}

	bool RayTracingScene::RT_Update(Ref<RenderCommandBuffer> cb, const std::vector<InstanceInput>& instances)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!RHIDevice::SupportsRayQuery())
			return false;
		m_UpdateCounter++;

		Tuning tuning;
		const uint32_t bits = s_TuningBits.load();
		tuning.CompactBLAS = (bits & 1u) != 0;
		tuning.RefitSkinned = (bits & 2u) != 0;
		tuning.UpdateTLAS = (bits & 4u) != 0;

		// Which meshes are present decides the geometry; transforms and materials only the TLAS; a skin pose only the skinned BLAS.
		uint64_t geometrySignature = 1469598103934665603ull;
		uint64_t poseSignature = 1469598103934665603ull;
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
			instanceSignature = HashValue(instanceSignature, input.PoseHash);
			instanceSignature = HashValue(instanceSignature, input.ObjectToWorld);
			instanceSignature = HashValue(instanceSignature, input.Albedo);
			instanceSignature = HashValue(instanceSignature, input.Metalness);
			instanceSignature = HashValue(instanceSignature, input.Roughness);
			instanceSignature = HashValue(instanceSignature, input.Emission);
			instanceSignature = HashValue(instanceSignature, input.MaterialID);
			if (input.Variant != 0)
				poseSignature = HashValue(HashValue(HashValue(poseSignature, input.Source.Raw()), input.Submesh), input.PoseHash);
		}

		const bool geometryChanged = geometrySignature != m_GeometrySignature;
		const bool poseChanged = poseSignature != m_PoseSignature;
		const bool instancesChanged = instanceSignature != m_InstanceSignature;

		// Compaction swaps BLASes a few frames after their build; the TLAS then points at the new ones.
		const bool compacted = !geometryChanged && ProcessCompaction(cb);
		if (!geometryChanged && !instancesChanged && !compacted)
			return false;

		if (instances.empty())
		{
			ReleaseGeometry();
			ReleaseTLAS();
			m_InstanceCount = 0;
			m_GeometrySignature = geometrySignature;
			m_InstanceSignature = instanceSignature;
			m_PoseSignature = poseSignature;
			return true;
		}

		bool rebuiltBLAS = geometryChanged;
		if (geometryChanged)
			BuildGeometry(cb, instances);
		else if (poseChanged)
		{
			if (tuning.RefitSkinned)
				RefitSkinned(cb, instances);
			else
			{
				BuildGeometry(cb, instances); // A/B: the old behaviour, every BLAS rebuilt for a new pose
				rebuiltBLAS = true;
			}
		}
		BuildTLAS(cb, instances, rebuiltBLAS, compacted, tuning);

		m_GeometrySignature = geometrySignature;
		m_InstanceSignature = instanceSignature;
		m_PoseSignature = poseSignature;
		return instancesChanged || geometryChanged; // a compaction alone changes no image
	}

	void RayTracingScene::BuildGeometry(Ref<RenderCommandBuffer> cb, const std::vector<InstanceInput>& instances)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const NRIInterface& api = RHIDevice::API();
		const Tuning tuning = [&]
		{
			Tuning t;
			t.CompactBLAS = (s_TuningBits.load() & 1u) != 0;
			return t;
		}();
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
		struct Pending { SubmeshKey Key; uint32_t VertexBase; uint32_t VertexCount; uint32_t IndexBase; uint32_t IndexCount; uint64_t PoseHash; };
		std::vector<Pending> pending;
		m_TriangleCount = 0;
		for (const SubmeshKey& key : keys)
		{
			const Submesh& submesh = key.Source->GetSubmeshes()[key.Submesh];
			const auto& sourceIndices = key.Source->GetIndices();

			Pending entry = { key, static_cast<uint32_t>(vertices.size()), submesh.VertexCount, static_cast<uint32_t>(indices.size()), submesh.IndexCount, 0 };
			// The palette of a skinned instance (the first one with this key).
			const std::vector<glm::mat4>* palette = nullptr;
			if (key.Variant != 0)
			{
				for (const InstanceInput& input : instances)
				{
					if (KeyOf(input) == key)
					{
						palette = &input.SkinPalette;
						entry.PoseHash = input.PoseHash;
						break;
					}
				}
			}
			AppendVertices(vertices, *key.Source, submesh, palette);
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

		// One BLAS (one geometry) per submesh. Skinned ones can be refit; static ones compacted.
		std::vector<nri::BottomLevelGeometryDesc> geometries(pending.size());
		std::vector<nri::BuildBottomLevelAccelerationStructureDesc> builds(pending.size());
		std::vector<nri::AccelerationStructure*> structures(pending.size(), nullptr);
		std::vector<uint64_t> scratchOffsets(pending.size(), 0);
		uint64_t scratchTotal = 0;
		for (size_t i = 0; i < pending.size(); i++)
		{
			geometries[i] = MakeGeometry(m_VertexBuffer->GetRHIBuffer(), m_IndexBuffer->GetRHIBuffer(), pending[i].VertexBase, pending[i].VertexCount, pending[i].IndexBase, pending[i].IndexCount);

			nri::AccelerationStructureDesc desc = {};
			desc.geometries = &geometries[i];
			desc.geometryOrInstanceNum = 1;
			desc.flags = nri::AccelerationStructureBits::PREFER_FAST_TRACE;
			if (pending[i].Key.Variant != 0)
				desc.flags = desc.flags | nri::AccelerationStructureBits::ALLOW_UPDATE;
			else if (tuning.CompactBLAS)
				desc.flags = desc.flags | nri::AccelerationStructureBits::ALLOW_COMPACTION;
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
		CompactionBatch batch;
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
			record.VertexCount = pending[i].VertexCount;
			record.IndexCount = pending[i].IndexCount;
			record.Updatable = pending[i].Key.Variant != 0;
			record.PoseHash = pending[i].PoseHash;
			m_BLASes[pending[i].Key] = record;

			if (tuning.CompactBLAS && !record.Updatable)
			{
				batch.Keys.push_back(pending[i].Key);
				batch.Sources.push_back(structures[i]);
				batch.Geometries.push_back(geometries[i]);
			}
		}

		if (!validBuilds.empty() && scratch)
		{
			if (nri::CommandBuffer* commands = cb->RT_BeginNRITransfer())
			{
				api.CmdBuildBottomLevelAccelerationStructures(*commands, validBuilds.data(), static_cast<uint32_t>(validBuilds.size()));
				RecordASBarrier(api, commands);

				// Ask the GPU how small each static BLAS could be; the answer is read a few frames later (ProcessCompaction).
				if (!batch.Keys.empty())
				{
					const uint32_t count = static_cast<uint32_t>(batch.Keys.size());
					if (api.CreateQueryPool(RHIDevice::Get(), { nri::QueryType::ACCELERATION_STRUCTURE_COMPACTED_SIZE, count }, batch.Pool) == nri::Result::SUCCESS)
					{
						batch.Readback = CreateRawBuffer(count * sizeof(uint64_t), nri::BufferUsageBits::NONE, nri::MemoryLocation::HOST_READBACK, "RT BLAS Compacted Sizes");
						if (batch.Readback)
						{
							api.CmdResetQueries(*commands, *batch.Pool, 0, count);
							api.CmdWriteAccelerationStructureSizes(*commands, batch.Sources.data(), count, *batch.Pool, 0);
							api.CmdCopyQueries(*commands, *batch.Pool, 0, count, *batch.Readback, 0);
							batch.IssuedAt = m_UpdateCounter;
							batch.GeometrySignature = 0; // set by RT_Update's caller flow below
							m_Compactions.push_back(std::move(batch));
						}
						else
						{
							api.DestroyQueryPool(batch.Pool);
						}
					}
				}
				cb->RT_EndNRISegment();
			}
		}

		if (scratch)
		{
			Renderer::SubmitResourceFree([scratch]() { RHIDevice::API().DestroyBuffer(scratch); });
		}
	}

	void RayTracingScene::RefitSkinned(Ref<RenderCommandBuffer> cb, const std::vector<InstanceInput>& instances)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const NRIInterface& api = RHIDevice::API();

		struct Refit { SubmeshKey Key; const InstanceInput* Input; };
		std::vector<Refit> refits;
		for (const InstanceInput& input : instances)
		{
			if (input.Variant == 0)
				continue;
			const SubmeshKey key = KeyOf(input);
			auto it = m_BLASes.find(key);
			if (it == m_BLASes.end() || !it->second.Updatable || !it->second.Structure || it->second.PoseHash == input.PoseHash)
				continue;
			if (std::find_if(refits.begin(), refits.end(), [&](const Refit& r) { return r.Key == key; }) == refits.end())
				refits.push_back({ key, &input });
		}
		if (refits.empty())
			return;

		std::vector<nri::BottomLevelGeometryDesc> geometries(refits.size());
		std::vector<nri::BuildBottomLevelAccelerationStructureDesc> builds(refits.size());
		std::vector<uint64_t> scratchOffsets(refits.size(), 0);
		uint64_t scratchTotal = 0;
		for (size_t i = 0; i < refits.size(); i++)
		{
			BLASRecord& record = m_BLASes[refits[i].Key];
			const Submesh& submesh = refits[i].Key.Source->GetSubmeshes()[refits[i].Key.Submesh];
			std::vector<PackedRTVertex> vertices;
			AppendVertices(vertices, *refits[i].Key.Source, submesh, &refits[i].Input->SkinPalette);
			m_VertexBuffer->RT_SetData(cb, vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(PackedRTVertex)), static_cast<uint32_t>(record.VertexBase * sizeof(PackedRTVertex)));
			geometries[i] = MakeGeometry(m_VertexBuffer->GetRHIBuffer(), m_IndexBuffer->GetRHIBuffer(), record.VertexBase, record.VertexCount, record.IndexBase, record.IndexCount);
			scratchOffsets[i] = scratchTotal;
			scratchTotal += AlignUp(api.GetAccelerationStructureUpdateScratchBufferSize(*record.Structure), k_ScratchAlignment);
		}

		nri::Buffer* scratch = CreateRawBuffer(scratchTotal, nri::BufferUsageBits::SCRATCH, nri::MemoryLocation::DEVICE, "RT BLAS Update Scratch");
		if (!scratch)
			return;
		for (size_t i = 0; i < refits.size(); i++)
		{
			BLASRecord& record = m_BLASes[refits[i].Key];
			builds[i] = {};
			builds[i].dst = record.Structure;
			builds[i].src = record.Structure; // an update in place
			builds[i].geometries = &geometries[i];
			builds[i].geometryNum = 1;
			builds[i].scratchBuffer = scratch;
			builds[i].scratchOffset = scratchOffsets[i];
			record.PoseHash = refits[i].Input->PoseHash;
		}
		if (nri::CommandBuffer* commands = cb->RT_BeginNRITransfer())
		{
			api.CmdBuildBottomLevelAccelerationStructures(*commands, builds.data(), static_cast<uint32_t>(builds.size()));
			RecordASBarrier(api, commands);
			cb->RT_EndNRISegment();
		}
		m_BLASRefits += static_cast<uint32_t>(refits.size());
		if (m_BLASRefits == static_cast<uint32_t>(refits.size()))
			LUX_CORE_INFO_TAG("Renderer", "[RT] skinned BLASes are refit in place ({} this frame)", refits.size());
		Renderer::SubmitResourceFree([scratch]() { RHIDevice::API().DestroyBuffer(scratch); });
	}

	bool RayTracingScene::ProcessCompaction(Ref<RenderCommandBuffer> cb)
	{
		if (m_Compactions.empty())
			return false;
		const NRIInterface& api = RHIDevice::API();
		bool swapped = false;
		for (size_t b = 0; b < m_Compactions.size();)
		{
			CompactionBatch& batch = m_Compactions[b];
			if (m_UpdateCounter < batch.IssuedAt + k_CompactionDelay)
			{
				b++;
				continue;
			}

			// The GPU wrote the sizes at least k_CompactionDelay updates ago, so mapping does not wait.
			std::vector<uint64_t> sizes(batch.Keys.size(), 0);
			if (const void* mapped = api.MapBuffer(*batch.Readback, 0, sizes.size() * sizeof(uint64_t)))
			{
				std::memcpy(sizes.data(), mapped, sizes.size() * sizeof(uint64_t));
				api.UnmapBuffer(*batch.Readback);
			}

			std::vector<nri::AccelerationStructure*> retired;
			uint64_t bytesBefore = 0, bytesAfter = 0;
			if (nri::CommandBuffer* commands = cb->RT_BeginNRITransfer())
			{
				for (size_t i = 0; i < batch.Keys.size(); i++)
				{
					auto it = m_BLASes.find(batch.Keys[i]);
					if (it == m_BLASes.end() || it->second.Structure != batch.Sources[i] || sizes[i] == 0)
						continue; // rebuilt or freed since
					nri::MemoryDesc memory = {};
					api.GetAccelerationStructureMemoryDesc(*batch.Sources[i], nri::MemoryLocation::DEVICE, memory);
					if (sizes[i] >= memory.size)
						continue; // nothing to gain
					nri::AccelerationStructureDesc desc = {};
					desc.optimizedSize = sizes[i];
					desc.geometries = &batch.Geometries[i];
					desc.geometryOrInstanceNum = 1;
					desc.flags = nri::AccelerationStructureBits::PREFER_FAST_TRACE;
					desc.type = nri::AccelerationStructureType::BOTTOM_LEVEL;
					nri::AccelerationStructure* compact = nullptr;
					if (api.CreateCommittedAccelerationStructure(RHIDevice::Get(), nri::MemoryLocation::DEVICE, 0.0f, desc, compact) != nri::Result::SUCCESS)
						continue;
					api.CmdCopyAccelerationStructure(*commands, *compact, *batch.Sources[i], nri::CopyMode::COMPACT);
					it->second.Structure = compact;
					retired.push_back(batch.Sources[i]);
					bytesBefore += memory.size;
					bytesAfter += sizes[i];
					swapped = true;
				}
				if (!retired.empty())
					RecordASBarrier(api, commands);
				cb->RT_EndNRISegment();
			}
			if (!retired.empty())
			{
				m_CompactedBLASes += static_cast<uint32_t>(retired.size());
				LUX_CORE_INFO_TAG("Renderer", "[RT] compacted {} BLASes: {:.2f} MiB -> {:.2f} MiB", retired.size(), bytesBefore / 1048576.0, bytesAfter / 1048576.0);
				Renderer::SubmitResourceFree([retired = std::move(retired)]()
				{
					for (nri::AccelerationStructure* structure : retired)
						RHIDevice::API().DestroyAccelerationStructure(structure);
				});
			}
			Renderer::SubmitResourceFree([pool = batch.Pool, readback = batch.Readback]()
			{
				RHIDevice::API().DestroyQueryPool(pool);
				RHIDevice::API().DestroyBuffer(readback);
			});
			m_Compactions.erase(m_Compactions.begin() + b);
		}
		return swapped;
	}

	void RayTracingScene::BuildTLAS(Ref<RenderCommandBuffer> cb, const std::vector<InstanceInput>& instances, bool geometryRebuilt, bool forceRebuild, const Tuning& tuning)
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
			desc.flags = nri::AccelerationStructureBits::PREFER_FAST_TRACE | nri::AccelerationStructureBits::ALLOW_UPDATE;
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
			m_TLASBuilt = false;
			m_TLASInstanceBuffer = CreateRawBuffer(sizeof(nri::TopLevelInstance) * capacity, nri::BufferUsageBits::ACCELERATION_STRUCTURE_BUILD_INPUT, nri::MemoryLocation::HOST_UPLOAD, "RT TLAS Instances");
			const uint64_t scratchSize = std::max(api.GetAccelerationStructureBuildScratchBufferSize(*m_TLAS), api.GetAccelerationStructureUpdateScratchBufferSize(*m_TLAS));
			m_TLASScratch = CreateRawBuffer(scratchSize, nri::BufferUsageBits::SCRATCH, nri::MemoryLocation::DEVICE, "RT TLAS Scratch");
		}
		if (!m_TLAS || !m_TLASInstanceBuffer || !m_TLASScratch)
			return;

		// TLAS instances and the shader-side instance table, in the same order (instanceId = index).
		std::vector<GPUInstance> gpuInstances(count);
		auto* mapped = static_cast<nri::TopLevelInstance*>(api.MapBuffer(*m_TLASInstanceBuffer, 0, sizeof(nri::TopLevelInstance) * m_TLASCapacity));
		if (!mapped)
			return;
		std::memset(mapped, 0, sizeof(nri::TopLevelInstance) * m_TLASCapacity);
		uint32_t emitterOrdinal = 0;
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
			gpu.Info = glm::uvec4(blas.VertexBase, blas.IndexBase, input.MaterialID, input.IsEmissive() ? emitterOrdinal++ : 0xffffffffu);
			gpu.AlbedoMetal = glm::vec4(input.Albedo, input.Metalness);
			gpu.EmissionRough = glm::vec4(input.Emission, input.Roughness);
		}
		api.UnmapBuffer(*m_TLASInstanceBuffer);

		const uint32_t instanceBytes = count * static_cast<uint32_t>(sizeof(GPUInstance));
		EnsureCapacity(m_InstanceBuffer, instanceBytes);
		m_InstanceBuffer->RT_SetData(cb, gpuInstances.data(), instanceBytes);

		// A refit keeps the tree and only moves its bounds: valid while the instance count and every BLAS stay the same.
		const bool update = tuning.UpdateTLAS && m_TLASBuilt && !geometryRebuilt && !forceRebuild && count == m_InstanceCount;

		nri::BuildTopLevelAccelerationStructureDesc build = {};
		build.dst = m_TLAS;
		build.src = update ? m_TLAS : nullptr;
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
			auto traceStages = nri::StageBits::COMPUTE_SHADER | nri::StageBits::FRAGMENT_SHADER;
			if (RHIDevice::SupportsShaderExecutionReordering())
				traceStages |= nri::StageBits::RAY_TRACING_SHADERS;
			global.after = { nri::AccessBits::ACCELERATION_STRUCTURE_READ, traceStages };
			nri::BarrierDesc barrier = {};
			barrier.globals = &global;
			barrier.globalNum = 1;
			api.CmdBarrier(*commands, barrier);
			cb->RT_EndNRISegment();
		}
		if (update)
		{
			if (m_TLASUpdates++ == 0)
				LUX_CORE_INFO_TAG("Renderer", "[RT] the TLAS is refit in place for transform-only changes");
		}
		else
			m_TLASRebuilds++;
		m_TLASBuilt = true;
		m_InstanceCount = count;
	}

}
