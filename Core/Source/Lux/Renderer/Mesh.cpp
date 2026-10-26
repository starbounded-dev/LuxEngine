// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h" 
#include "Mesh.h"

#include "Lux/Core/Application.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Math/Math.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/UploadContext.h"
#include "Lux/Renderer/RHI/RHIShader.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Project/Project.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Utilities/FileSystem.h"

//#include "Jolt/Core/HashCombine.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include "imgui/imgui.h"

#include "meshoptimizer/meshoptimizer.h"

#include <array>
#include <cstring>
#include <map>
#include <mutex>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>
#include <unordered_map>

namespace Lux
{

#define MESH_DEBUG_LOG 0
#if MESH_DEBUG_LOG
#define LUX_MESH_LOG(...) LUX_CORE_TRACE_TAG("Mesh", __VA_ARGS__)
#define LUX_MESH_ERROR(...) LUX_CORE_ERROR_TAG("Mesh", __VA_ARGS__)
#else
#define LUX_MESH_LOG(...)
#define LUX_MESH_ERROR(...)
#endif

	namespace
	{
		// Generated LODs (meshoptimizer, after Lucky Engine's MeshProcessor). Each level halves the one
		// before it within an error of 2% of the submesh's extent. Normals are weighted into the error, so
		// shading across hard edges survives, and the open border is locked, so submeshes meeting at a
		// material boundary do not crack apart. A level is a new index list over the submesh's own
		// vertices; no vertices are added.
		constexpr uint32_t k_MaxGeneratedLODs = 3;
		constexpr float k_LODTargetRatio = 0.5f;
		constexpr float k_LODTargetError = 0.02f;
		constexpr float k_LODNormalWeight = 0.5f;
		// A level must drop at least this share of the previous level's indices to be worth drawing.
		constexpr float k_LODMinimumSaving = 0.08f;
		// Submeshes smaller than this are drawn as they are.
		constexpr uint32_t k_LODMinimumVertexCount = 64;
		constexpr uint32_t k_LODMinimumIndexCount = 300;
		// LOD n is drawn beyond this many bounding-sphere radii from the camera (SelectStaticMeshLOD).
		constexpr std::array<float, k_MaxGeneratedLODs> k_LODDistanceMultipliers = { 24.0f, 48.0f, 96.0f };

		static glm::vec3 NormalizeOrFallback(const glm::vec3& value, const glm::vec3& fallback)
		{
			const float lengthSq = glm::dot(value, value);
			return lengthSq > 0.000001f ? value * glm::inversesqrt(lengthSq) : fallback;
		}

		// One coarser level of `submesh` simplified from `previous` (indices local to the submesh's
		// vertices). False when it would not save enough to be worth a level.
		static bool SimplifySubmeshLOD(const std::vector<Vertex>& vertices, const Submesh& submesh,
			const std::vector<uint32_t>& previous, std::vector<uint32_t>& simplified)
		{
			const Vertex& first = vertices[submesh.BaseVertex];
			const float normalWeights[3] = { k_LODNormalWeight, k_LODNormalWeight, k_LODNormalWeight };
			const size_t targetIndexCount = static_cast<size_t>(static_cast<float>(previous.size()) * k_LODTargetRatio) / 3 * 3;

			simplified.resize(previous.size());
			const size_t indexCount = meshopt_simplifyWithAttributes(simplified.data(), previous.data(), previous.size(),
				&first.Position.x, submesh.VertexCount, sizeof(Vertex),
				&first.Normal.x, sizeof(Vertex), normalWeights, 3,
				nullptr, targetIndexCount, k_LODTargetError, meshopt_SimplifyLockBorder, nullptr);
			simplified.resize(indexCount);

			if (indexCount < 3 || static_cast<float>(indexCount) > static_cast<float>(previous.size()) * (1.0f - k_LODMinimumSaving))
				return false;

			meshopt_optimizeVertexCache(simplified.data(), simplified.data(), indexCount, submesh.VertexCount);
			return true;
		}

		// An explicit submesh selection also draws the other placements of each selected submesh: for
		// every further node that places a mesh, the importer appends a copy over the same vertex range
		// after the original (AssimpMeshImporter::TraverseNodes). Without this, a selection saved
		// before placements existed would show one copy of an instanced mesh.
		static std::vector<uint32_t> AddSubmeshPlacements(const std::vector<uint32_t>& selection, const std::vector<Submesh>& submeshes)
		{
			std::vector<uint32_t> result = selection;
			for (uint32_t candidate = 0; candidate < (uint32_t)submeshes.size(); candidate++)
			{
				if (std::find(result.begin(), result.end(), candidate) != result.end())
					continue;

				const Submesh& placement = submeshes[candidate];
				const bool placesSelected = std::any_of(selection.begin(), selection.end(), [&](uint32_t selected)
				{
					if (selected >= candidate)
						return false;
					const Submesh& original = submeshes[selected];
					// Not BaseIndex: a mirrored placement indexes a reversed copy of the range.
					return original.BaseVertex == placement.BaseVertex && original.VertexCount == placement.VertexCount
						&& original.IndexCount == placement.IndexCount;
				});
				if (placesSelected)
					result.push_back(candidate);
			}
			return result;
		}

		// Generated-LOD cache (Cache/Geometry/<stem>-<path hash>-<key>.lgeo). The key covers the geometry and
		// every LOD setting, so a changed source file, importer or setting misses instead of reading stale LODs.
		constexpr std::array<char, 4> k_GeometryCacheMagic = { 'L', 'X', 'G', 'C' };
		constexpr uint32_t k_GeometryCacheVersion = 1;

		struct GeometryCacheHeader
		{
			std::array<char, 4> Magic = k_GeometryCacheMagic;
			uint32_t Version = k_GeometryCacheVersion;
			uint64_t Key = 0;
			uint32_t SubmeshCount = 0;
			uint32_t LODCount = 0;      // GeometryCacheLOD entries that follow, every submesh's levels
			uint64_t TriangleCount = 0; // generated triangles, appended after the source indices
		};

		struct GeometryCacheLOD
		{
			uint32_t SubmeshIndex = 0;
			uint32_t BaseIndex = 0;
			uint32_t IndexCount = 0;
			float DistanceMultiplier = 0.0f;
		};

		// 64-bit FNV-style hash fed a word at a time: a cache key over tens of megabytes of vertices
		// has to cost far less than the LOD generation it saves.
		class GeometryHasher
		{
		public:
			void Add(const void* data, size_t size)
			{
				const uint8_t* bytes = static_cast<const uint8_t*>(data);
				size_t offset = 0;
				for (; offset + sizeof(uint64_t) <= size; offset += sizeof(uint64_t))
				{
					uint64_t word;
					std::memcpy(&word, bytes + offset, sizeof(word));
					Mix(word);
				}
				uint64_t tail = 0;
				std::memcpy(&tail, bytes + offset, size - offset);
				Mix(tail ^ (static_cast<uint64_t>(size) << 56));
			}

			uint64_t Get() const { return m_Hash; }

		private:
			void Mix(uint64_t word)
			{
				m_Hash = (m_Hash ^ (word * 0x9E3779B97F4A7C15ull)) * 0x100000001B3ull;
				m_Hash ^= m_Hash >> 29;
			}

			uint64_t m_Hash = 14695981039346656037ull;
		};

		static uint64_t GetGeometryCacheKey(const std::vector<Vertex>& vertices, const std::vector<Index>& indices, const std::vector<Submesh>& submeshes)
		{
			GeometryHasher hasher;
			hasher.Add(vertices.data(), vertices.size() * sizeof(Vertex));
			hasher.Add(indices.data(), indices.size() * sizeof(Index));
			for (const Submesh& submesh : submeshes)
			{
				const uint32_t range[5] = { submesh.BaseVertex, submesh.BaseIndex, submesh.VertexCount, submesh.IndexCount, submesh.IsRigged ? 1u : 0u };
				hasher.Add(range, sizeof(range));
			}
			const float settings[] = { static_cast<float>(k_MaxGeneratedLODs), k_LODTargetRatio, k_LODTargetError, k_LODNormalWeight,
				k_LODMinimumSaving, static_cast<float>(k_LODMinimumVertexCount), static_cast<float>(k_LODMinimumIndexCount),
				k_LODDistanceMultipliers[0], k_LODDistanceMultipliers[1], k_LODDistanceMultipliers[2], static_cast<float>(MESHOPTIMIZER_VERSION) };
			hasher.Add(settings, sizeof(settings));
			return hasher.Get();
		}

		// MeshSource::SetGeometryCacheDirectory; set on the main thread, read by asset loader threads.
		std::mutex s_GeometryCacheDirectoryMutex;
		std::filesystem::path s_GeometryCacheDirectory;

		static std::filesystem::path GetGeometryCacheDirectory()
		{
			std::scoped_lock lock(s_GeometryCacheDirectoryMutex);
			return s_GeometryCacheDirectory;
		}

		// The cache file for `sourcePath`, and the prefix every cache file of that source shares (so
		// a regenerated file replaces the ones from earlier versions of the source).
		static std::filesystem::path GetGeometryCachePath(const std::filesystem::path& directory, const std::string& sourcePath, uint64_t key, std::string& outPrefix)
		{
			GeometryHasher pathHasher;
			pathHasher.Add(sourcePath.data(), sourcePath.size());
			outPrefix = std::format("{}-{:08x}-", std::filesystem::path(sourcePath).stem().string(), static_cast<uint32_t>(pathHasher.Get()));
			return directory / std::format("{}{:016x}.lgeo", outPrefix, key);
		}

		// Reads the LODs cached for `key` into `outLODs`, appending their triangles to `renderIndices`.
		// Anything that does not fit this geometry exactly (truncated, another mesh or version, a range or
		// index out of bounds) is a miss that leaves both untouched, and the caller regenerates the file.
		static bool TryReadGeometryCache(const std::filesystem::path& path, uint64_t key, const std::vector<Submesh>& submeshes,
			std::vector<std::vector<SubmeshLOD>>& outLODs, std::vector<Index>& renderIndices)
		{
			if (!FileSystem::Exists(path))
				return false;

			Buffer data = FileSystem::ReadBytes(path);
			const size_t sourceTriangleCount = renderIndices.size();
			auto reject = [&]()
			{
				LUX_CORE_WARN_TAG("Mesh", "Geometry cache '{}' does not match its mesh; regenerating it", path.string());
				renderIndices.resize(sourceTriangleCount);
				for (std::vector<SubmeshLOD>& lods : outLODs)
					lods.clear();
				data.Release();
				return false;
			};

			GeometryCacheHeader header;
			if (data.Size < sizeof(header))
				return reject();
			std::memcpy(&header, data.Data, sizeof(header));
			if (header.Magic != k_GeometryCacheMagic || header.Version != k_GeometryCacheVersion || header.Key != key
				|| header.SubmeshCount != submeshes.size() || header.TriangleCount > data.Size / sizeof(Index)
				|| data.Size != sizeof(header) + uint64_t(header.LODCount) * sizeof(GeometryCacheLOD) + header.TriangleCount * sizeof(Index))
				return reject();

			const uint8_t* cursor = static_cast<const uint8_t*>(data.Data) + sizeof(header);
			std::vector<GeometryCacheLOD> entries(header.LODCount);
			std::memcpy(entries.data(), cursor, entries.size() * sizeof(GeometryCacheLOD));
			cursor += entries.size() * sizeof(GeometryCacheLOD);
			renderIndices.resize(sourceTriangleCount + header.TriangleCount);
			std::memcpy(renderIndices.data() + sourceTriangleCount, cursor, header.TriangleCount * sizeof(Index));

			for (const GeometryCacheLOD& entry : entries)
			{
				if (entry.SubmeshIndex >= submeshes.size() || entry.BaseIndex % 3 != 0 || entry.IndexCount % 3 != 0
					|| uint64_t(entry.BaseIndex / 3) + entry.IndexCount / 3 > renderIndices.size())
					return reject();

				const Submesh& submesh = submeshes[entry.SubmeshIndex];
				for (uint32_t triangle = entry.BaseIndex / 3; triangle < entry.BaseIndex / 3 + entry.IndexCount / 3; triangle++)
				{
					const Index& indices = renderIndices[triangle];
					if (indices.V1 >= submesh.VertexCount || indices.V2 >= submesh.VertexCount || indices.V3 >= submesh.VertexCount)
						return reject();
				}
				outLODs[entry.SubmeshIndex].push_back({ submesh.BaseVertex, entry.BaseIndex, entry.IndexCount, submesh.VertexCount, entry.DistanceMultiplier });
			}

			data.Release();
			return true;
		}

		// Writes through a temporary file, so an interrupted write never leaves a cache file that reads as
		// valid, then removes this source's cache files from earlier versions of it.
		static void WriteGeometryCache(const std::filesystem::path& path, const std::string& prefix, uint64_t key,
			const std::vector<std::vector<SubmeshLOD>>& lods, const std::vector<Index>& renderIndices, size_t sourceTriangleCount)
		{
			std::vector<GeometryCacheLOD> entries;
			for (uint32_t submeshIndex = 0; submeshIndex < (uint32_t)lods.size(); submeshIndex++)
			{
				for (const SubmeshLOD& lod : lods[submeshIndex])
					entries.push_back({ submeshIndex, lod.BaseIndex, lod.IndexCount, lod.DistanceMultiplier });
			}

			GeometryCacheHeader header;
			header.Key = key;
			header.SubmeshCount = (uint32_t)lods.size();
			header.LODCount = (uint32_t)entries.size();
			header.TriangleCount = renderIndices.size() - sourceTriangleCount;

			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			// Per thread: two loader threads can build the same mesh at once.
			const std::filesystem::path temporary = path.string() + std::format(".{}.tmp", std::hash<std::thread::id>{}(std::this_thread::get_id()));
			{
				std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
				stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
				stream.write(reinterpret_cast<const char*>(entries.data()), static_cast<std::streamsize>(entries.size() * sizeof(GeometryCacheLOD)));
				stream.write(reinterpret_cast<const char*>(renderIndices.data() + sourceTriangleCount), static_cast<std::streamsize>(header.TriangleCount * sizeof(Index)));
				stream.close();
				if (!stream)
				{
					LUX_CORE_ERROR_TAG("Mesh", "Failed to write the geometry cache {}", temporary.string());
					std::filesystem::remove(temporary, error);
					return;
				}
			}
			if (!FileSystem::ReplaceFileAtomically(temporary, path))
			{
				std::filesystem::remove(temporary, error);
				return;
			}

			for (const auto& entry : std::filesystem::directory_iterator(path.parent_path(), error))
			{
				const std::string filename = entry.path().filename().string();
				if (entry.path() != path && entry.path().extension() == ".lgeo" && filename.starts_with(prefix))
					std::filesystem::remove(entry.path(), error);
			}
		}

		// Split one submesh LOD range into meshlets (<= 64 vertices, <= 124
		// triangles each) with a bounding sphere and a backface cone per meshlet
		// for task-shader culling.
		static void BuildSubmeshLODMeshlets(
			const std::vector<Vertex>& renderVertices,
			const std::vector<Index>& renderIndices,
			SubmeshLOD& lod,
			std::vector<MeshletDesc>& meshlets,
			std::vector<uint32_t>& meshletVertices,
			std::vector<uint32_t>& meshletTriangles)
		{
			constexpr uint32_t MaxMeshletVertices = 64;
			constexpr uint32_t MaxMeshletTriangles = 124;

			lod.MeshletOffset = (uint32_t)meshlets.size();
			lod.MeshletCount = 0;

			const uint32_t firstTriangle = lod.BaseIndex / 3u;
			const uint32_t triangleCount = lod.IndexCount / 3u;
			if (triangleCount == 0 || firstTriangle + triangleCount > renderIndices.size())
				return;

			MeshletDesc current{};
			current.VertexOffset = (uint32_t)meshletVertices.size();
			current.TriangleOffset = (uint32_t)meshletTriangles.size();

			// Local vertex index (relative to lod.BaseVertex) -> slot in the current meshlet.
			std::unordered_map<uint32_t, uint8_t> vertexSlots;
			vertexSlots.reserve(MaxMeshletVertices);

			glm::vec3 coneAxisAccum{ 0.0f };
			std::vector<glm::vec3> triangleNormals;
			triangleNormals.reserve(MaxMeshletTriangles);

			auto flushMeshlet = [&]()
			{
				if (current.TriangleCount == 0)
					return;

				// Bounding sphere over the meshlet's vertices (mesh-local space).
				glm::vec3 center{ 0.0f };
				for (uint32_t i = 0; i < current.VertexCount; i++)
					center += renderVertices[meshletVertices[current.VertexOffset + i]].Position;
				center /= (float)current.VertexCount;

				float radiusSq = 0.0f;
				for (uint32_t i = 0; i < current.VertexCount; i++)
				{
					const glm::vec3 toVertex = renderVertices[meshletVertices[current.VertexOffset + i]].Position - center;
					radiusSq = glm::max(radiusSq, glm::dot(toVertex, toVertex));
				}
				current.BoundsSphere = glm::vec4(center, glm::sqrt(radiusSq));

				// Backface cone: axis = mean triangle normal; cutoff from the widest
				// deviation. cutoff >= 1 disables the test (spread too wide or
				// degenerate triangles present).
				const glm::vec3 axis = NormalizeOrFallback(coneAxisAccum, { 0.0f, 0.0f, 0.0f });
				float minAxisDot = 1.0f;
				bool coneValid = glm::dot(axis, axis) > 0.5f && triangleNormals.size() == current.TriangleCount;
				for (const glm::vec3& normal : triangleNormals)
					minAxisDot = glm::min(minAxisDot, glm::dot(axis, normal));
				if (coneValid && minAxisDot > 0.0f)
					current.Cone = glm::vec4(axis, glm::sqrt(glm::max(0.0f, 1.0f - minAxisDot * minAxisDot)));
				else
					current.Cone = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

				meshlets.push_back(current);
				lod.MeshletCount++;

				current = MeshletDesc{};
				current.VertexOffset = (uint32_t)meshletVertices.size();
				current.TriangleOffset = (uint32_t)meshletTriangles.size();
				vertexSlots.clear();
				coneAxisAccum = glm::vec3(0.0f);
				triangleNormals.clear();
			};

			for (uint32_t triangle = 0; triangle < triangleCount; triangle++)
			{
				const Index& triangleIndices = renderIndices[firstTriangle + triangle];
				const uint32_t localIndices[3] = { triangleIndices.V1, triangleIndices.V2, triangleIndices.V3 };
				if (localIndices[0] >= lod.VertexCount || localIndices[1] >= lod.VertexCount || localIndices[2] >= lod.VertexCount)
					continue;

				const uint32_t newVertexCount =
					(uint32_t)(!vertexSlots.contains(localIndices[0])) +
					(uint32_t)(!vertexSlots.contains(localIndices[1])) +
					(uint32_t)(!vertexSlots.contains(localIndices[2]));

				if (current.VertexCount + newVertexCount > MaxMeshletVertices || current.TriangleCount + 1 > MaxMeshletTriangles)
					flushMeshlet();

				uint8_t slots[3];
				for (int corner = 0; corner < 3; corner++)
				{
					auto [slotIt, inserted] = vertexSlots.try_emplace(localIndices[corner], (uint8_t)current.VertexCount);
					if (inserted)
					{
						meshletVertices.push_back(lod.BaseVertex + localIndices[corner]);
						current.VertexCount++;
					}
					slots[corner] = slotIt->second;
				}

				meshletTriangles.push_back((uint32_t)slots[0] | ((uint32_t)slots[1] << 8u) | ((uint32_t)slots[2] << 16u));
				current.TriangleCount++;

				const glm::vec3& p0 = renderVertices[lod.BaseVertex + localIndices[0]].Position;
				const glm::vec3& p1 = renderVertices[lod.BaseVertex + localIndices[1]].Position;
				const glm::vec3& p2 = renderVertices[lod.BaseVertex + localIndices[2]].Position;
				const glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);
				if (glm::dot(faceNormal, faceNormal) > 0.000000001f)
				{
					const glm::vec3 normalized = glm::normalize(faceNormal);
					coneAxisAccum += normalized;
					triangleNormals.push_back(normalized);
				}
			}

			flushMeshlet();
		}

		static NRIBuffer CreateMeshletStorageBuffer(const void* data, uint64_t size, const char* debugName)
		{
			if (size == 0)
				return {};

			RHIBufferDesc bufferDesc;
			bufferDesc.Size = size;
			bufferDesc.CanHaveRawViews = true;
			bufferDesc.RestingState = ResourceState::ShaderResource;
			bufferDesc.DebugName = debugName;

			NRIBuffer buffer = NRIBuffer::Create(bufferDesc);
			LUX_CORE_VERIFY(buffer, "Failed to create meshlet buffer \"{}\" ({} bytes)", debugName, size);

			// The record callback runs immediately; reading from stack-local data is safe.
			Renderer::RecordResourceUpload([&](UploadContext& upload)
			{
				upload.WriteBuffer(buffer, data, size);
			});
			return buffer;
		}
	}

	////////////////////////////////////////////////////////
	// MeshSource //////////////////////////////////////////
	////////////////////////////////////////////////////////

	bool MeshSource::s_RetainFullCPUGeometry = true;

	void MeshSource::SetGeometryCacheDirectory(const std::filesystem::path& directory)
	{
		std::scoped_lock lock(s_GeometryCacheDirectoryMutex);
		s_GeometryCacheDirectory = directory;
	}

	void MeshSource::CompactCPUGeometry()
	{
		if (s_RetainFullCPUGeometry || m_Vertices.empty())
			return;

		// Keep positions (physics cooking) and indices (Jolt triangle lists);
		// drop the full vertex array — the GPU already has its copy.
		m_CollisionPositions.resize(m_Vertices.size());
		for (size_t i = 0; i < m_Vertices.size(); i++)
			m_CollisionPositions[i] = m_Vertices[i].Position;

		std::vector<Vertex>().swap(m_Vertices);
	}

	MeshSource::MeshSource(const std::vector<Vertex>& vertices, const std::vector<Index>& indices, const glm::mat4& transform)
		: m_Vertices(vertices), m_Indices(indices)
	{
		// Generate a new asset handle
		Handle = {};

		Submesh submesh;
		submesh.BaseVertex = 0;
		submesh.BaseIndex = 0;
		submesh.MaterialIndex = 0;
		submesh.IndexCount = (uint32_t)indices.size() * 3u;
		submesh.VertexCount = (uint32_t)vertices.size();
		submesh.Transform = transform;
		m_Submeshes.push_back(submesh);

		// Calculate bounding box
		m_BoundingBox.Min = { FLT_MAX, FLT_MAX, FLT_MAX };
		m_BoundingBox.Max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (size_t i = 0; i < m_Vertices.size(); i++)
		{
			const Vertex& vertex = m_Vertices[i];
			m_BoundingBox.Min.x = glm::min(vertex.Position.x, m_BoundingBox.Min.x);
			m_BoundingBox.Min.y = glm::min(vertex.Position.y, m_BoundingBox.Min.y);
			m_BoundingBox.Min.z = glm::min(vertex.Position.z, m_BoundingBox.Min.z);
			m_BoundingBox.Max.x = glm::max(vertex.Position.x, m_BoundingBox.Max.x);
			m_BoundingBox.Max.y = glm::max(vertex.Position.y, m_BoundingBox.Max.y);
			m_BoundingBox.Max.z = glm::max(vertex.Position.z, m_BoundingBox.Max.z);
		}

		BuildRenderGeometry();
		CompactCPUGeometry();
	}

	MeshSource::MeshSource(const std::vector<Vertex>& vertices, const std::vector<Index>& indices, const std::vector<Submesh>& submeshes)
		: m_Vertices(vertices), m_Indices(indices), m_Submeshes(submeshes)
	{
		// Generate a new asset handle
		Handle = {};

		// Calculate bounding box
		m_BoundingBox.Min = { FLT_MAX, FLT_MAX, FLT_MAX };
		m_BoundingBox.Max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (size_t i = 0; i < m_Vertices.size(); i++)
		{
			const Vertex& vertex = m_Vertices[i];
			m_BoundingBox.Min.x = glm::min(vertex.Position.x, m_BoundingBox.Min.x);
			m_BoundingBox.Min.y = glm::min(vertex.Position.y, m_BoundingBox.Min.y);
			m_BoundingBox.Min.z = glm::min(vertex.Position.z, m_BoundingBox.Min.z);
			m_BoundingBox.Max.x = glm::max(vertex.Position.x, m_BoundingBox.Max.x);
			m_BoundingBox.Max.y = glm::max(vertex.Position.y, m_BoundingBox.Max.y);
			m_BoundingBox.Max.z = glm::max(vertex.Position.z, m_BoundingBox.Max.z);
		}

		BuildRenderGeometry();
		CompactCPUGeometry();
	}

	MeshSource::~MeshSource()
	{
	}

	uint32_t MeshSource::GetSubmeshLODCount(uint32_t submeshIndex) const
	{
		if (submeshIndex >= m_SubmeshLODs.size() || m_SubmeshLODs[submeshIndex].empty())
			return 1;

		return (uint32_t)m_SubmeshLODs[submeshIndex].size();
	}

	SubmeshLOD MeshSource::GetSubmeshLOD(uint32_t submeshIndex, uint32_t lodIndex) const
	{
		if (submeshIndex >= m_Submeshes.size())
			return {};

		if (submeshIndex < m_SubmeshLODs.size() && !m_SubmeshLODs[submeshIndex].empty())
		{
			const std::vector<SubmeshLOD>& lods = m_SubmeshLODs[submeshIndex];
			return lods[glm::min(lodIndex, (uint32_t)lods.size() - 1u)];
		}

		// MeshSource that never went through BuildRenderGeometry: LOD 0 is the
		// submesh itself.
		const Submesh& submesh = m_Submeshes[submeshIndex];
		return { submesh.BaseVertex, submesh.BaseIndex, submesh.IndexCount, submesh.VertexCount, 0.0f };
	}

	void MeshSource::BuildRenderGeometry()
	{
		m_SubmeshLODs.clear();
		m_SubmeshLODs.resize(m_Submeshes.size());

		if (m_Vertices.empty() || m_Indices.empty())
		{
			m_VertexBuffer.Reset();
			m_IndexBuffer.Reset();
			return;
		}

		// Generated LODs add indices over the existing vertices, so the vertex buffer is the source data.
		const std::vector<Vertex>& renderVertices = m_Vertices;
		std::vector<Index> renderIndices = m_Indices;

		std::vector<uint32_t> previousIndices, simplifiedIndices;

		// Submeshes over the same vertex/index range are placements of one geometry (the importer adds
		// one per extra node reference): build its LODs and meshlets once, for the first of them.
		std::vector<uint32_t> geometryOwner(m_Submeshes.size());
		{
			std::map<std::array<uint32_t, 4>, uint32_t> ownerByRange;
			for (uint32_t submeshIndex = 0; submeshIndex < (uint32_t)m_Submeshes.size(); submeshIndex++)
			{
				const Submesh& submesh = m_Submeshes[submeshIndex];
				const std::array<uint32_t, 4> range = { submesh.BaseVertex, submesh.BaseIndex, submesh.VertexCount, submesh.IndexCount };
				geometryOwner[submeshIndex] = ownerByRange.try_emplace(range, submeshIndex).first->second;
			}
		}

		// An imported mesh's LODs come from the geometry cache when it holds them for exactly this geometry.
		std::filesystem::path cachePath;
		std::string cachePrefix;
		uint64_t cacheKey = 0;
		if (const std::filesystem::path cacheDirectory = GetGeometryCacheDirectory(); !cacheDirectory.empty() && !m_FilePath.empty())
		{
			LUX_PROFILE_SCOPE("MeshSource::BuildRenderGeometry::CacheKey");
			cacheKey = GetGeometryCacheKey(m_Vertices, m_Indices, m_Submeshes);
			cachePath = GetGeometryCachePath(cacheDirectory, m_FilePath, cacheKey, cachePrefix);
		}
		const size_t sourceTriangleCount = renderIndices.size();
		const bool cachedLODs = !cachePath.empty() && TryReadGeometryCache(cachePath, cacheKey, m_Submeshes, m_SubmeshLODs, renderIndices);

		uint32_t generatedLODCount = 0;
		uint64_t removedIndexCount = 0;
		for (uint32_t submeshIndex = 0; submeshIndex < (uint32_t)m_Submeshes.size() && !cachedLODs; submeshIndex++)
		{
			if (geometryOwner[submeshIndex] != submeshIndex)
				continue;

			const Submesh& submesh = m_Submeshes[submeshIndex];
			std::vector<SubmeshLOD>& lods = m_SubmeshLODs[submeshIndex];
			lods.push_back({
				submesh.BaseVertex,
				submesh.BaseIndex,
				submesh.IndexCount,
				submesh.VertexCount,
				0.0f
			});

			if (submesh.IsRigged || submesh.VertexCount < k_LODMinimumVertexCount || submesh.IndexCount < k_LODMinimumIndexCount
				|| submesh.BaseVertex + submesh.VertexCount > m_Vertices.size() || (submesh.BaseIndex + submesh.IndexCount) / 3 > m_Indices.size())
				continue;

			// The simplifier needs indices local to the submesh's vertices (as the importer writes them).
			const uint32_t* submeshIndices = &m_Indices[submesh.BaseIndex / 3].V1;
			previousIndices.assign(submeshIndices, submeshIndices + submesh.IndexCount);
			if (std::any_of(previousIndices.begin(), previousIndices.end(), [&](uint32_t index) { return index >= submesh.VertexCount; }))
				continue;

			for (uint32_t lodLevel = 0; lodLevel < k_MaxGeneratedLODs; lodLevel++)
			{
				if (!SimplifySubmeshLOD(m_Vertices, submesh, previousIndices, simplifiedIndices))
					break;

				SubmeshLOD lod{};
				lod.BaseVertex = submesh.BaseVertex;
				lod.VertexCount = submesh.VertexCount;
				lod.BaseIndex = static_cast<uint32_t>(renderIndices.size()) * 3u;
				lod.IndexCount = static_cast<uint32_t>(simplifiedIndices.size());
				lod.DistanceMultiplier = k_LODDistanceMultipliers[lodLevel];
				for (size_t index = 0; index < simplifiedIndices.size(); index += 3)
					renderIndices.push_back({ simplifiedIndices[index], simplifiedIndices[index + 1], simplifiedIndices[index + 2] });

				lods.push_back(lod);
				generatedLODCount++;
				removedIndexCount += previousIndices.size() - simplifiedIndices.size();
				std::swap(previousIndices, simplifiedIndices);
			}
		}
		if (!cachedLODs && !cachePath.empty())
			WriteGeometryCache(cachePath, cachePrefix, cacheKey, m_SubmeshLODs, renderIndices, sourceTriangleCount);

		// Meshlet data for the mesh-shader path (built per submesh per LOD so the
		// two systems compose). Only when the GPU supports mesh shaders.
		m_MeshletBuffer.Reset();
		m_MeshletVertexBuffer.Reset();
		m_MeshletTriangleBuffer.Reset();
		m_MeshletSet = nullptr;
		m_MeshletSetLayout = nullptr;
		if (s_BuildMeshlets)
		{
			std::vector<MeshletDesc> meshlets;
			std::vector<uint32_t> meshletVertices;
			std::vector<uint32_t> meshletTriangles;

			for (std::vector<SubmeshLOD>& lods : m_SubmeshLODs)
				for (SubmeshLOD& lod : lods)
					BuildSubmeshLODMeshlets(renderVertices, renderIndices, lod, meshlets, meshletVertices, meshletTriangles);

			if (!meshlets.empty())
			{
				m_MeshletBuffer = CreateMeshletStorageBuffer(meshlets.data(), meshlets.size() * sizeof(MeshletDesc), "MeshletDescs");
				m_MeshletVertexBuffer = CreateMeshletStorageBuffer(meshletVertices.data(), meshletVertices.size() * sizeof(uint32_t), "MeshletVertices");
				m_MeshletTriangleBuffer = CreateMeshletStorageBuffer(meshletTriangles.data(), meshletTriangles.size() * sizeof(uint32_t), "MeshletTriangles");
			}
		}

		for (uint32_t submeshIndex = 0; submeshIndex < (uint32_t)m_Submeshes.size(); submeshIndex++)
		{
			if (geometryOwner[submeshIndex] != submeshIndex)
				m_SubmeshLODs[submeshIndex] = m_SubmeshLODs[geometryOwner[submeshIndex]];
		}

		// The GPU gets the 28-byte PackedVertex; the full Vertex stays on the CPU (physics, picking, caches).
		std::vector<PackedVertex> packedVertices;
		packedVertices.reserve(renderVertices.size());
		for (const Vertex& vertex : renderVertices)
			packedVertices.emplace_back(vertex);
		m_VertexBuffer = VertexBuffer::Create(Buffer(packedVertices.data(), (uint32_t)(packedVertices.size() * sizeof(PackedVertex))));
		m_IndexBuffer = IndexBuffer::Create(Buffer(renderIndices.data(), (uint32_t)(renderIndices.size() * sizeof(Index))));

		if (cachedLODs)
			LUX_CORE_INFO_TAG("Mesh", "MeshSource: LODs for '{}' read from the geometry cache", m_FilePath);
		else if (generatedLODCount > 0)
		{
			LUX_CORE_INFO("MeshSource: generated {} auto LOD level(s) for '{}' ({} fewer indices across selected levels)",
				generatedLODCount,
				m_FilePath.empty() ? "<memory>" : m_FilePath,
				removedIndexCount);
		}
	}

	BoundDescriptorSet MeshSource::RT_GetOrCreateMeshletSet(const RHIShader& shader)
	{
		const nri::PipelineLayout* layout = shader.GetNRIPipelineLayout();
		if (layout && layout == m_MeshletSetLayout)
			return m_MeshletSet ? m_MeshletSet->Bind(0) : BoundDescriptorSet{};

		// Not cached: the buffers may still be on their way.
		if (!layout || !m_MeshletBuffer || !m_MeshletVertexBuffer || !m_MeshletTriangleBuffer || !m_VertexBuffer || !m_VertexBuffer->GetRHIBuffer())
			return {};

		m_MeshletSet = nullptr;
		m_MeshletSetLayout = layout;

		const std::string meshName = m_FilePath.empty() ? std::string("<memory>") : m_FilePath;
		const uint32_t setIndex = shader.GetNRISetIndex(0);
		if (setIndex == RHIShader::k_NoNRISet)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[MeshSource] No meshlet set for '{}': {} has no NRI set 0", meshName, shader.GetName());
			return {};
		}

		// One instance: the meshlet buffers are fixed for the mesh, so the set is written once.
		Ref<DescriptorSetGroup> group = Ref<DescriptorSetGroup>::Create(*shader.GetNRIPipelineLayout(), setIndex, shader.GetNRIPoolDesc(0, 1), 1, 0, "MeshletSet");
		if (!group->IsValid())
			return {};

		// Read-only raw buffers: 0 = meshlet descs, 1 = meshlet vertices, 2 = meshlet triangles,
		// 3 = the shared render vertex buffer.
		const std::array<const NRIBuffer*, 4> buffers = { &m_MeshletBuffer, &m_MeshletVertexBuffer, &m_MeshletTriangleBuffer, &m_VertexBuffer->GetBuffer() };
		const auto& storageBuffers = shader.GetShaderDescriptorSets()[0].StorageBuffers;
		for (uint32_t binding = 0; binding < buffers.size(); binding++)
		{
			const uint32_t rangeIndex = shader.GetNRIRangeIndex(0, binding);
			if (rangeIndex == RHIShader::k_NoNRISet)
				continue;

			const auto storageIt = storageBuffers.find(binding);
			if (storageIt == storageBuffers.end() || !storageIt->second.ReadOnly)
			{
				LUX_CORE_ERROR_TAG("Renderer", "[MeshSource] No meshlet set for '{}': {} binding {} is not a read-only storage buffer", meshName, shader.GetName(), binding);
				return {};
			}

			// GetNRIBufferView logs its own failures.
			const nri::Descriptor* view = GetNRIBufferView(buffers[binding]->Get(), nri::BufferView::BYTE_ADDRESS_BUFFER);
			if (!view)
				return {};
			group->Write(0, rangeIndex, 0, &view, 1);
			group->GetUses(0).AddBuffer(buffers[binding]->GetTracked(), ResourceState::ShaderResource);
		}

		m_MeshletSet = group;
		return m_MeshletSet->Bind(0);
	}

	static std::string LevelToSpaces(uint32_t level)
	{
		std::string result = "";
		for (uint32_t i = 0; i < level; i++)
			result += "--";
		return result;
	}

	void MeshSource::DumpVertexBuffer()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// TODO: Convert to ImGui
		LUX_MESH_LOG("------------------------------------------------------");
		LUX_MESH_LOG("Vertex Buffer Dump");
		LUX_MESH_LOG("Mesh: {0}", m_FilePath);
		for (size_t i = 0; i < m_Vertices.size(); i++)
		{
			auto& vertex = m_Vertices[i];
			LUX_MESH_LOG("Vertex: {0}", i);
			LUX_MESH_LOG("Position: {0}, {1}, {2}", vertex.Position.x, vertex.Position.y, vertex.Position.z);
			LUX_MESH_LOG("Normal: {0}, {1}, {2}", vertex.Normal.x, vertex.Normal.y, vertex.Normal.z);
			LUX_MESH_LOG("Binormal: {0}, {1}, {2}", vertex.Binormal.x, vertex.Binormal.y, vertex.Binormal.z);
			LUX_MESH_LOG("Tangent: {0}, {1}, {2}", vertex.Tangent.x, vertex.Tangent.y, vertex.Tangent.z);
			LUX_MESH_LOG("TexCoord: {0}, {1}", vertex.Texcoord.x, vertex.Texcoord.y);
			LUX_MESH_LOG("--");
		}
		LUX_MESH_LOG("------------------------------------------------------");
	}
#if 0

	// TODO (0x): this is temporary.. and will eventually be replaced with some kind of skeleton retargeting
	bool MeshSource::IsCompatibleSkeleton(const std::string_view animationName, const Skeleton& skeleton) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_Skeleton)
		{
			HZ_CORE_VERIFY(!m_Runtime);
			auto path = Project::GetEditorAssetManager()->GetFileSystemPath(Handle);
			AssimpMeshImporter importer(path);
			return importer.IsCompatibleSkeleton(animationName, skeleton);
		}

		return m_Skeleton->GetBoneNames() == skeleton.GetBoneNames();
	}


	std::vector<std::string> MeshSource::GetAnimationNames() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_AnimationNames;
	}


	const Animation* MeshSource::GetAnimation(const std::string& animationName, const Skeleton& skeleton, bool extractRootMotion, uint32_t rootBoneIndex, const glm::bvec3& rootTranslationMask, const glm::bvec3& rootRotationMask, bool discardRootMotion) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::size_t hash = 0;
		JPH::HashCombine(hash, animationName, extractRootMotion, rootBoneIndex, rootTranslationMask.x, rootTranslationMask.y, rootTranslationMask.z, rootRotationMask.x, rootRotationMask.y, rootRotationMask.z, discardRootMotion);

		auto& animation = m_Animations[hash];
		if (!animation)
		{
			// Deferred load of animations.
			// We cannot load them earlier (e.g. in MeshSource constructor) for two reasons:
			// 1) Assimp does not import bones (and hence no skeleton) if the mesh source file contains only animations (and no skin)
			//    This means we need to wait until we know what the skeleton is before we can load the animations.
			// 2) We don't have any way to pass the root motion parameters to the mesh source constructor
			HZ_CORE_VERIFY(!m_Runtime);
			auto path = Project::GetEditorAssetManager()->GetFileSystemPath(Handle);
			AssimpMeshImporter importer(path);
			importer.ImportAnimation(animationName, skeleton, extractRootMotion, rootBoneIndex, rootTranslationMask, rootRotationMask, discardRootMotion, animation);
		}
		return animation.get(); // Note: (0x) could be nullptr (e.g. if import, above, failed.)
	}
#endif

	Mesh::Mesh(AssetHandle meshSource, bool generateColliders)
		: m_MeshSource(meshSource)
		, m_GenerateColliders(generateColliders)
	{
		// Generate a new asset handle
		Handle = {};

		// Make sure to create material table even if meshsource asset cannot be retrieved
		// (this saves having to keep checking mesh->m_Materials is not null elsewhere in the code)
		m_Materials = Ref<MaterialTable>::Create(0);

		if (auto meshSourceAsset = AssetManager::GetAsset<MeshSource>(meshSource); meshSourceAsset)
		{
			SetSubmeshes({}, meshSourceAsset);

			const std::vector<AssetHandle>& meshMaterials = meshSourceAsset->GetMaterials();
			for (size_t i = 0; i < meshMaterials.size(); i++)
				m_Materials->SetMaterial((uint32_t)i, meshMaterials[i]);
		}
	}

	Mesh::Mesh(AssetHandle meshSource, const std::vector<uint32_t>& submeshes, bool generateColliders)
		: m_MeshSource(meshSource)
		, m_GenerateColliders(generateColliders)
	{
		// Generate a new asset handle
		Handle = {};

		// Make sure to create material table even if meshsource asset cannot be retrieved
		// (this saves having to keep checking mesh->m_Materials is not null elsewhere in the code)
		m_Materials = Ref<MaterialTable>::Create(0);

		if (auto meshSourceAsset = AssetManager::GetAsset<MeshSource>(meshSource); meshSourceAsset)
		{
			SetSubmeshes(submeshes, meshSourceAsset);

			const std::vector<AssetHandle>& meshMaterials = meshSourceAsset->GetMaterials();
			for (size_t i = 0; i < meshMaterials.size(); i++)
				m_Materials->SetMaterial((uint32_t)i, meshMaterials[i]);
		}
	}
	void Mesh::OnDependencyUpdated(AssetHandle handle)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		AssetManager::ReloadDataAsync(Handle);
	}

	void Mesh::SetSubmeshes(const std::vector<uint32_t>& submeshes, Ref<MeshSource> meshSource)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!submeshes.empty())
		{
			m_Submeshes = meshSource ? AddSubmeshPlacements(submeshes, meshSource->GetSubmeshes()) : submeshes;
		}
		else
		{
			const auto& submeshes = meshSource->GetSubmeshes();
			m_Submeshes.resize(submeshes.size());
			for (uint32_t i = 0; i < submeshes.size(); i++)
				m_Submeshes[i] = i;
		}
	}

	PackedVertex::PackedVertex(const Vertex& vertex)
		: Position(vertex.Position), Texcoord(vertex.Texcoord)
	{
		constexpr float Snorm16Max = 32767.0f;
		auto isUsable = [](const glm::vec3& v)
			{
				const float lengthSquared = glm::dot(v, v);
				return lengthSquared > 1e-8f && !std::isnan(lengthSquared);
			};

		const glm::vec3 normal = isUsable(vertex.Normal) ? glm::normalize(vertex.Normal) : glm::vec3(0.0f, 0.0f, 1.0f);

		glm::vec3 tangent(0.0f);
		float handedness = 1.0f;
		if (isUsable(vertex.Tangent))
		{
			const glm::vec3 sourceTangent = glm::normalize(vertex.Tangent);
			if (isUsable(vertex.Binormal))
				handedness = glm::dot(glm::cross(normal, sourceTangent), vertex.Binormal) >= 0.0f ? 1.0f : -1.0f;
			tangent = sourceTangent - normal * glm::dot(normal, sourceTangent);
		}
		// No tangent, or one parallel to the normal: any perpendicular will do.
		if (!isUsable(tangent))
			tangent = glm::cross(std::abs(normal.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f), normal);
		tangent = glm::normalize(tangent);

		// A quaternion holds only rotations, so the frame is built right-handed and the handedness
		// rides in the sign of w; w is kept off zero so that sign survives snorm16.
		const glm::mat3 frame(tangent, glm::cross(normal, tangent), normal);
		glm::quat q = glm::normalize(glm::quat_cast(frame));
		if (q.w < 0.0f)
			q = -q;
		if (q.w < 1.0f / Snorm16Max)
		{
			q.w = 1.0f / Snorm16Max;
			q = glm::normalize(q);
		}
		if (handedness < 0.0f)
			q = -q;

		const float components[4] = { q.x, q.y, q.z, q.w };
		for (int i = 0; i < 4; i++)
			QTangent[i] = static_cast<int16_t>(std::round(std::clamp(components[i], -1.0f, 1.0f) * Snorm16Max));
	}

	////////////////////////////////////////////////////////
	// StaticMesh //////////////////////////////////////////
	////////////////////////////////////////////////////////

	namespace {
		std::unordered_map<AssetHandle, Ref<StaticMesh>> s_RuntimeStaticMeshCache;
	}

	StaticMesh::StaticMesh(AssetHandle meshSource, bool generateColliders)
		: m_MeshSource(meshSource)
		, m_GenerateColliders(generateColliders)
	{
		// Generate a new asset handle
		Handle = {};

		// Make sure to create material table even if meshsource asset cannot be retrieved
		// (this saves having to keep checking mesh->m_Materials is not null elsewhere in the code)
		m_Materials = Ref<MaterialTable>::Create(0);

		if (auto meshSourceAsset = AssetManager::GetAsset<MeshSource>(meshSource); meshSourceAsset)
		{
			SetSubmeshes({}, meshSourceAsset);

			const std::vector<AssetHandle>& meshMaterials = meshSourceAsset->GetMaterials();
			uint32_t numMaterials = static_cast<uint32_t>(meshMaterials.size());
			for (uint32_t i = 0; i < numMaterials; i++)
				m_Materials->SetMaterial(i, meshMaterials[i]);
		}
	}

	StaticMesh::StaticMesh(AssetHandle meshSource, const std::vector<uint32_t>& submeshes, bool generateColliders)
		: m_MeshSource(meshSource)
		, m_GenerateColliders(generateColliders)
	{
		// Generate a new asset handle
		Handle = {};

		// Make sure to create material table even if meshsource asset cannot be retrieved
		// (this saves having to keep checking mesh->m_Materials is not null elsewhere in the code)
		m_Materials = Ref<MaterialTable>::Create(0);

		if (auto meshSourceAsset = AssetManager::GetAsset<MeshSource>(meshSource); meshSourceAsset)
		{
			SetSubmeshes(submeshes, meshSourceAsset);

			const std::vector<AssetHandle>& meshMaterials = meshSourceAsset->GetMaterials();
			uint32_t numMaterials = static_cast<uint32_t>(meshMaterials.size());
			for (uint32_t i = 0; i < numMaterials; i++)
				m_Materials->SetMaterial(i, meshMaterials[i]);
		}
	}
	void StaticMesh::OnDependencyUpdated(AssetHandle)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		AssetManager::ReloadDataAsync(Handle);
	}

	Ref<StaticMesh> StaticMesh::GetOrCreateRuntime(AssetHandle staticMeshOrMeshSource)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!staticMeshOrMeshSource || !Project::GetAssetManager())
			return nullptr;

		const AssetType assetType = AssetManager::GetAssetType(staticMeshOrMeshSource);
		if (assetType == AssetType::StaticMesh)
			return AssetManager::GetAsset<StaticMesh>(staticMeshOrMeshSource);

		if (assetType != AssetType::MeshSource)
			return nullptr;

		if (auto it = s_RuntimeStaticMeshCache.find(staticMeshOrMeshSource); it != s_RuntimeStaticMeshCache.end())
		{
			if (it->second)
				return it->second;
		}

		Ref<StaticMesh> staticMesh = Ref<StaticMesh>::Create(staticMeshOrMeshSource, false);
		if (staticMesh->GetSubmeshes().empty())
			return nullptr;

		s_RuntimeStaticMeshCache[staticMeshOrMeshSource] = staticMesh;
		return staticMesh;
	}

	void StaticMesh::SetSubmeshes(const std::vector<uint32_t>& submeshes, Ref<MeshSource> meshSource)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!submeshes.empty())
		{
			m_Submeshes = meshSource ? AddSubmeshPlacements(submeshes, meshSource->GetSubmeshes()) : submeshes;
		}
		else
		{
			const auto& submeshes = meshSource->GetSubmeshes();
			m_Submeshes.resize(submeshes.size());
			for (uint32_t i = 0; i < submeshes.size(); i++)
				m_Submeshes[i] = i;
		}
	}

	AssetHandle ResolveStaticMeshMaterialHandle(const Ref<MaterialTable>& materialTable, const Ref<StaticMesh>& staticMesh,
		const Ref<MeshSource>& meshSource, uint32_t materialIndex)
	{
		if (materialTable)
		{
			if (materialTable->HasMaterial(materialIndex))
				return materialTable->GetMaterial(materialIndex);

			const auto& overrides = materialTable->GetMaterials();
			if (overrides.size() == 1 && materialTable->HasMaterial(0))
				return materialTable->GetMaterial(0);
		}

		Ref<MaterialTable> staticMeshMaterials = staticMesh ? staticMesh->GetMaterials() : nullptr;
		if (staticMeshMaterials && staticMeshMaterials->HasMaterial(materialIndex))
			return staticMeshMaterials->GetMaterial(materialIndex);

		if (meshSource && materialIndex < meshSource->GetMaterials().size())
			return meshSource->GetMaterials()[materialIndex];

		return 0;
	}
}
