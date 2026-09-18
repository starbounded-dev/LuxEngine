#include "lpch.h"
#include "MaterialSerializer.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Material.h"
#include "Lux/Renderer/MaterialAsset.h"

#include <fstream>
#include <sstream>
#include <charconv>
#include <system_error>

#include <yaml-cpp/yaml.h>

namespace Lux
{
	namespace
	{
		enum class TextureReferenceSerialization
		{
			AssetHandles,
			AssetPaths
		};

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

		static void WriteVec2(YAML::Emitter& out, const glm::vec2& value)
		{
			out << YAML::Flow << YAML::BeginSeq << value.x << value.y << YAML::EndSeq;
		}

		static glm::vec2 ReadVec2(const YAML::Node& node, const glm::vec2& fallback)
		{
			if (!node || !node.IsSequence() || node.size() < 2)
				return fallback;

			return { node[0].as<float>(), node[1].as<float>() };
		}

		static const char* ChannelToString(MaterialTextureChannel channel)
		{
			switch (channel)
			{
				case MaterialTextureChannel::R: return "R";
				case MaterialTextureChannel::G: return "G";
				case MaterialTextureChannel::B: return "B";
				case MaterialTextureChannel::A: return "A";
			}
			return "R";
		}

		static MaterialTextureChannel ReadChannel(const YAML::Node& node, MaterialTextureChannel fallback)
		{
			const std::string value = node ? node.as<std::string>("") : std::string{};
			if (value == "R")
				return MaterialTextureChannel::R;
			if (value == "G")
				return MaterialTextureChannel::G;
			if (value == "B")
				return MaterialTextureChannel::B;
			if (value == "A")
				return MaterialTextureChannel::A;
			return fallback;
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

		static bool TryParseAssetHandle(std::string_view value, AssetHandle& handle)
		{
			if (value.empty())
				return false;

			uint64_t parsedHandle = 0;
			const char* begin = value.data();
			const char* end = value.data() + value.size();
			const auto [ptr, error] = std::from_chars(begin, end, parsedHandle);
			if (error != std::errc() || ptr != end)
				return false;

			handle = parsedHandle;
			return true;
		}

		static AssetHandle ResolveTextureHandle(AssetHandle textureHandle)
		{
			if (!textureHandle || !Project::GetAssetManager() || !AssetManager::IsAssetHandleValid(textureHandle))
				return 0;

			const AssetType assetType = AssetManager::GetAssetType(textureHandle);
			return assetType == AssetType::Texture ? textureHandle : AssetHandle(0);
		}

		static AssetHandle ResolveTexturePath(const std::filesystem::path& texturePath)
		{
			if (texturePath.empty())
				return 0;

			Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager();
			if (!editorAssetManager)
				return 0;

			AssetHandle textureHandle = editorAssetManager->GetAssetHandleFromFilePath(texturePath);
			if (!textureHandle)
				textureHandle = editorAssetManager->ImportAsset(texturePath);

			return ResolveTextureHandle(textureHandle);
		}

		static AssetHandle ResolveTextureReference(const YAML::Node& node)
		{
			if (!node)
				return 0;

			if (node.IsMap())
			{
				if (AssetHandle textureHandle = ResolveTexturePath(node["Path"].as<std::string>("")))
					return textureHandle;

				return ResolveTextureReference(node["Handle"]);
			}

			if (!node.IsScalar())
				return 0;

			const std::string reference = node.as<std::string>("");
			AssetHandle textureHandle = 0;
			if (TryParseAssetHandle(reference, textureHandle))
				return ResolveTextureHandle(textureHandle);

			return ResolveTexturePath(reference);
		}

		static void WriteTextureReference(YAML::Emitter& out, const char* key, AssetHandle textureHandle, TextureReferenceSerialization textureReferenceSerialization)
		{
			out << YAML::Key << key << YAML::Value;

			if (textureReferenceSerialization == TextureReferenceSerialization::AssetPaths && textureHandle)
			{
				if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
				{
					AssetMetadata metadata = editorAssetManager->GetMetadata(textureHandle);
					if (metadata.IsValid() && metadata.Type == AssetType::Texture && !metadata.FilePath.empty())
					{
						out << metadata.FilePath.generic_string();
						return;
					}
				}
			}

			out << textureHandle;
		}

		static std::string SerializeMaterialToYAML(Ref<MaterialAsset> materialAsset, TextureReferenceSerialization textureReferenceSerialization)
		{
			if (!materialAsset || !materialAsset->GetMaterial())
				return {};

			const bool transparent = materialAsset->IsTransparent();
			const glm::vec3 albedoColor = materialAsset->GetAlbedoColor();
			const float emission = materialAsset->GetEmission();
			const bool useNormalMap = materialAsset->IsUsingNormalMap();
			const float metalness = transparent ? 0.0f : materialAsset->GetMetalness();
			const float roughness = materialAsset->GetRoughness();
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

			// The transparent shader uses roughness and the normal map too; only metalness is opaque-only.
			out << YAML::Key << "UseNormalMap" << YAML::Value << useNormalMap;
			if (!transparent)
				out << YAML::Key << "Metalness" << YAML::Value << metalness;
			out << YAML::Key << "Roughness" << YAML::Value << roughness;
			if (transparent)
				out << YAML::Key << "Transparency" << YAML::Value << transparency;

			WriteTextureReference(out, "AlbedoMap", albedoMap, textureReferenceSerialization);
			WriteTextureReference(out, "NormalMap", normalMap, textureReferenceSerialization);
			WriteTextureReference(out, "MetalnessMap", metalnessMap, textureReferenceSerialization);
			WriteTextureReference(out, "RoughnessMap", roughnessMap, textureReferenceSerialization);

			// Standard inputs are written only when they differ from the default, so materials that do
			// not use them keep their files unchanged. EmissiveColor is always written once emission is
			// on: its absence marks a pre-emissive-colour file, which loads through the migration.
			const MaterialSurfaceParameters& surface = materialAsset->GetSurfaceParameters();
			const MaterialSurfaceParameters defaults;
			if (emission > 0.0f || surface.EmissiveColor != defaults.EmissiveColor)
			{
				out << YAML::Key << "EmissiveColor" << YAML::Value;
				WriteVec3(out, surface.EmissiveColor);
			}
			if (surface.EmissiveMap)
				WriteTextureReference(out, "EmissiveMap", surface.EmissiveMap, textureReferenceSerialization);
			if (surface.OcclusionMap)
				WriteTextureReference(out, "OcclusionMap", surface.OcclusionMap, textureReferenceSerialization);
			if (surface.OcclusionStrength != defaults.OcclusionStrength)
				out << YAML::Key << "OcclusionStrength" << YAML::Value << surface.OcclusionStrength;
			if (surface.OcclusionChannel != defaults.OcclusionChannel)
				out << YAML::Key << "OcclusionChannel" << YAML::Value << ChannelToString(surface.OcclusionChannel);
			if (surface.MetalnessChannel != defaults.MetalnessChannel)
				out << YAML::Key << "MetalnessChannel" << YAML::Value << ChannelToString(surface.MetalnessChannel);
			if (surface.RoughnessChannel != defaults.RoughnessChannel)
				out << YAML::Key << "RoughnessChannel" << YAML::Value << ChannelToString(surface.RoughnessChannel);
			if (surface.Specular != defaults.Specular)
				out << YAML::Key << "Specular" << YAML::Value << surface.Specular;
			if (surface.NormalStrength != defaults.NormalStrength)
				out << YAML::Key << "NormalStrength" << YAML::Value << surface.NormalStrength;
			if (surface.HeightMap)
				WriteTextureReference(out, "HeightMap", surface.HeightMap, textureReferenceSerialization);
			if (surface.BumpHeight != defaults.BumpHeight)
				out << YAML::Key << "BumpHeight" << YAML::Value << surface.BumpHeight;
			if (surface.UVTiling != defaults.UVTiling)
			{
				out << YAML::Key << "UVTiling" << YAML::Value;
				WriteVec2(out, surface.UVTiling);
			}
			if (surface.UVOffset != defaults.UVOffset)
			{
				out << YAML::Key << "UVOffset" << YAML::Value;
				WriteVec2(out, surface.UVOffset);
			}
			if (surface.UVRotation != defaults.UVRotation)
				out << YAML::Key << "UVRotation" << YAML::Value << surface.UVRotation;

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

			const AssetHandle albedoMap = ResolveTextureReference(materialNode["AlbedoMap"]);
			const AssetHandle normalMap = ResolveTextureReference(materialNode["NormalMap"]);
			const AssetHandle metalnessMap = ResolveTextureReference(materialNode["MetalnessMap"]);
			const AssetHandle roughnessMap = ResolveTextureReference(materialNode["RoughnessMap"]);

			AssetManager::RegisterDependency(albedoMap, handle);
			AssetManager::RegisterDependency(normalMap, handle);
			AssetManager::RegisterDependency(metalnessMap, handle);
			AssetManager::RegisterDependency(roughnessMap, handle);
			AssetManager::RegisterDependency(ResolveTextureReference(materialNode["EmissiveMap"]), handle);
			AssetManager::RegisterDependency(ResolveTextureReference(materialNode["OcclusionMap"]), handle);
			AssetManager::RegisterDependency(ResolveTextureReference(materialNode["HeightMap"]), handle);
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

			targetMaterialAsset->SetUseNormalMap(materialNode["UseNormalMap"].as<bool>(false));
			targetMaterialAsset->SetRoughness(materialNode["Roughness"].as<float>(0.5f));
			if (!transparent)
				targetMaterialAsset->SetMetalness(materialNode["Metalness"].as<float>(0.0f));
			else
				targetMaterialAsset->SetTransparency(materialNode["Transparency"].as<float>(1.0f));

			const auto tryAssignTexture = [&targetMaterialAsset](const YAML::Node& textureNode, auto&& assignFn)
			{
				if (AssetHandle textureHandle = ResolveTextureReference(textureNode))
					assignFn(textureHandle);
			};

			tryAssignTexture(materialNode["AlbedoMap"], [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetAlbedoMap(handle); });
			tryAssignTexture(materialNode["NormalMap"], [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetNormalMap(handle); });
			tryAssignTexture(materialNode["MetalnessMap"], [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetMetalnessMap(handle); });
			tryAssignTexture(materialNode["RoughnessMap"], [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetRoughnessMap(handle); });

			MaterialSurfaceParameters surface;
			surface.EmissiveColor = ReadVec3(materialNode["EmissiveColor"], surface.EmissiveColor);
			surface.EmissiveMap = ResolveTextureReference(materialNode["EmissiveMap"]);
			surface.OcclusionMap = ResolveTextureReference(materialNode["OcclusionMap"]);
			surface.OcclusionStrength = materialNode["OcclusionStrength"].as<float>(surface.OcclusionStrength);
			surface.OcclusionChannel = ReadChannel(materialNode["OcclusionChannel"], surface.OcclusionChannel);
			surface.MetalnessChannel = ReadChannel(materialNode["MetalnessChannel"], surface.MetalnessChannel);
			surface.RoughnessChannel = ReadChannel(materialNode["RoughnessChannel"], surface.RoughnessChannel);
			surface.Specular = materialNode["Specular"].as<float>(surface.Specular);
			surface.NormalStrength = materialNode["NormalStrength"].as<float>(surface.NormalStrength);
			surface.HeightMap = ResolveTextureReference(materialNode["HeightMap"]);
			surface.BumpHeight = materialNode["BumpHeight"].as<float>(surface.BumpHeight);
			surface.UVTiling = ReadVec2(materialNode["UVTiling"], surface.UVTiling);
			surface.UVOffset = ReadVec2(materialNode["UVOffset"], surface.UVOffset);
			surface.UVRotation = materialNode["UVRotation"].as<float>(surface.UVRotation);

			// Files written before emissive colour existed tinted emission by the base colour and its
			// map. Carry that over as data so they render exactly as they did.
			if (!materialNode["EmissiveColor"] && targetMaterialAsset->GetEmission() > 0.0f)
			{
				surface.EmissiveColor = targetMaterialAsset->GetAlbedoColor();
				surface.EmissiveMap = targetMaterialAsset->GetAlbedoMapHandle();
			}
			targetMaterialAsset->SetSurfaceParameters(surface);

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

		const std::string serializedMaterial = SerializeMaterialToYAML(materialAsset, TextureReferenceSerialization::AssetPaths);
		if (serializedMaterial.empty())
			return;

		if (ReadMaterialYAML(metadata) == serializedMaterial)
			return;

		std::ofstream fout(Project::GetEditorAssetManager()->GetFileSystemPath(metadata));
		if (!fout.is_open())
		{
			LUX_CORE_ERROR("MaterialSerializer: Could not open '{}' for writing", metadata.FilePath.string());
			return;
		}

		fout << serializedMaterial;
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
		stream.WriteString(SerializeMaterialToYAML(materialAsset, TextureReferenceSerialization::AssetHandles));
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
