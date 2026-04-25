#include "lpch.h"
#include "MeshRuntimeSerializer.h"

#include "MeshSourceFile.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/Renderer.h"

namespace Lux
{
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
					material.ShaderName = shader->GetName();
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
		stream.SetStreamPosition(assetInfo.PackedOffset);
		const uint64_t streamOffset = stream.GetStreamPosition();

		MeshSourceFile file;
		stream.ReadRaw<MeshSourceFile::FileHeader>(file.Header);
		const bool validHeader = std::memcmp(file.Header.HEADER, "LZMS", 4) == 0;
		LUX_CORE_ASSERT(validHeader);
		if (!validHeader)
			return nullptr;

		Ref<MeshSource> meshSource = Ref<MeshSource>::Create();
		meshSource->m_Runtime = true;

		stream.ReadRaw<MeshSourceFile::Metadata>(file.Data);
		const auto& metadata = file.Data;
		const bool hasMaterials = (metadata.Flags & (uint32_t)MeshSourceFile::MeshFlags::HasMaterials) != 0;

		stream.SetStreamPosition(streamOffset + metadata.NodeArrayOffset);
		stream.ReadArray(meshSource->m_Nodes);

		stream.SetStreamPosition(streamOffset + metadata.SubmeshArrayOffset);
		stream.ReadArray(meshSource->m_Submeshes);

		if (hasMaterials)
		{
			std::vector<MeshMaterial> meshMaterials;
			stream.SetStreamPosition(streamOffset + metadata.MaterialArrayOffset);
			stream.ReadArray(meshMaterials);

			meshSource->m_Materials.resize(meshMaterials.size());
			for (size_t i = 0; i < meshMaterials.size(); i++)
			{
				const auto& meshMaterial = meshMaterials[i];
				Ref<Shader> shader = Renderer::GetShaderLibrary()->Get(meshMaterial.ShaderName);
				Ref<Material> material = Material::Create(shader, meshMaterial.MaterialName);
				Ref<MaterialAsset> materialAsset = Ref<MaterialAsset>::Create(material);

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

		stream.SetStreamPosition(streamOffset + metadata.VertexBufferOffset);
		stream.ReadArray(meshSource->m_Vertices);

		stream.SetStreamPosition(streamOffset + metadata.IndexBufferOffset);
		stream.ReadArray(meshSource->m_Indices);

		if (!meshSource->m_Vertices.empty())
			meshSource->m_VertexBuffer = VertexBuffer::Create(Buffer(meshSource->m_Vertices.data(), (uint32_t)(meshSource->m_Vertices.size() * sizeof(Vertex))));

		if (!meshSource->m_Indices.empty())
			meshSource->m_IndexBuffer = IndexBuffer::Create(Buffer(meshSource->m_Indices.data(), (uint32_t)(meshSource->m_Indices.size() * sizeof(Index))));

		return meshSource;
	}
}
