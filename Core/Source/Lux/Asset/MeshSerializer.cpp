#include "lpch.h"
#include "MeshSerializer.h"

#include "AssimpMeshImporter.h"

#include "Lux/Project/Project.h"
#include "Lux/Renderer/Mesh.h"

#include <fstream>
#include <yaml-cpp/yaml.h>

namespace Lux
{
	// ─── MeshSourceSerializer ────────────────────────────────────────────────

	void MeshSourceSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		// The source file (fbx / gltf / obj …) IS the serialized form – nothing to write back.
	}

	bool MeshSourceSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		auto filepath = Project::GetActiveAssetDirectory() / metadata.FilePath;
		AssimpMeshImporter importer(filepath);
		Ref<MeshSource> meshSource = importer.ImportToMeshSource();
		if (!meshSource)
			return false;

		meshSource->Handle = metadata.Handle;
		asset = meshSource;
		return true;
	}

	// ─── MeshSerializer ──────────────────────────────────────────────────────

	void MeshSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		Ref<Mesh> mesh = asset.As<Mesh>();
		LUX_CORE_ASSERT(mesh);

		auto filepath = Project::GetActiveAssetDirectory() / metadata.FilePath;

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Mesh" << YAML::Value;
		out << YAML::BeginMap;
		out << YAML::Key << "MeshSource" << YAML::Value << (uint64_t)mesh->GetMeshSource();
		out << YAML::Key << "GenerateColliders" << YAML::Value << mesh->ShouldGenerateColliders();

		out << YAML::Key << "Submeshes" << YAML::Value << YAML::BeginSeq;
		for (uint32_t idx : mesh->GetSubmeshes())
			out << idx;
		out << YAML::EndSeq;

		out << YAML::EndMap;
		out << YAML::EndMap;

		std::ofstream fout(filepath);
		if (!fout.is_open())
		{
			LUX_CORE_ERROR("MeshSerializer: Could not open '{}' for writing", filepath.string());
			return;
		}
		fout << out.c_str();
	}

	bool MeshSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		auto filepath = Project::GetActiveAssetDirectory() / metadata.FilePath;

		YAML::Node data;
		try
		{
			data = YAML::LoadFile(filepath.string());
		}
		catch (const YAML::ParserException& e)
		{
			LUX_CORE_ERROR("MeshSerializer: Failed to parse '{}': {}", filepath.string(), e.what());
			return false;
		}

		auto meshNode = data["Mesh"];
		if (!meshNode)
			return false;

		AssetHandle meshSource = meshNode["MeshSource"].as<uint64_t>(0);
		bool generateColliders = meshNode["GenerateColliders"].as<bool>(false);

		std::vector<uint32_t> submeshes;
		if (auto submeshesNode = meshNode["Submeshes"])
		{
			for (const auto& node : submeshesNode)
				submeshes.push_back(node.as<uint32_t>());
		}

		Ref<Mesh> mesh = Ref<Mesh>::Create(meshSource, submeshes, generateColliders);
		mesh->Handle = metadata.Handle;
		asset = mesh;
		return true;
	}

	// ─── StaticMeshSerializer ─────────────────────────────────────────────────

	void StaticMeshSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		Ref<StaticMesh> mesh = asset.As<StaticMesh>();
		LUX_CORE_ASSERT(mesh);

		auto filepath = Project::GetActiveAssetDirectory() / metadata.FilePath;

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "StaticMesh" << YAML::Value;
		out << YAML::BeginMap;
		out << YAML::Key << "MeshSource" << YAML::Value << (uint64_t)mesh->GetMeshSource();
		out << YAML::Key << "GenerateColliders" << YAML::Value << mesh->ShouldGenerateColliders();

		out << YAML::Key << "Submeshes" << YAML::Value << YAML::BeginSeq;
		for (uint32_t idx : mesh->GetSubmeshes())
			out << idx;
		out << YAML::EndSeq;

		out << YAML::EndMap;
		out << YAML::EndMap;

		std::ofstream fout(filepath);
		if (!fout.is_open())
		{
			LUX_CORE_ERROR("StaticMeshSerializer: Could not open '{}' for writing", filepath.string());
			return;
		}
		fout << out.c_str();
	}

	bool StaticMeshSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		auto filepath = Project::GetActiveAssetDirectory() / metadata.FilePath;

		YAML::Node data;
		try
		{
			data = YAML::LoadFile(filepath.string());
		}
		catch (const YAML::ParserException& e)
		{
			LUX_CORE_ERROR("StaticMeshSerializer: Failed to parse '{}': {}", filepath.string(), e.what());
			return false;
		}

		auto meshNode = data["StaticMesh"];
		if (!meshNode)
			return false;

		AssetHandle meshSource = meshNode["MeshSource"].as<uint64_t>(0);
		bool generateColliders = meshNode["GenerateColliders"].as<bool>(false);

		std::vector<uint32_t> submeshes;
		if (auto submeshesNode = meshNode["Submeshes"])
		{
			for (const auto& node : submeshesNode)
				submeshes.push_back(node.as<uint32_t>());
		}

		Ref<StaticMesh> mesh = Ref<StaticMesh>::Create(meshSource, submeshes, generateColliders);
		mesh->Handle = metadata.Handle;
		asset = mesh;
		return true;
	}
}
