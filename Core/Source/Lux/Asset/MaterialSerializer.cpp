#include "lpch.h"
#include "MaterialSerializer.h"

#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Project/Project.h"

#include <fstream>
#include <yaml-cpp/yaml.h>

#include <glm/glm.hpp>

namespace Lux
{
	static void WriteVec3(YAML::Emitter& out, const glm::vec3& v)
	{
		out << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
	}

	void MaterialSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		Ref<MaterialAsset> materialAsset = asset.As<MaterialAsset>();
		LUX_CORE_ASSERT(materialAsset);

		auto filepath = Project::GetActiveAssetDirectory() / metadata.FilePath;

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Material" << YAML::Value;
		out << YAML::BeginMap;

		out << YAML::Key << "AlbedoColor"  << YAML::Value;
		WriteVec3(out, materialAsset->GetAlbedoColor());
		out << YAML::Key << "Metalness"    << YAML::Value << materialAsset->GetMetalness();
		out << YAML::Key << "Roughness"    << YAML::Value << materialAsset->GetRoughness();
		out << YAML::Key << "Emission"     << YAML::Value << materialAsset->GetEmission();
		out << YAML::Key << "Transparency" << YAML::Value << materialAsset->GetTransparency();
		out << YAML::Key << "Transparent"  << YAML::Value << materialAsset->IsTransparent();
		out << YAML::Key << "ShadowCasting"<< YAML::Value << materialAsset->IsShadowCasting();
		out << YAML::Key << "UseNormalMap" << YAML::Value << materialAsset->IsUsingNormalMap();

		out << YAML::EndMap;
		out << YAML::EndMap;

		std::ofstream fout(filepath);
		fout << out.c_str();
	}

	bool MaterialSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		auto filepath = Project::GetActiveAssetDirectory() / metadata.FilePath;

		YAML::Node data;
		try
		{
			data = YAML::LoadFile(filepath.string());
		}
		catch (const YAML::ParserException& e)
		{
			LUX_CORE_ERROR("MaterialSerializer: Failed to parse '{}': {}", filepath.string(), e.what());
			return false;
		}

		auto matNode = data["Material"];
		if (!matNode)
			return false;

		bool transparent = matNode["Transparent"].as<bool>(false);
		Ref<MaterialAsset> materialAsset = Ref<MaterialAsset>::Create(transparent);
		materialAsset->Handle = metadata.Handle;

		if (auto node = matNode["AlbedoColor"])
		{
			auto seq = node.as<std::vector<float>>();
			if (seq.size() >= 3)
				materialAsset->SetAlbedoColor({ seq[0], seq[1], seq[2] });
		}
		materialAsset->SetMetalness(matNode["Metalness"].as<float>(0.0f));
		materialAsset->SetRoughness(matNode["Roughness"].as<float>(0.5f));
		materialAsset->SetEmission(matNode["Emission"].as<float>(0.0f));
		materialAsset->SetTransparency(matNode["Transparency"].as<float>(1.0f));
		materialAsset->SetShadowCasting(matNode["ShadowCasting"].as<bool>(true));
		materialAsset->SetUseNormalMap(matNode["UseNormalMap"].as<bool>(false));

		asset = materialAsset;
		return true;
	}
}
