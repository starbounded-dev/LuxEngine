#include "lpch.h" 
#include "Mesh.h"

#include "Lux/Debug/Profiler.h"
#include "Lux/Math/Math.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Project/Project.h"
#include "Lux/Asset/AssetManager.h"

//#include "Jolt/Core/HashCombine.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include "imgui/imgui.h"

#include <array>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

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
		struct TriangleKey
		{
			uint32_t A = 0;
			uint32_t B = 0;
			uint32_t C = 0;

			bool operator==(const TriangleKey& other) const
			{
				return A == other.A && B == other.B && C == other.C;
			}
		};

		struct TriangleKeyHasher
		{
			size_t operator()(const TriangleKey& key) const
			{
				size_t seed = std::hash<uint32_t>{}(key.A);
				seed ^= std::hash<uint32_t>{}(key.B) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				seed ^= std::hash<uint32_t>{}(key.C) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				return seed;
			}
		};

		struct VertexCluster
		{
			Vertex Sum{};
			uint32_t Count = 0;
		};

		static glm::vec3 NormalizeOrFallback(const glm::vec3& value, const glm::vec3& fallback)
		{
			const float lengthSq = glm::dot(value, value);
			return lengthSq > 0.000001f ? value * glm::inversesqrt(lengthSq) : fallback;
		}

		static uint64_t PackClusterCell(uint32_t x, uint32_t y, uint32_t z)
		{
			return (uint64_t)x | ((uint64_t)y << 21u) | ((uint64_t)z << 42u);
		}

		static uint32_t QuantizeAxis(float value, float minValue, float extent, uint32_t resolution)
		{
			if (extent <= 0.000001f || resolution <= 1)
				return 0;

			const float normalized = std::clamp((value - minValue) / extent, 0.0f, 1.0f);
			return (uint32_t)std::clamp((int)glm::floor(normalized * (float)(resolution - 1u)), 0, (int)resolution - 1);
		}

		static bool BuildClusteredSubmeshLOD(
			const std::vector<Vertex>& sourceVertices,
			const std::vector<Index>& sourceIndices,
			const Submesh& submesh,
			float targetRatio,
			std::vector<Vertex>& renderVertices,
			std::vector<Index>& renderIndices,
			SubmeshLOD& outLOD)
		{
			if (submesh.VertexCount < 64 || submesh.IndexCount < 300 || submesh.BaseVertex + submesh.VertexCount > sourceVertices.size())
				return false;

			const uint32_t firstTriangle = submesh.BaseIndex / 3u;
			const uint32_t triangleCount = submesh.IndexCount / 3u;
			if (triangleCount == 0 || firstTriangle + triangleCount > sourceIndices.size())
				return false;

			const glm::vec3 boundsMin = submesh.BoundingBox.Min;
			const glm::vec3 boundsExtent = glm::max(submesh.BoundingBox.Max - submesh.BoundingBox.Min, glm::vec3(0.000001f));
			const uint32_t targetVertexCount = glm::max(16u, (uint32_t)glm::ceil((float)submesh.VertexCount * targetRatio));
			const uint32_t gridResolution = std::clamp((uint32_t)glm::ceil(glm::pow((float)targetVertexCount, 1.0f / 3.0f) * 1.25f), 2u, 64u);

			std::vector<uint32_t> localToCluster(submesh.VertexCount, std::numeric_limits<uint32_t>::max());
			std::vector<VertexCluster> clusters;
			clusters.reserve(targetVertexCount);

			std::unordered_map<uint64_t, uint32_t> clusterIndexByCell;
			clusterIndexByCell.reserve(targetVertexCount);

			for (uint32_t localVertex = 0; localVertex < submesh.VertexCount; localVertex++)
			{
				const Vertex& vertex = sourceVertices[submesh.BaseVertex + localVertex];
				const uint32_t cellX = QuantizeAxis(vertex.Position.x, boundsMin.x, boundsExtent.x, gridResolution);
				const uint32_t cellY = QuantizeAxis(vertex.Position.y, boundsMin.y, boundsExtent.y, gridResolution);
				const uint32_t cellZ = QuantizeAxis(vertex.Position.z, boundsMin.z, boundsExtent.z, gridResolution);
				const uint64_t cellKey = PackClusterCell(cellX, cellY, cellZ);

				auto [clusterIt, inserted] = clusterIndexByCell.try_emplace(cellKey, (uint32_t)clusters.size());
				if (inserted)
					clusters.emplace_back();

				const uint32_t clusterIndex = clusterIt->second;
				localToCluster[localVertex] = clusterIndex;

				VertexCluster& cluster = clusters[clusterIndex];
				cluster.Sum.Position += vertex.Position;
				cluster.Sum.Normal += vertex.Normal;
				cluster.Sum.Tangent += vertex.Tangent;
				cluster.Sum.Binormal += vertex.Binormal;
				cluster.Sum.Texcoord += vertex.Texcoord;
				cluster.Count++;
			}

			if (clusters.size() >= submesh.VertexCount)
				return false;

			const uint32_t baseVertex = (uint32_t)renderVertices.size();
			const uint32_t baseIndex = (uint32_t)renderIndices.size() * 3u;

			for (const VertexCluster& cluster : clusters)
			{
				const float invCount = cluster.Count > 0 ? 1.0f / (float)cluster.Count : 1.0f;
				Vertex vertex{};
				vertex.Position = cluster.Sum.Position * invCount;
				vertex.Normal = NormalizeOrFallback(cluster.Sum.Normal * invCount, { 0.0f, 1.0f, 0.0f });
				vertex.Tangent = NormalizeOrFallback(cluster.Sum.Tangent * invCount, { 1.0f, 0.0f, 0.0f });
				const glm::vec3 fallbackBinormal = NormalizeOrFallback(glm::cross(vertex.Normal, vertex.Tangent), { 0.0f, 0.0f, 1.0f });
				vertex.Binormal = NormalizeOrFallback(cluster.Sum.Binormal * invCount, fallbackBinormal);
				vertex.Texcoord = cluster.Sum.Texcoord * invCount;
				renderVertices.push_back(vertex);
			}

			std::unordered_set<TriangleKey, TriangleKeyHasher> emittedTriangles;
			emittedTriangles.reserve(triangleCount);

			for (uint32_t triangle = 0; triangle < triangleCount; triangle++)
			{
				const Index& sourceIndex = sourceIndices[firstTriangle + triangle];
				if (sourceIndex.V1 >= submesh.VertexCount || sourceIndex.V2 >= submesh.VertexCount || sourceIndex.V3 >= submesh.VertexCount)
					continue;

				Index lodIndex{
					localToCluster[sourceIndex.V1],
					localToCluster[sourceIndex.V2],
					localToCluster[sourceIndex.V3]
				};

				if (lodIndex.V1 == lodIndex.V2 || lodIndex.V1 == lodIndex.V3 || lodIndex.V2 == lodIndex.V3)
					continue;

				std::array<uint32_t, 3> sorted = { lodIndex.V1, lodIndex.V2, lodIndex.V3 };
				std::sort(sorted.begin(), sorted.end());
				TriangleKey key{ sorted[0], sorted[1], sorted[2] };
				if (!emittedTriangles.insert(key).second)
					continue;

				renderIndices.push_back(lodIndex);
			}

			const uint32_t generatedIndexCount = (uint32_t)(renderIndices.size() * 3u - baseIndex);
			const bool usefulReduction = generatedIndexCount >= 3u && generatedIndexCount < (uint32_t)((float)submesh.IndexCount * 0.92f);
			if (!usefulReduction)
			{
				renderVertices.resize(baseVertex);
				renderIndices.resize(baseIndex / 3u);
				return false;
			}

			outLOD.BaseVertex = baseVertex;
			outLOD.BaseIndex = baseIndex;
			outLOD.IndexCount = generatedIndexCount;
			outLOD.VertexCount = (uint32_t)clusters.size();
			return true;
		}
	}

	////////////////////////////////////////////////////////
	// MeshSource //////////////////////////////////////////
	////////////////////////////////////////////////////////

	bool MeshSource::s_RetainFullCPUGeometry = true;

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

		std::vector<Vertex> renderVertices = m_Vertices;
		std::vector<Index> renderIndices = m_Indices;

		constexpr std::array<float, 3> lodRatios = { 0.50f, 0.25f, 0.125f };
		constexpr std::array<float, 3> lodDistanceMultipliers = { 24.0f, 48.0f, 96.0f };

		uint32_t generatedLODCount = 0;
		uint64_t removedIndexCount = 0;
		for (uint32_t submeshIndex = 0; submeshIndex < (uint32_t)m_Submeshes.size(); submeshIndex++)
		{
			const Submesh& submesh = m_Submeshes[submeshIndex];
			std::vector<SubmeshLOD>& lods = m_SubmeshLODs[submeshIndex];
			lods.push_back({
				submesh.BaseVertex,
				submesh.BaseIndex,
				submesh.IndexCount,
				submesh.VertexCount,
				0.0f
			});

			if (submesh.IsRigged)
				continue;

			uint32_t previousIndexCount = submesh.IndexCount;
			for (size_t lodLevel = 0; lodLevel < lodRatios.size(); lodLevel++)
			{
				SubmeshLOD lod{};
				if (!BuildClusteredSubmeshLOD(m_Vertices, m_Indices, submesh, lodRatios[lodLevel], renderVertices, renderIndices, lod))
					continue;

				if (lod.IndexCount >= previousIndexCount)
					continue;

				lod.DistanceMultiplier = lodDistanceMultipliers[lodLevel];
				lods.push_back(lod);
				generatedLODCount++;
				removedIndexCount += previousIndexCount - lod.IndexCount;
				previousIndexCount = lod.IndexCount;
			}
		}

		m_VertexBuffer = VertexBuffer::Create(Buffer(renderVertices.data(), (uint32_t)(renderVertices.size() * sizeof(Vertex))));
		m_IndexBuffer = IndexBuffer::Create(Buffer(renderIndices.data(), (uint32_t)(renderIndices.size() * sizeof(Index))));

		if (generatedLODCount > 0)
		{
			LUX_CORE_INFO("MeshSource: generated {} auto LOD level(s) for '{}' ({} fewer indices across selected levels)",
				generatedLODCount,
				m_FilePath.empty() ? "<memory>" : m_FilePath,
				removedIndexCount);
		}
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
			m_Submeshes = submeshes;
		}
		else
		{
			const auto& submeshes = meshSource->GetSubmeshes();
			m_Submeshes.resize(submeshes.size());
			for (uint32_t i = 0; i < submeshes.size(); i++)
				m_Submeshes[i] = i;
		}
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
			m_Submeshes = submeshes;
		}
		else
		{
			const auto& submeshes = meshSource->GetSubmeshes();
			m_Submeshes.resize(submeshes.size());
			for (uint32_t i = 0; i < submeshes.size(); i++)
				m_Submeshes[i] = i;
		}
	}
}
