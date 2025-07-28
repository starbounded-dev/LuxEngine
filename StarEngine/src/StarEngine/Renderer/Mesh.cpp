#include "sepch.h" 
#include "Mesh.h"

#include "StarEngine/Debug/Profiler.h"
#include "StarEngine/Math/Math.h"
#include "StarEngine/Renderer/Renderer.h"
#include "StarEngine/Project/Project.h"
#include "StarEngine/Asset/AssimpMeshImporter.h"
#include "StarEngine/Asset/AssetManager.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include "imgui/imgui.h"

#include <filesystem>

namespace StarEngine
{

#define MESH_DEBUG_LOG 0
#if MESH_DEBUG_LOG
#define SE_MESH_LOG(...) SE_CORE_TRACE_TAG("Mesh", __VA_ARGS__)
#define SE_MESH_ERROR(...) SE_CORE_ERROR_TAG("Mesh", __VA_ARGS__)
#else
#define SE_MESH_LOG(...)
#define SE_MESH_ERROR(...)
#endif

	////////////////////////////////////////////////////////
	// MeshSource //////////////////////////////////////////
	////////////////////////////////////////////////////////
	MeshSource::MeshSource(const std::vector<Vertex>& vertices, const std::vector<Index>& indices, const glm::mat4& transform)
		: m_Vertices(vertices), m_Indices(indices)
	{
		// Generate a new asset handle
		Handle = {};

		Submesh submesh;
		submesh.BaseVertex = 0;
		submesh.BaseIndex = 0;
		submesh.IndexCount = (uint32_t)indices.size() * 3u;
		submesh.Transform = transform;
		m_Submeshes.push_back(submesh);

		m_VertexBuffer = VertexBuffer::Create(Buffer(m_Vertices.data(), (uint32_t)(m_Vertices.size() * sizeof(Vertex))));
		m_IndexBuffer = IndexBuffer::Create(Buffer(m_Indices.data(), (uint32_t)(m_Indices.size() * sizeof(Index))));

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
	}

	MeshSource::MeshSource(const std::vector<Vertex>& vertices, const std::vector<Index>& indices, const std::vector<Submesh>& submeshes)
		: m_Vertices(vertices), m_Indices(indices), m_Submeshes(submeshes)
	{
		// Generate a new asset handle
		Handle = {};

		m_VertexBuffer = VertexBuffer::Create(Buffer(m_Vertices.data(), (uint32_t)(m_Vertices.size() * sizeof(Vertex))));
		m_IndexBuffer = IndexBuffer::Create(Buffer(m_Indices.data(), (uint32_t)(m_Indices.size() * sizeof(Index))));

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
	}

	MeshSource::~MeshSource()
	{
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
		// TODO: Convert to ImGui
		SE_MESH_LOG("------------------------------------------------------");
		SE_MESH_LOG("Vertex Buffer Dump");
		SE_MESH_LOG("Mesh: {0}", m_FilePath);
		for (size_t i = 0; i < m_Vertices.size(); i++)
		{
			auto& vertex = m_Vertices[i];
			SE_MESH_LOG("Vertex: {0}", i);
			SE_MESH_LOG("Position: {0}, {1}, {2}", vertex.Position.x, vertex.Position.y, vertex.Position.z);
			SE_MESH_LOG("Normal: {0}, {1}, {2}", vertex.Normal.x, vertex.Normal.y, vertex.Normal.z);
			SE_MESH_LOG("Binormal: {0}, {1}, {2}", vertex.Binormal.x, vertex.Binormal.y, vertex.Binormal.z);
			SE_MESH_LOG("Tangent: {0}, {1}, {2}", vertex.Tangent.x, vertex.Tangent.y, vertex.Tangent.z);
			SE_MESH_LOG("TexCoord: {0}, {1}", vertex.Texcoord.x, vertex.Texcoord.y);
			SE_MESH_LOG("--");
		}
		SE_MESH_LOG("------------------------------------------------------");
	}


	/*
	// TODO (0x): this is temporary.. and will eventually be replaced with some kind of skeleton retargeting
	bool MeshSource::IsCompatibleSkeleton(const std::string_view animationName, const Skeleton& skeleton) const
	{
		if (!m_Skeleton)
		{
			SE_CORE_VERIFY(!m_Runtime);
			auto path = Project::GetEditorAssetManager()->GetFileSystemPath(Handle);
			AssimpMeshImporter importer(path);
			return importer.IsCompatibleSkeleton(animationName, skeleton);
		}

		return m_Skeleton->GetBoneNames() == skeleton.GetBoneNames();
	}


	std::vector<std::string> MeshSource::GetAnimationNames() const
	{
		return m_AnimationNames;
	}


	const Animation* MeshSource::GetAnimation(const std::string& animationName, const Skeleton& skeleton, bool extractRootMotion, uint32_t rootBoneIndex, const glm::bvec3& rootTranslationMask, const glm::bvec3& rootRotationMask, bool discardRootMotion) const
	{
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
			SE_CORE_VERIFY(!m_Runtime);
			auto path = Project::GetEditorAssetManager()->GetFileSystemPath(Handle);
			AssimpMeshImporter importer(path);
			importer.ImportAnimation(animationName, skeleton, extractRootMotion, rootBoneIndex, rootTranslationMask, rootRotationMask, discardRootMotion, animation);
		}
		return animation.get(); // Note: (0x) could be nullptr (e.g. if import, above, failed.)
	}*/


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
		Project::GetAssetManager()->ReloadDataAsync(Handle);
	}

	void Mesh::SetSubmeshes(const std::vector<uint32_t>& submeshes, Ref<MeshSource> meshSource)
	{
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

	StaticMesh::StaticMesh(AssetHandle meshSource, bool generateColliders)
		: m_MeshSource(meshSource)
		, m_GenerateColliders(generateColliders)
	{
		// Generate a new asset handle
		Handle = {};

		// Make sure to create material table even if meshsource asset cannot be retrieved
		// (this saves having to keep checking mesh->m_Materials is not null elsewhere in the code)
		m_Materials = Ref<MaterialTable>::Create(0);

		if(auto meshSourceAsset = AssetManager::GetAsset<MeshSource>(meshSource); meshSourceAsset)
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
		Project::GetAssetManager()->ReloadDataAsync(Handle);
	}

	void StaticMesh::SetSubmeshes(const std::vector<uint32_t>& submeshes, Ref<MeshSource> meshSource)
	{
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
