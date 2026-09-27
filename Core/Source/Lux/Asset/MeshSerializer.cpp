// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MeshSerializer.h"

#include "MeshRuntimeSerializer.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Physics/MeshColliderAsset.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Mesh.h"

#ifndef LUX_DIST
#include "AssimpMeshImporter.h"
#endif

#include <fstream>
#include <sstream>

#include "Lux/Serialization/Yaml.h"

namespace Lux
{
	namespace
	{
		static std::string ReadMeshYAML(const AssetMetadata& metadata)
		{
			std::ifstream stream(Project::GetActiveAssetDirectory() / metadata.FilePath);
			if (!stream.is_open())
				return {};

			std::stringstream strStream;
			strStream << stream.rdbuf();
			return strStream.str();
		}

		template<typename TMesh>
		static std::string SerializeMeshSelectionToYAML(const Ref<TMesh>& mesh)
		{
			Yaml::Writer out;
			out << Yaml::BeginMap;
			out << Yaml::Key << "Mesh" << Yaml::Value;
			out << Yaml::BeginMap;
			out << Yaml::Key << "MeshSource" << Yaml::Value << mesh->GetMeshSource();
			out << Yaml::Key << "SubmeshIndices" << Yaml::Value;

			if (auto meshSource = AssetManager::GetAsset<MeshSource>(mesh->GetMeshSource());
				meshSource && meshSource->GetSubmeshes().size() == mesh->GetSubmeshes().size())
			{
				out << std::vector<uint32_t>();
			}
			else
			{
				out << mesh->GetSubmeshes();
			}

			out << Yaml::Key << "GenerateColliders" << Yaml::Value << mesh->ShouldGenerateColliders();
			out << Yaml::EndMap;
			out << Yaml::EndMap;
			return std::string(out.c_str());
		}

		static void RegisterMeshDependencyFromYAML(const Yaml::Node& data, AssetHandle handle)
		{
			AssetManager::DeregisterDependencies(handle);

			AssetHandle meshSourceHandle = 0;
			if (auto rootNode = data["Mesh"]; rootNode)
				meshSourceHandle = rootNode["MeshSource"].as<uint64_t>(0);

			AssetManager::RegisterDependency(meshSourceHandle, handle);
		}

		static std::string SerializeMeshColliderToYAML(const Ref<MeshColliderAsset>& collider)
		{
			Yaml::Writer out;
			out << Yaml::BeginMap;
			out << Yaml::Key << "MeshCollider" << Yaml::Value;
			out << Yaml::BeginMap;
			out << Yaml::Key << "Mesh" << Yaml::Value << collider->Mesh;
			out << Yaml::Key << "CollisionComplexity" << Yaml::Value << static_cast<uint32_t>(collider->CollisionComplexity);
			out << Yaml::EndMap;
			out << Yaml::EndMap;
			return std::string(out.c_str());
		}

		static bool DeserializeMeshColliderFromYAML(const Yaml::Node& data, Ref<MeshColliderAsset>& targetCollider)
		{
			Yaml::Node rootNode = data["MeshCollider"];
			if (!rootNode)
				return false;

			AssetHandle mesh = rootNode["Mesh"].as<uint64_t>(0);
			targetCollider = Ref<MeshColliderAsset>::Create(mesh);
			targetCollider->CollisionComplexity = (ECollisionComplexity)rootNode["CollisionComplexity"].as<uint8_t>((uint8_t)ECollisionComplexity::Default);
			return true;
		}

		static void RegisterMeshColliderDependencyFromYAML(const Yaml::Node& data, AssetHandle handle)
		{
			AssetManager::DeregisterDependencies(handle);
			AssetHandle mesh = 0;
			if (auto rootNode = data["MeshCollider"])
				mesh = rootNode["Mesh"].as<uint64_t>(0);
			AssetManager::RegisterDependency(mesh, handle);
		}

		static bool DeserializeMeshSelectionFromYAML(const Yaml::Node& data, Ref<Mesh>& targetMesh)
		{
			if (!data["Mesh"])
				return false;

			Yaml::Node rootNode = data["Mesh"];
			if (!rootNode["MeshAsset"] && !rootNode["MeshSource"])
				return false;

			AssetHandle meshSource = rootNode["MeshAsset"] ? rootNode["MeshAsset"].as<uint64_t>() : rootNode["MeshSource"].as<uint64_t>();
			if (!AssetManager::GetAsset<MeshSource>(meshSource))
				return false;

			std::vector<uint32_t> submeshIndices;
			if (rootNode["SubmeshIndices"])
				submeshIndices = rootNode["SubmeshIndices"].as<std::vector<uint32_t>>();

			const bool generateColliders = rootNode["GenerateColliders"].as<bool>(false);
			targetMesh = Ref<Mesh>::Create(meshSource, submeshIndices, generateColliders);
			return true;
		}

		static bool DeserializeStaticMeshSelectionFromYAML(const Yaml::Node& data, Ref<StaticMesh>& targetStaticMesh)
		{
			if (!data["Mesh"])
				return false;

			Yaml::Node rootNode = data["Mesh"];
			if (!rootNode["MeshAsset"] && !rootNode["MeshSource"])
				return false;

			AssetHandle meshSource = rootNode["MeshAsset"] ? rootNode["MeshAsset"].as<uint64_t>() : rootNode["MeshSource"].as<uint64_t>();
			std::vector<uint32_t> submeshIndices;
			if (rootNode["SubmeshIndices"])
				submeshIndices = rootNode["SubmeshIndices"].as<std::vector<uint32_t>>();

			const bool generateColliders = rootNode["GenerateColliders"].as<bool>(true);
			targetStaticMesh = Ref<StaticMesh>::Create(meshSource, submeshIndices, generateColliders);
			return true;
		}
	}

	void MeshSourceSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
	}

	bool MeshSourceSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
#ifdef LUX_DIST
		LUX_CORE_ERROR("MeshSourceSerializer cannot import source meshes in Dist builds: {}", metadata.FilePath.string());
		return false;
#else
		AssimpMeshImporter importer(Project::GetEditorAssetManager()->GetFileSystemPath(metadata));
		Ref<MeshSource> meshSource = importer.ImportToMeshSource();
		if (!meshSource)
			return false;

		meshSource->Handle = metadata.Handle;
		asset = meshSource;
		return true;
#endif
	}

	bool MeshSourceSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		MeshRuntimeSerializer serializer;
		return serializer.SerializeToAssetPack(handle, stream, outInfo);
	}

	Ref<Asset> MeshSourceSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const
	{
		MeshRuntimeSerializer serializer;
		return serializer.DeserializeFromAssetPack(stream, assetInfo);
	}

	void MeshSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		Ref<Mesh> mesh = asset.As<Mesh>();
		LUX_CORE_ASSERT(mesh);

		std::ofstream fout(Project::GetActive()->GetAssetDirectory() / metadata.FilePath);
		if (!fout.is_open())
		{
			LUX_CORE_ERROR("MeshSerializer: failed to open '{}' for writing", metadata.FilePath.string());
			return;
		}

		fout << SerializeMeshSelectionToYAML(mesh);
	}

	bool MeshSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		std::string yaml = ReadMeshYAML(metadata);
		if (yaml.empty())
			return false;

		Ref<Mesh> mesh;
		Yaml::Node data = Yaml::Load(yaml);
		if (!DeserializeMeshSelectionFromYAML(data, mesh))
			return false;

		mesh->Handle = metadata.Handle;
		RegisterMeshDependencyFromYAML(data, mesh->Handle);
		asset = mesh;
		return true;
	}

	void MeshSerializer::RegisterDependencies(const AssetMetadata& metadata) const
	{
		const std::string yaml = ReadMeshYAML(metadata);
		if (yaml.empty())
		{
			AssetManager::RegisterDependency(0, metadata.Handle);
			return;
		}

		RegisterMeshDependencyFromYAML(Yaml::Load(yaml), metadata.Handle);
	}

	bool MeshSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(handle);
		if (!mesh)
			return false;

		outInfo.Offset = stream.GetStreamPosition();
		stream.WriteString(SerializeMeshSelectionToYAML(mesh));
		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return true;
	}

	Ref<Asset> MeshSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const
	{
		stream.SetStreamPosition(assetInfo.PackedOffset);
		std::string yaml;
		stream.ReadString(yaml);

		Ref<Mesh> mesh;
		if (!DeserializeMeshSelectionFromYAML(Yaml::Load(yaml), mesh))
			return nullptr;

		return mesh;
	}

	void StaticMeshSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		Ref<StaticMesh> staticMesh = asset.As<StaticMesh>();
		LUX_CORE_ASSERT(staticMesh);

		std::ofstream fout(Project::GetActive()->GetAssetDirectory() / metadata.FilePath);
		if (!fout.is_open())
		{
			LUX_CORE_ERROR("StaticMeshSerializer: failed to open '{}' for writing", metadata.FilePath.string());
			return;
		}

		fout << SerializeMeshSelectionToYAML(staticMesh);
	}

	bool StaticMeshSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		std::string yaml = ReadMeshYAML(metadata);
		if (yaml.empty())
			return false;

		Ref<StaticMesh> staticMesh;
		Yaml::Node data = Yaml::Load(yaml);
		if (!DeserializeStaticMeshSelectionFromYAML(data, staticMesh))
			return false;

		staticMesh->Handle = metadata.Handle;
		RegisterMeshDependencyFromYAML(data, staticMesh->Handle);
		asset = staticMesh;
		return true;
	}

	void StaticMeshSerializer::RegisterDependencies(const AssetMetadata& metadata) const
	{
		const std::string yaml = ReadMeshYAML(metadata);
		if (yaml.empty())
		{
			AssetManager::RegisterDependency(0, metadata.Handle);
			return;
		}

		RegisterMeshDependencyFromYAML(Yaml::Load(yaml), metadata.Handle);
	}

	bool StaticMeshSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		Ref<StaticMesh> staticMesh = AssetManager::GetAsset<StaticMesh>(handle);
		if (!staticMesh)
			return false;

		outInfo.Offset = stream.GetStreamPosition();
		stream.WriteString(SerializeMeshSelectionToYAML(staticMesh));
		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return true;
	}

	Ref<Asset> StaticMeshSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const
	{
		stream.SetStreamPosition(assetInfo.PackedOffset);
		std::string yaml;
		stream.ReadString(yaml);

		Ref<StaticMesh> staticMesh;
		if (!DeserializeStaticMeshSelectionFromYAML(Yaml::Load(yaml), staticMesh))
			return nullptr;

		return staticMesh;
	}

	void MeshColliderSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		Ref<MeshColliderAsset> collider = asset.As<MeshColliderAsset>();
		LUX_CORE_ASSERT(collider);

		std::ofstream fout(Project::GetActive()->GetAssetDirectory() / metadata.FilePath);
		if (!fout.is_open())
		{
			LUX_CORE_ERROR("MeshColliderSerializer: failed to open '{}' for writing", metadata.FilePath.string());
			return;
		}

		fout << SerializeMeshColliderToYAML(collider);
	}

	bool MeshColliderSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		std::string yaml = ReadMeshYAML(metadata);
		if (yaml.empty())
			return false;

		Ref<MeshColliderAsset> collider;
		Yaml::Node data = Yaml::Load(yaml);
		if (!DeserializeMeshColliderFromYAML(data, collider))
			return false;

		collider->Handle = metadata.Handle;
		RegisterMeshColliderDependencyFromYAML(data, collider->Handle);
		asset = collider;
		return true;
	}

	void MeshColliderSerializer::RegisterDependencies(const AssetMetadata& metadata) const
	{
		const std::string yaml = ReadMeshYAML(metadata);
		if (yaml.empty())
		{
			AssetManager::RegisterDependency(0, metadata.Handle);
			return;
		}

		RegisterMeshColliderDependencyFromYAML(Yaml::Load(yaml), metadata.Handle);
	}

	bool MeshColliderSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		Ref<MeshColliderAsset> collider = AssetManager::GetAsset<MeshColliderAsset>(handle);
		if (!collider)
			return false;

		outInfo.Offset = stream.GetStreamPosition();
		stream.WriteString(SerializeMeshColliderToYAML(collider));
		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return true;
	}

	Ref<Asset> MeshColliderSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const
	{
		stream.SetStreamPosition(assetInfo.PackedOffset);
		std::string yaml;
		stream.ReadString(yaml);

		Ref<MeshColliderAsset> collider;
		if (!DeserializeMeshColliderFromYAML(Yaml::Load(yaml), collider))
			return nullptr;

		return collider;
	}
}
