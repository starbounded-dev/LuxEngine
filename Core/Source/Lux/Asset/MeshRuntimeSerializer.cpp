// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MeshRuntimeSerializer.h"

#include "MeshSourceFile.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Shader.h"

namespace Lux
{
	namespace
	{
		std::string NormalizeRuntimeMaterialShaderName(std::string shaderName)
		{
			if (shaderName == "LuxPBR_Static")
				return "HazelPBR_Static";

			if (shaderName == "LuxPBR_Transparent")
				return "HazelPBR_Transparent";

			return shaderName;
		}
	}

	struct MeshMaterial
	{
		std::string MaterialName;
		std::string ShaderName;

		glm::vec3 AlbedoColor = { 1.0f, 1.0f, 1.0f };
		float Emission = 0.0f;
		float Metalness = 0.0f;
		float Roughness = 0.5f;
		bool UseNormalMap = false;

		uint64_t AlbedoTexture = 0;
		uint64_t NormalTexture = 0;
		uint64_t MetalnessTexture = 0;
		uint64_t RoughnessTexture = 0;

		static void Serialize(StreamWriter* serializer, const MeshMaterial& instance)
		{
			serializer->WriteString(instance.MaterialName);
			serializer->WriteString(instance.ShaderName);
			serializer->WriteRaw(instance.AlbedoColor);
			serializer->WriteRaw(instance.Emission);
			serializer->WriteRaw(instance.Metalness);
			serializer->WriteRaw(instance.Roughness);
			serializer->WriteRaw(instance.UseNormalMap);
			serializer->WriteRaw(instance.AlbedoTexture);
			serializer->WriteRaw(instance.NormalTexture);
			serializer->WriteRaw(instance.MetalnessTexture);
			serializer->WriteRaw(instance.RoughnessTexture);
		}

		static void Deserialize(StreamReader* deserializer, MeshMaterial& instance)
		{
			deserializer->ReadString(instance.MaterialName);
			deserializer->ReadString(instance.ShaderName);
			deserializer->ReadRaw(instance.AlbedoColor);
			deserializer->ReadRaw(instance.Emission);
			deserializer->ReadRaw(instance.Metalness);
			deserializer->ReadRaw(instance.Roughness);
			deserializer->ReadRaw(instance.UseNormalMap);
			deserializer->ReadRaw(instance.AlbedoTexture);
			deserializer->ReadRaw(instance.NormalTexture);
			deserializer->ReadRaw(instance.MetalnessTexture);
			deserializer->ReadRaw(instance.RoughnessTexture);
		}
	};

	namespace
	{
		// A runtime pack is input the engine did not just write, so a mesh entry is read into memory once
		// (size-capped), every count, string and section is checked against the bytes actually there, and
		// the mesh and its materials are created only after all of it checks out. A bad entry is
		// reported and skipped instead of allocating garbage counts or reading past its end.
		constexpr uint64_t k_MaxPackedMeshBytes = 2ull << 30;
		constexpr uint32_t k_MeshSourceFileVersion = 1;

		class BoundedReader
		{
		public:
			BoundedReader(const uint8_t* data, uint64_t size)
				: m_Data(data), m_Size(size)
			{
			}

			bool Read(void* destination, uint64_t size)
			{
				if (size > Remaining())
					return false;
				std::memcpy(destination, m_Data + m_Position, size);
				m_Position += size;
				return true;
			}

			template<typename T>
			bool Read(T& value)
			{
				static_assert(std::is_trivially_copyable_v<T>);
				return Read(&value, sizeof(T));
			}

			// A bool written as one byte; a value other than 0 or 1 is corrupt.
			bool ReadBool(bool& value)
			{
				uint8_t byte = 0;
				if (!Read(byte) || byte > 1)
					return false;
				value = byte != 0;
				return true;
			}

			// StreamWriter::WriteString: a size_t length, then the characters.
			bool ReadString(std::string& string)
			{
				static_assert(sizeof(size_t) == sizeof(uint64_t));
				uint64_t length = 0;
				if (!Read(length) || length > Remaining())
					return false;
				string.assign(reinterpret_cast<const char*>(m_Data + m_Position), length);
				m_Position += length;
				return true;
			}

			// The uint32 count StreamWriter::WriteArray writes, refused when that many elements cannot
			// fit in what is left.
			bool ReadCount(uint32_t& count, uint64_t minimumElementBytes)
			{
				return Read(count) && static_cast<uint64_t>(count) <= Remaining() / minimumElementBytes;
			}

			template<typename T>
			bool ReadTrivialArray(std::vector<T>& array)
			{
				static_assert(std::is_trivially_copyable_v<T>);
				uint32_t count = 0;
				if (!ReadCount(count, sizeof(T)))
					return false;
				array.resize(count);
				return Read(array.data(), static_cast<uint64_t>(count) * sizeof(T));
			}

			template<typename T, typename ReadElement>
			bool ReadArray(std::vector<T>& array, uint64_t minimumElementBytes, ReadElement&& readElement)
			{
				uint32_t count = 0;
				if (!ReadCount(count, minimumElementBytes))
					return false;
				array.resize(count);
				for (T& element : array)
				{
					if (!readElement(*this, element))
						return false;
				}
				return true;
			}

			uint64_t Remaining() const { return m_Size - m_Position; }
		private:
			const uint8_t* m_Data = nullptr;
			uint64_t m_Size = 0;
			uint64_t m_Position = 0;
		};

		// The fixed part of each serialized element, for refusing counts that cannot fit.
		constexpr uint64_t k_MinNodeBytes = sizeof(uint32_t) * 3 + sizeof(uint64_t) + sizeof(glm::mat4);
		constexpr uint64_t k_MinSubmeshBytes = sizeof(uint32_t) * 5 + sizeof(glm::mat4) * 2 + sizeof(AABB) + sizeof(uint64_t) * 2 + 1;
		constexpr uint64_t k_MinMaterialBytes = sizeof(uint64_t) * 2 + sizeof(glm::vec3) + sizeof(float) * 3 + 1 + sizeof(uint64_t) * 4;

		bool ReadNode(BoundedReader& reader, MeshNode& node)
		{
			return reader.Read(node.Parent) && reader.ReadTrivialArray(node.Children) && reader.ReadTrivialArray(node.Submeshes)
				&& reader.ReadString(node.Name) && reader.Read(node.LocalTransform);
		}

		bool ReadSubmesh(BoundedReader& reader, Submesh& submesh)
		{
			return reader.Read(submesh.BaseVertex) && reader.Read(submesh.BaseIndex) && reader.Read(submesh.MaterialIndex)
				&& reader.Read(submesh.IndexCount) && reader.Read(submesh.VertexCount) && reader.Read(submesh.Transform)
				&& reader.Read(submesh.LocalTransform) && reader.Read(submesh.BoundingBox) && reader.ReadString(submesh.NodeName)
				&& reader.ReadString(submesh.MeshName) && reader.ReadBool(submesh.IsRigged);
		}

		bool ReadMaterial(BoundedReader& reader, MeshMaterial& material)
		{
			return reader.ReadString(material.MaterialName) && reader.ReadString(material.ShaderName) && reader.Read(material.AlbedoColor)
				&& reader.Read(material.Emission) && reader.Read(material.Metalness) && reader.Read(material.Roughness)
				&& reader.ReadBool(material.UseNormalMap) && reader.Read(material.AlbedoTexture) && reader.Read(material.NormalTexture)
				&& reader.Read(material.MetalnessTexture) && reader.Read(material.RoughnessTexture);
		}

		struct PackedMeshSource
		{
			MeshSourceFile::Metadata Metadata;
			std::vector<MeshNode> Nodes;
			std::vector<Submesh> Submeshes;
			std::vector<MeshMaterial> Materials;
			std::vector<Vertex> Vertices;
			std::vector<Index> Indices;
		};

		// Parses one MeshSourceFile entry of `size` bytes. Returns what is wrong, or empty.
		std::string ParsePackedMeshSource(const uint8_t* data, uint64_t size, PackedMeshSource& mesh)
		{
			BoundedReader reader(data, size);
			char magic[4] = {};
			uint32_t version = 0;
			if (!reader.Read(magic, sizeof(magic)) || !reader.Read(version) || !reader.Read(mesh.Metadata))
				return "the header is truncated";
			if (std::memcmp(magic, "LZMS", 4) != 0)
				return "it is not a mesh (bad magic)";
			if (version != k_MeshSourceFileVersion)
				return std::format("its format version {} is not {}", version, k_MeshSourceFileVersion);

			// Each section gets its own reader, so it cannot run into the next one either.
			auto section = [&](uint64_t offset, uint64_t sectionSize, const char* name, auto&& parse) -> std::string
			{
				if (offset > size || sectionSize > size - offset)
					return std::format("its {} section lies outside the entry", name);
				BoundedReader sectionReader(data + offset, sectionSize);
				return parse(sectionReader) ? std::string() : std::format("its {} section is malformed", name);
			};

			const MeshSourceFile::Metadata& metadata = mesh.Metadata;
			std::string error = section(metadata.NodeArrayOffset, metadata.NodeArraySize, "node",
				[&](BoundedReader& sectionReader) { return sectionReader.ReadArray(mesh.Nodes, k_MinNodeBytes, ReadNode); });
			if (error.empty())
			{
				error = section(metadata.SubmeshArrayOffset, metadata.SubmeshArraySize, "submesh",
					[&](BoundedReader& sectionReader) { return sectionReader.ReadArray(mesh.Submeshes, k_MinSubmeshBytes, ReadSubmesh); });
			}
			if (error.empty() && (metadata.Flags & static_cast<uint32_t>(MeshSourceFile::MeshFlags::HasMaterials)))
			{
				error = section(metadata.MaterialArrayOffset, metadata.MaterialArraySize, "material",
					[&](BoundedReader& sectionReader) { return sectionReader.ReadArray(mesh.Materials, k_MinMaterialBytes, ReadMaterial); });
			}
			if (error.empty())
			{
				error = section(metadata.VertexBufferOffset, metadata.VertexBufferSize, "vertex",
					[&](BoundedReader& sectionReader) { return sectionReader.ReadTrivialArray(mesh.Vertices); });
			}
			if (error.empty())
			{
				error = section(metadata.IndexBufferOffset, metadata.IndexBufferSize, "index",
					[&](BoundedReader& sectionReader) { return sectionReader.ReadTrivialArray(mesh.Indices); });
			}
			return error;
		}

		// Every range and reference the renderer, physics and node walks follow. Returns what is
		// wrong, or empty.
		std::string ValidatePackedMeshSource(const PackedMeshSource& mesh)
		{
			for (size_t submeshIndex = 0; submeshIndex < mesh.Submeshes.size(); submeshIndex++)
			{
				const Submesh& submesh = mesh.Submeshes[submeshIndex];
				if (submesh.BaseIndex % 3 != 0 || submesh.IndexCount % 3 != 0)
					return std::format("submesh {} does not cover whole triangles", submeshIndex);
				if (static_cast<uint64_t>(submesh.BaseVertex) + submesh.VertexCount > mesh.Vertices.size())
					return std::format("submesh {} uses vertices past the {} the mesh has", submeshIndex, mesh.Vertices.size());

				const uint64_t firstTriangle = submesh.BaseIndex / 3;
				const uint64_t triangleCount = submesh.IndexCount / 3;
				if (firstTriangle + triangleCount > mesh.Indices.size())
					return std::format("submesh {} uses triangles past the {} the mesh has", submeshIndex, mesh.Indices.size());

				// Indices are local to the submesh's vertices (AssimpMeshImporter).
				for (uint64_t triangle = firstTriangle; triangle < firstTriangle + triangleCount; triangle++)
				{
					const Index& index = mesh.Indices[triangle];
					if (index.V1 >= submesh.VertexCount || index.V2 >= submesh.VertexCount || index.V3 >= submesh.VertexCount)
						return std::format("submesh {} has a triangle outside its {} vertices", submeshIndex, submesh.VertexCount);
				}
			}

			const uint64_t nodeCount = mesh.Nodes.size();
			for (size_t nodeIndex = 0; nodeIndex < mesh.Nodes.size(); nodeIndex++)
			{
				const MeshNode& node = mesh.Nodes[nodeIndex];
				if (!node.IsRoot() && node.Parent >= nodeCount)
					return std::format("node {} has a parent that does not exist", nodeIndex);
				if (std::any_of(node.Children.begin(), node.Children.end(), [nodeCount](uint32_t child) { return child >= nodeCount; }))
					return std::format("node {} has a child that does not exist", nodeIndex);
				if (std::any_of(node.Submeshes.begin(), node.Submeshes.end(), [&mesh](uint32_t submesh) { return submesh >= mesh.Submeshes.size(); }))
					return std::format("node {} places a submesh that does not exist", nodeIndex);
			}
			return {};
		}
	}

	bool MeshRuntimeSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo)
	{
		Ref<MeshSource> meshSource = AssetManager::GetAsset<MeshSource>(handle);
		if (!meshSource)
			return false;

		outInfo.Offset = stream.GetStreamPosition();
		const uint64_t streamOffset = stream.GetStreamPosition();

		MeshSourceFile file;
		file.Data.Flags = 0;
		file.Data.BoundingBox = meshSource->GetBoundingBox();

		const bool hasMaterials = !meshSource->GetMaterials().empty();
		if (hasMaterials)
			file.Data.Flags |= (uint32_t)MeshSourceFile::MeshFlags::HasMaterials;

		stream.WriteRaw<MeshSourceFile::FileHeader>(file.Header);
		const uint64_t metadataAbsolutePosition = stream.GetStreamPosition();
		stream.WriteZero(sizeof(MeshSourceFile::Metadata));

		file.Data.NodeArrayOffset = stream.GetStreamPosition() - streamOffset;
		stream.WriteArray(meshSource->m_Nodes);
		file.Data.NodeArraySize = (stream.GetStreamPosition() - streamOffset) - file.Data.NodeArrayOffset;

		file.Data.SubmeshArrayOffset = stream.GetStreamPosition() - streamOffset;
		stream.WriteArray(meshSource->m_Submeshes);
		file.Data.SubmeshArraySize = (stream.GetStreamPosition() - streamOffset) - file.Data.SubmeshArrayOffset;

		if (hasMaterials)
		{
			std::vector<MeshMaterial> meshMaterials(meshSource->GetMaterials().size());
			const auto& sourceMaterials = meshSource->GetMaterials();
			for (size_t i = 0; i < meshMaterials.size(); i++)
			{
				Ref<MaterialAsset> materialAsset = AssetManager::GetAsset<MaterialAsset>(sourceMaterials[i]);
				if (!materialAsset)
					continue;

				MeshMaterial& material = meshMaterials[i];
				Ref<Material> sourceMaterial = materialAsset->GetMaterial();
				if (!sourceMaterial)
					continue;

				material.MaterialName = sourceMaterial->GetName();
				if (Ref<Shader> shader = sourceMaterial->GetShader())
					material.ShaderName = NormalizeRuntimeMaterialShaderName(shader->GetName());
				material.AlbedoColor = materialAsset->GetAlbedoColor();
				material.Emission = materialAsset->GetEmission();
				material.Metalness = materialAsset->GetMetalness();
				material.Roughness = materialAsset->GetRoughness();
				material.UseNormalMap = materialAsset->IsUsingNormalMap();
				material.AlbedoTexture = materialAsset->GetAlbedoMapHandle();
				material.NormalTexture = materialAsset->GetNormalMapHandle();
				material.MetalnessTexture = materialAsset->GetMetalnessMapHandle();
				material.RoughnessTexture = materialAsset->GetRoughnessMapHandle();
			}

			file.Data.MaterialArrayOffset = stream.GetStreamPosition() - streamOffset;
			stream.WriteArray(meshMaterials);
			file.Data.MaterialArraySize = (stream.GetStreamPosition() - streamOffset) - file.Data.MaterialArrayOffset;
		}
		else
		{
			file.Data.MaterialArrayOffset = 0;
			file.Data.MaterialArraySize = 0;
		}

		file.Data.VertexBufferOffset = stream.GetStreamPosition() - streamOffset;
		stream.WriteArray(meshSource->m_Vertices);
		file.Data.VertexBufferSize = (stream.GetStreamPosition() - streamOffset) - file.Data.VertexBufferOffset;

		file.Data.IndexBufferOffset = stream.GetStreamPosition() - streamOffset;
		stream.WriteArray(meshSource->m_Indices);
		file.Data.IndexBufferSize = (stream.GetStreamPosition() - streamOffset) - file.Data.IndexBufferOffset;

		const uint64_t endOfStream = stream.GetStreamPosition();
		stream.SetStreamPosition(metadataAbsolutePosition);
		stream.WriteRaw<MeshSourceFile::Metadata>(file.Data);
		stream.SetStreamPosition(endOfStream);

		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return true;
	}

	Ref<Asset> MeshRuntimeSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo)
	{
		auto reject = [&assetInfo](std::string_view reason) -> Ref<Asset>
		{
			LUX_CORE_ERROR_TAG("AssetManager", "The mesh at offset {} of the asset pack ({} bytes) is not loaded: {}", assetInfo.PackedOffset, assetInfo.PackedSize, reason);
			return nullptr;
		};

		if (assetInfo.PackedSize > k_MaxPackedMeshBytes)
			return reject("its size is implausible");

		Buffer entry;
		entry.Allocate(assetInfo.PackedSize);
		stream.SetStreamPosition(assetInfo.PackedOffset);
		const bool readEntry = stream.ReadData(static_cast<char*>(entry.Data), assetInfo.PackedSize);
		PackedMeshSource packed;
		std::string error = readEntry ? ParsePackedMeshSource(static_cast<const uint8_t*>(entry.Data), entry.Size, packed) : "the pack ends before it does";
		entry.Release();
		if (error.empty())
			error = ValidatePackedMeshSource(packed);
		if (!error.empty())
			return reject(error);

		// Everything checked out: only now is the mesh created and published.
		Ref<MeshSource> meshSource = Ref<MeshSource>::Create();
		meshSource->m_Runtime = true;
		meshSource->m_BoundingBox = packed.Metadata.BoundingBox;
		meshSource->m_Nodes = std::move(packed.Nodes);
		meshSource->m_Submeshes = std::move(packed.Submeshes);

		const bool hasMaterials = !packed.Materials.empty();
		if (hasMaterials)
		{
			const std::vector<MeshMaterial>& meshMaterials = packed.Materials;
			meshSource->m_Materials.resize(meshMaterials.size());
			for (size_t i = 0; i < meshMaterials.size(); i++)
			{
				const auto& meshMaterial = meshMaterials[i];
				Ref<Shader> shader;
				if (Ref<ShaderLibrary> shaderLibrary = Renderer::GetShaderLibrary())
				{
					const auto& shaders = shaderLibrary->GetShaders();
					const std::string shaderName = NormalizeRuntimeMaterialShaderName(meshMaterial.ShaderName);
					if (!shaderName.empty())
					{
						if (auto it = shaders.find(shaderName); it != shaders.end())
							shader = it->second;
					}

					if (!shader)
					{
						if (auto it = shaders.find("HazelPBR_Static"); it != shaders.end())
							shader = it->second;
					}
				}

				Ref<MaterialAsset> materialAsset;
				if (shader)
					materialAsset = Ref<MaterialAsset>::Create(Material::Create(shader, meshMaterial.MaterialName.empty() ? "RuntimeMeshMaterial" : meshMaterial.MaterialName));
				else
					materialAsset = Ref<MaterialAsset>::Create(false);

				if (!materialAsset || !materialAsset->GetMaterial())
					continue;

				materialAsset->SetAlbedoColor(meshMaterial.AlbedoColor);
				materialAsset->SetEmission(meshMaterial.Emission);
				materialAsset->SetMetalness(meshMaterial.Metalness);
				materialAsset->SetRoughness(meshMaterial.Roughness);
				materialAsset->SetUseNormalMap(meshMaterial.UseNormalMap);
				materialAsset->SetAlbedoMap(meshMaterial.AlbedoTexture);
				materialAsset->SetNormalMap(meshMaterial.NormalTexture);
				materialAsset->SetMetalnessMap(meshMaterial.MetalnessTexture);
				materialAsset->SetRoughnessMap(meshMaterial.RoughnessTexture);

				meshSource->m_Materials[i] = AssetManager::AddMemoryOnlyAsset(materialAsset);
			}
		}

		meshSource->m_Vertices = std::move(packed.Vertices);
		meshSource->m_Indices = std::move(packed.Indices);

		meshSource->BuildRenderGeometry();

		// In the standalone runtime, drop the full CPU vertex array now that the
		// GPU has it (positions + indices are kept for physics cooking).
		meshSource->CompactCPUGeometry();

		return meshSource;
	}
}
