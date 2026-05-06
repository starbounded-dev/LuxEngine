#include "lpch.h"
#include "AssimpMeshImporter.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Core/Math/AABB.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/VertexBuffer.h"
#include "Lux/Renderer/IndexBuffer.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/pbrmaterial.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <GLFW/deps/stb_image_write.h>

namespace Lux
{
	static constexpr uint32_t s_InvalidParentIndex = 0xffffffffu;

	static glm::mat4 AssimpMat4ToGlm(const aiMatrix4x4& m)
	{
		glm::mat4 result;
		result[0][0] = m.a1; result[1][0] = m.a2; result[2][0] = m.a3; result[3][0] = m.a4;
		result[0][1] = m.b1; result[1][1] = m.b2; result[2][1] = m.b3; result[3][1] = m.b4;
		result[0][2] = m.c1; result[1][2] = m.c2; result[2][2] = m.c3; result[3][2] = m.c4;
		result[0][3] = m.d1; result[1][3] = m.d2; result[2][3] = m.d3; result[3][3] = m.d4;
		return result;
	}

	static std::string SanitizeAssetFilename(std::string value)
	{
		if (value.empty())
			value = "Material";

		for (char& c : value)
		{
			const unsigned char ch = static_cast<unsigned char>(c);
			if (!std::isalnum(ch) && c != '-' && c != '_')
				c = '_';
		}

		return value;
	}

	static std::string GetMaterialName(aiMaterial* material, uint32_t index)
	{
		aiString name;
		if (material && material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0)
			return name.C_Str();

		return "Material_" + std::to_string(index);
	}

	static bool IsEmbeddedTextureReference(const std::string& texturePath)
	{
		return !texturePath.empty() && texturePath[0] == '*';
	}

	static std::string ToLower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
		{
			return (char)std::tolower(c);
		});
		return value;
	}

	static std::string GetEmbeddedTextureExtension(const aiTexture* texture)
	{
		if (!texture)
			return ".png";

		std::string hint = ToLower(texture->achFormatHint);
		if (hint == "jpeg")
			hint = "jpg";

		if (hint == "png" || hint == "jpg" || hint == "bmp" || hint == "tga")
			return "." + hint;

		return ".png";
	}

	static std::filesystem::path GetImportedTexturePath(const std::filesystem::path& meshPath, const std::string& textureName, const std::string& extension)
	{
		Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager();
		if (!editorAssetManager)
			return {};

		std::filesystem::path meshRelativePath = editorAssetManager->GetRelativePath(meshPath).lexically_normal();
		std::filesystem::path textureRelativeDirectory;
		if (!meshRelativePath.empty() && !meshRelativePath.is_absolute())
			textureRelativeDirectory = meshRelativePath.parent_path() / "Textures";
		else
			textureRelativeDirectory = std::filesystem::path("Textures") / SanitizeAssetFilename(meshPath.stem().string());

		const std::string fileName = SanitizeAssetFilename(meshPath.stem().string() + "_" + textureName) + extension;
		return (textureRelativeDirectory / fileName).lexically_normal();
	}

	static bool WriteEmbeddedTextureToFile(const aiTexture* texture, const std::filesystem::path& filesystemPath)
	{
		if (!texture)
			return false;

		std::error_code ec;
		std::filesystem::create_directories(filesystemPath.parent_path(), ec);

		if (texture->mHeight == 0)
		{
			std::ofstream stream(filesystemPath, std::ios::binary);
			if (!stream.is_open())
				return false;

			stream.write(reinterpret_cast<const char*>(texture->pcData), texture->mWidth);
			return stream.good();
		}

		std::vector<uint8_t> rgba(texture->mWidth * texture->mHeight * 4);
		for (uint32_t i = 0; i < texture->mWidth * texture->mHeight; i++)
		{
			const aiTexel& texel = texture->pcData[i];
			rgba[i * 4 + 0] = texel.r;
			rgba[i * 4 + 1] = texel.g;
			rgba[i * 4 + 2] = texel.b;
			rgba[i * 4 + 3] = texel.a;
		}

		return stbi_write_png(filesystemPath.string().c_str(), texture->mWidth, texture->mHeight, 4, rgba.data(), texture->mWidth * 4) != 0;
	}

	static std::filesystem::path ResolveTexturePath(const std::filesystem::path& meshPath, const aiString& assimpPath)
	{
		std::filesystem::path texturePath = std::filesystem::path(assimpPath.C_Str());
		if (texturePath.is_relative())
			texturePath = meshPath.parent_path() / texturePath;

		return texturePath.lexically_normal();
	}

	static std::filesystem::path FindTexturePath(const std::filesystem::path& meshPath, const aiString& assimpPath)
	{
		std::filesystem::path texturePath = ResolveTexturePath(meshPath, assimpPath);
		if (std::filesystem::exists(texturePath))
			return texturePath;

		const std::filesystem::path filename = texturePath.filename();
		if (filename.empty() || !std::filesystem::exists(meshPath.parent_path()))
			return {};

		std::error_code ec;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(meshPath.parent_path(), ec))
		{
			if (ec)
				break;

			if (entry.is_regular_file(ec) && entry.path().filename() == filename)
				return entry.path().lexically_normal();
		}

		return {};
	}

	static void AddReferencedTexturePath(const std::filesystem::path& meshPath, const aiScene* scene, const aiString& texturePath, std::vector<std::filesystem::path>& paths, std::unordered_set<std::string>& seen)
	{
		const std::string rawPath = texturePath.C_Str();
		if (rawPath.empty() || IsEmbeddedTextureReference(rawPath) || (scene && scene->GetEmbeddedTexture(rawPath.c_str())))
			return;

		std::filesystem::path resolvedPath = FindTexturePath(meshPath, texturePath);
		if (resolvedPath.empty())
			return;

		const std::string key = resolvedPath.lexically_normal().generic_string();
		if (seen.insert(key).second)
			paths.emplace_back(resolvedPath);
	}

	static AssetHandle ImportTexturePathReference(const std::filesystem::path& meshPath, const aiScene* scene, const aiString& texturePath, std::unordered_map<std::string, AssetHandle>& textureCache)
	{
		Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager();
		if (!editorAssetManager)
			return 0;

		const std::string rawPath = texturePath.C_Str();
		if (rawPath.empty())
			return 0;

		if (const aiTexture* embeddedTexture = scene ? scene->GetEmbeddedTexture(rawPath.c_str()) : nullptr)
		{
			const std::string cacheKey = meshPath.lexically_normal().generic_string() + "::embedded::" + rawPath;
			if (auto it = textureCache.find(cacheKey); it != textureCache.end())
				return it->second;

			const std::string extension = GetEmbeddedTextureExtension(embeddedTexture);
			const std::filesystem::path textureRelativePath = GetImportedTexturePath(meshPath, rawPath, extension);
			if (textureRelativePath.empty())
				return 0;

			const std::filesystem::path textureFilesystemPath = Project::GetActiveAssetDirectory() / textureRelativePath;
			if (!std::filesystem::exists(textureFilesystemPath) && !WriteEmbeddedTextureToFile(embeddedTexture, textureFilesystemPath))
			{
				LUX_CORE_WARN("AssimpMeshImporter: failed to extract embedded texture '{}' from '{}'", rawPath, meshPath.filename().string());
				return 0;
			}

			AssetHandle textureHandle = editorAssetManager->ImportAsset(textureRelativePath);
			if (textureHandle && AssetManager::GetAssetType(textureHandle) == AssetType::Texture)
			{
				textureCache[cacheKey] = textureHandle;
				return textureHandle;
			}

			return 0;
		}

		if (IsEmbeddedTextureReference(rawPath))
			return 0;

		std::filesystem::path resolvedPath = FindTexturePath(meshPath, texturePath);
		if (resolvedPath.empty())
		{
			LUX_CORE_WARN("AssimpMeshImporter: texture '{}' referenced by '{}' was not found", rawPath, meshPath.filename().string());
			return 0;
		}

		const std::string cacheKey = resolvedPath.lexically_normal().generic_string();
		if (auto it = textureCache.find(cacheKey); it != textureCache.end())
			return it->second;

		AssetHandle textureHandle = editorAssetManager->ImportAsset(resolvedPath);
		if (textureHandle && AssetManager::GetAssetType(textureHandle) == AssetType::Texture)
		{
			textureCache[cacheKey] = textureHandle;
			return textureHandle;
		}

		return 0;
	}

	static AssetHandle ImportTextureReference(const std::filesystem::path& meshPath, const aiScene* scene, aiMaterial* material, const std::initializer_list<aiTextureType>& textureTypes, std::unordered_map<std::string, AssetHandle>& textureCache)
	{
		if (!material)
			return 0;

		for (aiTextureType textureType : textureTypes)
		{
			if (material->GetTextureCount(textureType) == 0)
				continue;

			aiString texturePath;
			if (material->GetTexture(textureType, 0, &texturePath) != AI_SUCCESS)
				continue;

			AssetHandle textureHandle = ImportTexturePathReference(meshPath, scene, texturePath, textureCache);
			if (textureHandle)
				return textureHandle;
		}

		return 0;
	}

	static std::filesystem::path GetImportedMaterialPath(const std::filesystem::path& meshPath, const std::string& materialName, uint32_t index)
	{
		Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager();
		if (!editorAssetManager)
			return {};

		std::filesystem::path meshRelativePath = editorAssetManager->GetRelativePath(meshPath).lexically_normal();
		std::filesystem::path materialRelativeDirectory;
		if (!meshRelativePath.empty() && !meshRelativePath.is_absolute())
			materialRelativeDirectory = meshRelativePath.parent_path() / "Materials";
		else
			materialRelativeDirectory = std::filesystem::path("Materials") / SanitizeAssetFilename(meshPath.stem().string());

		const std::string fileName = SanitizeAssetFilename(meshPath.stem().string() + "_" + std::to_string(index) + "_" + materialName) + ".lmat";
		return (materialRelativeDirectory / fileName).lexically_normal();
	}

	static void ImportAssimpMaterials(const std::filesystem::path& meshPath, const aiScene* scene, Ref<MeshSource> meshSource)
	{
		Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager();
		if (!editorAssetManager || !scene || !meshSource)
			return;

		meshSource->GetMaterials().assign(scene->mNumMaterials, 0);
		std::unordered_map<std::string, AssetHandle> textureCache;

		for (uint32_t i = 0; i < scene->mNumMaterials; i++)
		{
			aiMaterial* assimpMaterial = scene->mMaterials[i];
			const std::string materialName = SanitizeAssetFilename(GetMaterialName(assimpMaterial, i));
			const std::filesystem::path materialRelativePath = GetImportedMaterialPath(meshPath, materialName, i);
			if (materialRelativePath.empty())
				continue;

			const std::filesystem::path materialFilesystemPath = Project::GetActiveAssetDirectory() / materialRelativePath;
			std::error_code ec;
			std::filesystem::create_directories(materialFilesystemPath.parent_path(), ec);
			if (!std::filesystem::exists(materialFilesystemPath))
			{
				std::ofstream createFile(materialFilesystemPath);
				createFile.close();
			}

			AssetHandle materialHandle = editorAssetManager->ImportAsset(materialRelativePath);
			if (!materialHandle)
				continue;

			float opacity = 1.0f;
			if (assimpMaterial)
				assimpMaterial->Get(AI_MATKEY_OPACITY, opacity);

			aiColor4D baseColor(1.0f, 1.0f, 1.0f, opacity);
			if (assimpMaterial && assimpMaterial->Get(AI_MATKEY_BASE_COLOR, baseColor) != AI_SUCCESS)
			{
				aiColor3D diffuseColor(1.0f, 1.0f, 1.0f);
				if (assimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == AI_SUCCESS)
					baseColor = aiColor4D(diffuseColor.r, diffuseColor.g, diffuseColor.b, opacity);
			}

			const bool transparent = baseColor.a < 0.999f || opacity < 0.999f;
			Ref<MaterialAsset> materialAsset = Ref<MaterialAsset>::Create(transparent);
			materialAsset->Handle = materialHandle;
			if (!materialAsset->GetMaterial())
				continue;

			materialAsset->SetAlbedoColor({ baseColor.r, baseColor.g, baseColor.b });
			if (transparent)
				materialAsset->SetTransparency(std::min(baseColor.a, opacity));

			float metalness = 0.0f;
			if (assimpMaterial && assimpMaterial->Get(AI_MATKEY_METALLIC_FACTOR, metalness) == AI_SUCCESS)
				materialAsset->SetMetalness(std::clamp(metalness, 0.0f, 1.0f));

			float roughness = 0.5f;
			if (assimpMaterial && assimpMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
				materialAsset->SetRoughness(std::clamp(roughness, 0.0f, 1.0f));

			if (AssetHandle albedoMap = ImportTextureReference(meshPath, scene, assimpMaterial, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, textureCache))
				materialAsset->SetAlbedoMap(albedoMap);

			if (AssetHandle normalMap = ImportTextureReference(meshPath, scene, assimpMaterial, { aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS, aiTextureType_HEIGHT }, textureCache))
			{
				materialAsset->SetNormalMap(normalMap);
				materialAsset->SetUseNormalMap(true);
			}

			if (AssetHandle metalnessMap = ImportTextureReference(meshPath, scene, assimpMaterial, { aiTextureType_METALNESS }, textureCache))
				materialAsset->SetMetalnessMap(metalnessMap);

			if (AssetHandle roughnessMap = ImportTextureReference(meshPath, scene, assimpMaterial, { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SHININESS }, textureCache))
				materialAsset->SetRoughnessMap(roughnessMap);

			AssetImporter::Serialize(editorAssetManager->GetMetadata(materialHandle), materialAsset.As<Asset>());
			AssetManager::ReloadData(materialHandle);
			meshSource->GetMaterials()[i] = materialHandle;
		}
	}

	AssimpMeshImporter::AssimpMeshImporter(const std::filesystem::path& path)
		: m_Path(path)
	{
	}

	std::vector<std::filesystem::path> AssimpMeshImporter::GetReferencedTexturePaths(const std::filesystem::path& path)
	{
		std::vector<std::filesystem::path> texturePaths;
		std::unordered_set<std::string> seenPaths;

		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(path.string(), aiProcess_ValidateDataStructure);
		if (!scene)
			return texturePaths;

		constexpr std::array<aiTextureType, 10> textureTypes = {
			aiTextureType_BASE_COLOR,
			aiTextureType_DIFFUSE,
			aiTextureType_NORMAL_CAMERA,
			aiTextureType_NORMALS,
			aiTextureType_HEIGHT,
			aiTextureType_METALNESS,
			aiTextureType_DIFFUSE_ROUGHNESS,
			aiTextureType_SHININESS,
			aiTextureType_EMISSIVE,
			aiTextureType_UNKNOWN
		};

		for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; materialIndex++)
		{
			aiMaterial* material = scene->mMaterials[materialIndex];
			if (!material)
				continue;

			for (aiTextureType textureType : textureTypes)
			{
				const uint32_t textureCount = material->GetTextureCount(textureType);
				for (uint32_t textureIndex = 0; textureIndex < textureCount; textureIndex++)
				{
					aiString texturePath;
					if (material->GetTexture(textureType, textureIndex, &texturePath) == AI_SUCCESS)
						AddReferencedTexturePath(path, scene, texturePath, texturePaths, seenPaths);
				}
			}
		}

		return texturePaths;
	}

	void AssimpMeshImporter::TraverseNodes(Ref<MeshSource> meshSource,
		aiNode* node,
		const glm::mat4& parentTransform,
		uint32_t parentIndex)
	{
		glm::mat4 localTransform = AssimpMat4ToGlm(node->mTransformation);
		glm::mat4 worldTransform = parentTransform * localTransform;

		MeshNode luxNode;
		luxNode.Name = node->mName.C_Str();
		luxNode.LocalTransform = localTransform;
		luxNode.Parent = parentIndex;

		uint32_t nodeIndex = (uint32_t)meshSource->m_Nodes.size();
		meshSource->m_Nodes.push_back(luxNode);

		if (parentIndex != s_InvalidParentIndex)
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
			TraverseNodes(meshSource, node->mChildren[i], worldTransform, nodeIndex);
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
		TraverseNodes(meshSource, scene->mRootNode, glm::mat4(1.0f), s_InvalidParentIndex);

		ImportAssimpMaterials(m_Path, scene, meshSource);

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
