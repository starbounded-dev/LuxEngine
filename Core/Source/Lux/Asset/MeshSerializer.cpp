#include "lpch.h"
#include "MeshSerializer.h"

#include "MeshRuntimeSerializer.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Mesh.h"

#ifndef LUX_DIST
#include "AssimpMeshImporter.h"
#endif

#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace YAML
{
	template<>
	struct convert<std::vector<uint32_t>>
	{
		static Node encode(const std::vector<uint32_t>& value)
		{
			Node node;
			for (uint32_t element : value)
				node.push_back(element);
			return node;
		}

		static bool decode(const Node& node, std::vector<uint32_t>& result)
		{
			if (!node.IsSequence())
				return false;

			result.resize(node.size());
			for (size_t i = 0; i < node.size(); i++)
				result[i] = node[i].as<uint32_t>();
			return true;
		}
	};
}

namespace Lux
{
	namespace
	{
		YAML::Emitter& operator<<(YAML::Emitter& out, const std::vector<uint32_t>& value)
		{
			out << YAML::Flow << YAML::BeginSeq;
			for (uint32_t element : value)
				out << element;
			out << YAML::EndSeq;
			return out;
		}

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
			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << "Mesh" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "MeshSource" << YAML::Value << mesh->GetMeshSource();
			out << YAML::Key << "SubmeshIndices" << YAML::Value;

			if (auto meshSource = AssetManager::GetAsset<MeshSource>(mesh->GetMeshSource());
				meshSource && meshSource->GetSubmeshes().size() == mesh->GetSubmeshes().size())
			{
				out << std::vector<uint32_t>();
			}
			else
			{
				out << mesh->GetSubmeshes();
			}

			out << YAML::Key << "GenerateColliders" << YAML::Value << mesh->ShouldGenerateColliders();
			out << YAML::EndMap;
			out << YAML::EndMap;
			return std::string(out.c_str());
		}

		static void RegisterMeshDependencyFromYAML(const YAML::Node& data, AssetHandle handle)
		{
			AssetManager::DeregisterDependencies(handle);

			AssetHandle meshSourceHandle = 0;
			if (auto rootNode = data["Mesh"]; rootNode)
				meshSourceHandle = rootNode["MeshSource"].as<uint64_t>(0);

			AssetManager::RegisterDependency(meshSourceHandle, handle);
		}

		static bool DeserializeMeshSelectionFromYAML(const YAML::Node& data, Ref<Mesh>& targetMesh)
		{
			if (!data["Mesh"])
				return false;

			YAML::Node rootNode = data["Mesh"];
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

		static bool DeserializeStaticMeshSelectionFromYAML(const YAML::Node& data, Ref<StaticMesh>& targetStaticMesh)
		{
			if (!data["Mesh"])
				return false;

			YAML::Node rootNode = data["Mesh"];
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
		YAML::Node data = YAML::Load(yaml);
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

		RegisterMeshDependencyFromYAML(YAML::Load(yaml), metadata.Handle);
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
		if (!DeserializeMeshSelectionFromYAML(YAML::Load(yaml), mesh))
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
		YAML::Node data = YAML::Load(yaml);
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

		RegisterMeshDependencyFromYAML(YAML::Load(yaml), metadata.Handle);
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
		if (!DeserializeStaticMeshSelectionFromYAML(YAML::Load(yaml), staticMesh))
			return nullptr;

		return staticMesh;
	}
}
