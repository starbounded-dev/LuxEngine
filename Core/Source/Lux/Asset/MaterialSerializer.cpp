#include "lpch.h"
#include "MaterialSerializer.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Material.h"
#include "Lux/Renderer/MaterialAsset.h"

#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace Lux
{
	namespace
	{
		static void WriteVec3(YAML::Emitter& out, const glm::vec3& value)
		{
			out << YAML::Flow << YAML::BeginSeq << value.x << value.y << value.z << YAML::EndSeq;
		}

		static glm::vec3 ReadVec3(const YAML::Node& node, const glm::vec3& fallback)
		{
			if (!node || !node.IsSequence() || node.size() < 3)
				return fallback;

			return { node[0].as<float>(), node[1].as<float>(), node[2].as<float>() };
		}

		static std::string ReadMaterialYAML(const AssetMetadata& metadata)
		{
			std::ifstream stream(Project::GetEditorAssetManager()->GetFileSystemPath(metadata));
			if (!stream.is_open())
				return {};

			std::stringstream strStream;
			strStream << stream.rdbuf();
			return strStream.str();
		}

		static std::string SerializeMaterialToYAML(Ref<MaterialAsset> materialAsset)
		{
			if (!materialAsset || !materialAsset->GetMaterial())
				return {};

			const bool transparent = materialAsset->IsTransparent();
			const glm::vec3 albedoColor = materialAsset->GetAlbedoColor();
			const float emission = materialAsset->GetEmission();
			const bool useNormalMap = transparent ? false : materialAsset->IsUsingNormalMap();
			const float metalness = transparent ? 0.0f : materialAsset->GetMetalness();
			const float roughness = transparent ? 0.5f : materialAsset->GetRoughness();
			const float transparency = transparent ? materialAsset->GetTransparency() : 1.0f;
			const AssetHandle albedoMap = materialAsset->GetAlbedoMapHandle();
			const AssetHandle normalMap = materialAsset->GetNormalMapHandle();
			const AssetHandle metalnessMap = materialAsset->GetMetalnessMapHandle();
			const AssetHandle roughnessMap = materialAsset->GetRoughnessMapHandle();
			const uint32_t materialFlags = materialAsset->GetMaterial()->GetFlags();

			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << "Material" << YAML::Value;
			out << YAML::BeginMap;

			out << YAML::Key << "Transparent" << YAML::Value << transparent;
			out << YAML::Key << "AlbedoColor" << YAML::Value;
			WriteVec3(out, albedoColor);
			out << YAML::Key << "Emission" << YAML::Value << emission;

			if (!transparent)
			{
				out << YAML::Key << "UseNormalMap" << YAML::Value << useNormalMap;
				out << YAML::Key << "Metalness" << YAML::Value << metalness;
				out << YAML::Key << "Roughness" << YAML::Value << roughness;
			}
			else
			{
				out << YAML::Key << "Transparency" << YAML::Value << transparency;
			}

			out << YAML::Key << "AlbedoMap" << YAML::Value << albedoMap;
			out << YAML::Key << "NormalMap" << YAML::Value << normalMap;
			out << YAML::Key << "MetalnessMap" << YAML::Value << metalnessMap;
			out << YAML::Key << "RoughnessMap" << YAML::Value << roughnessMap;
			out << YAML::Key << "MaterialFlags" << YAML::Value << materialFlags;

			out << YAML::EndMap;
			out << YAML::EndMap;
			return std::string(out.c_str());
		}

		static void RegisterMaterialDependenciesFromYAML(const std::string& yamlString, AssetHandle handle)
		{
			AssetManager::DeregisterDependencies(handle);

			if (yamlString.empty())
			{
				AssetManager::RegisterDependency(0, handle);
				return;
			}

			YAML::Node root = YAML::Load(yamlString);
			YAML::Node materialNode = root["Material"];

			const AssetHandle albedoMap = materialNode["AlbedoMap"].as<uint64_t>(0);
			const AssetHandle normalMap = materialNode["NormalMap"].as<uint64_t>(0);
			const AssetHandle metalnessMap = materialNode["MetalnessMap"].as<uint64_t>(0);
			const AssetHandle roughnessMap = materialNode["RoughnessMap"].as<uint64_t>(0);

			AssetManager::RegisterDependency(albedoMap, handle);
			AssetManager::RegisterDependency(normalMap, handle);
			AssetManager::RegisterDependency(metalnessMap, handle);
			AssetManager::RegisterDependency(roughnessMap, handle);
		}

		static bool DeserializeMaterialFromYAML(const std::string& yamlString, Ref<MaterialAsset>& targetMaterialAsset, AssetHandle handle)
		{
			if (yamlString.empty())
				return false;

			RegisterMaterialDependenciesFromYAML(yamlString, handle);

			YAML::Node root = YAML::Load(yamlString);
			YAML::Node materialNode = root["Material"];
			if (!materialNode)
				return false;

			const bool transparent = materialNode["Transparent"].as<bool>(false);
			targetMaterialAsset = Ref<MaterialAsset>::Create(transparent);
			targetMaterialAsset->Handle = handle;
			if (!targetMaterialAsset->GetMaterial())
			{
				LUX_CORE_ERROR("MaterialSerializer: Failed to create renderer material while loading material asset {}", (uint64_t)handle);
				return false;
			}

			targetMaterialAsset->SetAlbedoColor(ReadVec3(materialNode["AlbedoColor"], glm::vec3(0.8f)));
			targetMaterialAsset->SetEmission(materialNode["Emission"].as<float>(0.0f));

			if (!transparent)
			{
				targetMaterialAsset->SetUseNormalMap(materialNode["UseNormalMap"].as<bool>(false));
				targetMaterialAsset->SetMetalness(materialNode["Metalness"].as<float>(0.0f));
				targetMaterialAsset->SetRoughness(materialNode["Roughness"].as<float>(0.5f));
			}
			else
			{
				targetMaterialAsset->SetTransparency(materialNode["Transparency"].as<float>(1.0f));
			}

			const auto tryAssignTexture = [&targetMaterialAsset](AssetHandle textureHandle, auto&& assignFn)
			{
				if (textureHandle && AssetManager::IsAssetHandleValid(textureHandle))
					assignFn(textureHandle);
			};

			tryAssignTexture(materialNode["AlbedoMap"].as<uint64_t>(0), [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetAlbedoMap(handle); });
			tryAssignTexture(materialNode["NormalMap"].as<uint64_t>(0), [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetNormalMap(handle); });
			tryAssignTexture(materialNode["MetalnessMap"].as<uint64_t>(0), [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetMetalnessMap(handle); });
			tryAssignTexture(materialNode["RoughnessMap"].as<uint64_t>(0), [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetRoughnessMap(handle); });

			if (materialNode["MaterialFlags"])
				targetMaterialAsset->GetMaterial()->SetFlags(materialNode["MaterialFlags"].as<uint32_t>());

			return true;
		}
	}

	void MaterialSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		Ref<MaterialAsset> materialAsset = asset.As<MaterialAsset>();
		LUX_CORE_ASSERT(materialAsset);
		if (!materialAsset || !materialAsset->GetMaterial())
		{
			LUX_CORE_ERROR("MaterialSerializer: Cannot serialize material '{}' because renderer resources are unavailable", metadata.FilePath.string());
			return;
		}

		std::ofstream fout(Project::GetEditorAssetManager()->GetFileSystemPath(metadata));
		if (!fout.is_open())
		{
			LUX_CORE_ERROR("MaterialSerializer: Could not open '{}' for writing", metadata.FilePath.string());
			return;
		}

		fout << SerializeMaterialToYAML(materialAsset);
	}

	bool MaterialSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		Ref<MaterialAsset> materialAsset;
		if (!DeserializeMaterialFromYAML(ReadMaterialYAML(metadata), materialAsset, metadata.Handle))
			return false;

		asset = materialAsset;
		return true;
	}

	void MaterialSerializer::RegisterDependencies(const AssetMetadata& metadata) const
	{
		RegisterMaterialDependenciesFromYAML(ReadMaterialYAML(metadata), metadata.Handle);
	}

	bool MaterialSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		Ref<MaterialAsset> materialAsset = AssetManager::GetAsset<MaterialAsset>(handle);
		if (!materialAsset || !materialAsset->GetMaterial())
			return false;

		outInfo.Offset = stream.GetStreamPosition();
		stream.WriteString(SerializeMaterialToYAML(materialAsset));
		outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
		return true;
	}

	Ref<Asset> MaterialSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const
	{
		stream.SetStreamPosition(assetInfo.PackedOffset);
		std::string yamlString;
		stream.ReadString(yamlString);

		Ref<MaterialAsset> materialAsset;
		if (!DeserializeMaterialFromYAML(yamlString, materialAsset, 0))
			return nullptr;

		return materialAsset;
	}
}
