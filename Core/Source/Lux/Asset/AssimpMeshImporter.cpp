#include "lpch.h"
#include "AssimpMeshImporter.h"

#include "Lux/Core/Math/AABB.h"
#include "Lux/Renderer/VertexBuffer.h"
#include "Lux/Renderer/IndexBuffer.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Lux
{
	static glm::mat4 AssimpMat4ToGlm(const aiMatrix4x4& m)
	{
		glm::mat4 result;
		result[0][0] = m.a1; result[1][0] = m.a2; result[2][0] = m.a3; result[3][0] = m.a4;
		result[0][1] = m.b1; result[1][1] = m.b2; result[2][1] = m.b3; result[3][1] = m.b4;
		result[0][2] = m.c1; result[1][2] = m.c2; result[2][2] = m.c3; result[3][2] = m.c4;
		result[0][3] = m.d1; result[1][3] = m.d2; result[2][3] = m.d3; result[3][3] = m.d4;
		return result;
	}

	AssimpMeshImporter::AssimpMeshImporter(const std::filesystem::path& path)
		: m_Path(path)
	{
	}

	static void TraverseNodes(Ref<MeshSource> meshSource,
		aiNode* node,
		const glm::mat4& parentTransform,
		uint32_t parentIndex,
		uint32_t level = 0)
	{
		glm::mat4 localTransform = AssimpMat4ToGlm(node->mTransformation);
		glm::mat4 worldTransform = parentTransform * localTransform;

		MeshNode luxNode;
		luxNode.Name = node->mName.C_Str();
		luxNode.LocalTransform = localTransform;
		luxNode.Parent = parentIndex;

		uint32_t nodeIndex = (uint32_t)meshSource->m_Nodes.size();
		meshSource->m_Nodes.push_back(luxNode);

		if (parentIndex != 0xffffffff)
			meshSource->m_Nodes[parentIndex].Children.push_back(nodeIndex);

		auto& currentNode = meshSource->m_Nodes[nodeIndex];

		for (uint32_t i = 0; i < node->mNumMeshes; i++)
		{
			uint32_t submeshIndex = node->mMeshes[i];
			currentNode.Submeshes.push_back(submeshIndex);
			meshSource->m_Submeshes[submeshIndex].Transform = worldTransform;
			meshSource->m_Submeshes[submeshIndex].LocalTransform = localTransform;
			meshSource->m_Submeshes[submeshIndex].NodeName = node->mName.C_Str();
		}

		for (uint32_t i = 0; i < node->mNumChildren; i++)
			TraverseNodes(meshSource, node->mChildren[i], worldTransform, nodeIndex, level + 1);
	}

	Ref<MeshSource> AssimpMeshImporter::ImportToMeshSource()
	{
		Ref<MeshSource> meshSource = Ref<MeshSource>::Create();
		meshSource->m_FilePath = m_Path.string();

		Assimp::Importer importer;
		importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

		constexpr uint32_t meshImportFlags =
			aiProcess_CalcTangentSpace |
			aiProcess_Triangulate |
			aiProcess_SortByPType |
			aiProcess_GenNormals |
			aiProcess_GenUVCoords |
			aiProcess_OptimizeMeshes |
			aiProcess_JoinIdenticalVertices |
			aiProcess_LimitBoneWeights |
			aiProcess_ValidateDataStructure |
			aiProcess_GlobalScale;

		const aiScene* scene = importer.ReadFile(m_Path.string(), meshImportFlags);
		if (!scene || !scene->HasMeshes())
		{
			LUX_CORE_ERROR("AssimpMeshImporter: Failed to import mesh from '{}'", m_Path.string());
			LUX_CORE_ERROR("  Assimp error: {}", importer.GetErrorString());
			return nullptr;
		}

		// ── Reserve submeshes ─────────────────────────────────────────────────
		meshSource->m_Submeshes.reserve(scene->mNumMeshes);

		uint32_t vertexCount = 0;
		uint32_t indexCount = 0;

		meshSource->m_BoundingBox.Min = { FLT_MAX,  FLT_MAX,  FLT_MAX };
		meshSource->m_BoundingBox.Max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

		for (uint32_t m = 0; m < scene->mNumMeshes; m++)
		{
			aiMesh* mesh = scene->mMeshes[m];

			Submesh& submesh = meshSource->m_Submeshes.emplace_back();
			submesh.BaseVertex    = vertexCount;
			submesh.BaseIndex     = indexCount;
			submesh.MaterialIndex = mesh->mMaterialIndex;
			submesh.IndexCount    = mesh->mNumFaces * 3;
			submesh.VertexCount   = mesh->mNumVertices;
			submesh.MeshName      = mesh->mName.C_Str();

			vertexCount += mesh->mNumVertices;
			indexCount  += submesh.IndexCount;

			// ── AABB per submesh ──────────────────────────────────────────────
			AABB& aabb = submesh.BoundingBox;
			aabb.Min = { FLT_MAX,  FLT_MAX,  FLT_MAX };
			aabb.Max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

			for (uint32_t v = 0; v < mesh->mNumVertices; v++)
			{
				Vertex vertex;
				vertex.Position  = { mesh->mVertices[v].x,  mesh->mVertices[v].y,  mesh->mVertices[v].z };
				vertex.Normal    = { mesh->mNormals[v].x,   mesh->mNormals[v].y,   mesh->mNormals[v].z };

				if (mesh->HasTangentsAndBitangents())
				{
					vertex.Tangent  = { mesh->mTangents[v].x,   mesh->mTangents[v].y,   mesh->mTangents[v].z };
					vertex.Binormal = { mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z };
				}

				if (mesh->HasTextureCoords(0))
					vertex.Texcoord = { mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y };
				else
					vertex.Texcoord = { 0.0f, 0.0f };

				aabb.Min.x = glm::min(vertex.Position.x, aabb.Min.x);
				aabb.Min.y = glm::min(vertex.Position.y, aabb.Min.y);
				aabb.Min.z = glm::min(vertex.Position.z, aabb.Min.z);
				aabb.Max.x = glm::max(vertex.Position.x, aabb.Max.x);
				aabb.Max.y = glm::max(vertex.Position.y, aabb.Max.y);
				aabb.Max.z = glm::max(vertex.Position.z, aabb.Max.z);

				meshSource->m_Vertices.push_back(vertex);
			}

			meshSource->m_BoundingBox.Min.x = glm::min(aabb.Min.x, meshSource->m_BoundingBox.Min.x);
			meshSource->m_BoundingBox.Min.y = glm::min(aabb.Min.y, meshSource->m_BoundingBox.Min.y);
			meshSource->m_BoundingBox.Min.z = glm::min(aabb.Min.z, meshSource->m_BoundingBox.Min.z);
			meshSource->m_BoundingBox.Max.x = glm::max(aabb.Max.x, meshSource->m_BoundingBox.Max.x);
			meshSource->m_BoundingBox.Max.y = glm::max(aabb.Max.y, meshSource->m_BoundingBox.Max.y);
			meshSource->m_BoundingBox.Max.z = glm::max(aabb.Max.z, meshSource->m_BoundingBox.Max.z);

			// ── Indices ───────────────────────────────────────────────────────
			for (uint32_t f = 0; f < mesh->mNumFaces; f++)
			{
				const aiFace& face = mesh->mFaces[f];
				LUX_CORE_ASSERT(face.mNumIndices == 3, "Only triangles are supported!");
				Index idx;
				idx.V1 = face.mIndices[0];
				idx.V2 = face.mIndices[1];
				idx.V3 = face.mIndices[2];
				meshSource->m_Indices.push_back(idx);
			}
		}

		// ── Node hierarchy ────────────────────────────────────────────────────
		// Insert sentinel root so every real node has a valid parentIndex
		meshSource->m_Nodes.emplace_back(); // root placeholder
		TraverseNodes(meshSource, scene->mRootNode, glm::mat4(1.0f), 0xffffffff);

		// ── Materials (allocate zero-material placeholders) ───────────────────
		meshSource->m_Materials.resize(scene->mNumMaterials, 0);

		// ── Triangle cache ────────────────────────────────────────────────────
		for (uint32_t i = 0; i < (uint32_t)meshSource->m_Submeshes.size(); i++)
		{
			const Submesh& sm = meshSource->m_Submeshes[i];
			for (uint32_t f = 0; f < sm.IndexCount / 3; f++)
			{
				const Index& idx = meshSource->m_Indices[sm.BaseIndex / 3 + f];
				meshSource->m_TriangleCache[i].emplace_back(
					meshSource->m_Vertices[sm.BaseVertex + idx.V1],
					meshSource->m_Vertices[sm.BaseVertex + idx.V2],
					meshSource->m_Vertices[sm.BaseVertex + idx.V3]);
			}
		}

		// ── GPU buffers ───────────────────────────────────────────────────────
		meshSource->m_VertexBuffer = VertexBuffer::Create(
			Buffer(meshSource->m_Vertices.data(),
				(uint32_t)(meshSource->m_Vertices.size() * sizeof(Vertex))));

		meshSource->m_IndexBuffer = IndexBuffer::Create(
			Buffer(meshSource->m_Indices.data(),
				(uint32_t)(meshSource->m_Indices.size() * sizeof(Index))));

		LUX_CORE_INFO("AssimpMeshImporter: Loaded '{}' – {} submeshes, {} vertices, {} indices",
			m_Path.filename().string(),
			meshSource->m_Submeshes.size(),
			meshSource->m_Vertices.size(),
			meshSource->m_Indices.size() * 3);

		return meshSource;
	}
}
