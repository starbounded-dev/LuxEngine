// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MaterialSerializer.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AcousticMaterial.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Material.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/MaterialGraph/MaterialGraphRegistry.h"
#include "Lux/Utilities/StringUtils.h"

#include <fstream>
#include <sstream>
#include <charconv>
#include <system_error>

#include "Lux/Serialization/Yaml.h"

namespace Lux
{
	namespace
	{
		enum class TextureReferenceSerialization
		{
			AssetHandles,
			AssetPaths
		};

		static void WriteVec3(Yaml::Writer& out, const glm::vec3& value)
		{
			out << Yaml::Flow << Yaml::BeginSeq << value.x << value.y << value.z << Yaml::EndSeq;
		}

		static glm::vec3 ReadVec3(const Yaml::Node& node, const glm::vec3& fallback)
		{
			if (!node || !node.IsSequence() || node.size() < 3)
				return fallback;

			return { node[0].as<float>(), node[1].as<float>(), node[2].as<float>() };
		}

		static void WriteVec2(Yaml::Writer& out, const glm::vec2& value)
		{
			out << Yaml::Flow << Yaml::BeginSeq << value.x << value.y << Yaml::EndSeq;
		}

		static glm::vec2 ReadVec2(const Yaml::Node& node, const glm::vec2& fallback)
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

		static MaterialTextureChannel ReadChannel(const Yaml::Node& node, MaterialTextureChannel fallback)
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

		static const char* AlphaModeToString(MaterialAlphaMode mode)
		{
			switch (mode)
			{
				case MaterialAlphaMode::Opaque: return "Opaque";
				case MaterialAlphaMode::Cutout: return "Cutout";
				case MaterialAlphaMode::Blend: return "Blend";
			}
			return "Opaque";
		}

		static MaterialAlphaMode ReadAlphaMode(const Yaml::Node& node, MaterialAlphaMode fallback)
		{
			const std::string value = node ? node.as<std::string>("") : std::string{};
			if (value == "Opaque")
				return MaterialAlphaMode::Opaque;
			if (value == "Cutout")
				return MaterialAlphaMode::Cutout;
			if (value == "Blend")
				return MaterialAlphaMode::Blend;
			return fallback;
		}

		static const char* MappingToString(MaterialMapping mapping)
		{
			switch (mapping)
			{
				case MaterialMapping::UV: return "UV";
				case MaterialMapping::TriplanarWorld: return "TriplanarWorld";
				case MaterialMapping::TriplanarObject: return "TriplanarObject";
			}
			return "UV";
		}

		static MaterialMapping ReadMapping(const Yaml::Node& node, MaterialMapping fallback)
		{
			const std::string value = node ? node.as<std::string>("") : std::string{};
			if (value == "UV")
				return MaterialMapping::UV;
			if (value == "TriplanarWorld")
				return MaterialMapping::TriplanarWorld;
			if (value == "TriplanarObject")
				return MaterialMapping::TriplanarObject;
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

		static AssetHandle ResolveTextureReference(const Yaml::Node& node)
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

		static void WriteTextureReference(Yaml::Writer& out, const char* key, AssetHandle textureHandle, TextureReferenceSerialization textureReferenceSerialization)
		{
			out << Yaml::Key << key << Yaml::Value;

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

			Yaml::Writer out;
			out << Yaml::BeginMap;
			out << Yaml::Key << "Material" << Yaml::Value;
			out << Yaml::BeginMap;

			out << Yaml::Key << "Transparent" << Yaml::Value << transparent;
			out << Yaml::Key << "AlbedoColor" << Yaml::Value;
			WriteVec3(out, albedoColor);
			out << Yaml::Key << "Emission" << Yaml::Value << emission;

			// The transparent shader uses roughness and the normal map too; only metalness is opaque-only.
			out << Yaml::Key << "UseNormalMap" << Yaml::Value << useNormalMap;
			if (!transparent)
				out << Yaml::Key << "Metalness" << Yaml::Value << metalness;
			out << Yaml::Key << "Roughness" << Yaml::Value << roughness;
			if (transparent)
				out << Yaml::Key << "Transparency" << Yaml::Value << transparency;

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
				out << Yaml::Key << "EmissiveColor" << Yaml::Value;
				WriteVec3(out, surface.EmissiveColor);
			}
			if (surface.EmissiveMap)
				WriteTextureReference(out, "EmissiveMap", surface.EmissiveMap, textureReferenceSerialization);
			if (surface.OcclusionMap)
				WriteTextureReference(out, "OcclusionMap", surface.OcclusionMap, textureReferenceSerialization);
			if (surface.OcclusionStrength != defaults.OcclusionStrength)
				out << Yaml::Key << "OcclusionStrength" << Yaml::Value << surface.OcclusionStrength;
			if (surface.OcclusionChannel != defaults.OcclusionChannel)
				out << Yaml::Key << "OcclusionChannel" << Yaml::Value << ChannelToString(surface.OcclusionChannel);
			if (surface.MetalnessChannel != defaults.MetalnessChannel)
				out << Yaml::Key << "MetalnessChannel" << Yaml::Value << ChannelToString(surface.MetalnessChannel);
			if (surface.RoughnessChannel != defaults.RoughnessChannel)
				out << Yaml::Key << "RoughnessChannel" << Yaml::Value << ChannelToString(surface.RoughnessChannel);
			if (surface.Specular != defaults.Specular)
				out << Yaml::Key << "Specular" << Yaml::Value << surface.Specular;
			if (surface.NormalStrength != defaults.NormalStrength)
				out << Yaml::Key << "NormalStrength" << Yaml::Value << surface.NormalStrength;
			if (surface.HeightMap)
				WriteTextureReference(out, "HeightMap", surface.HeightMap, textureReferenceSerialization);
			if (surface.BumpHeight != defaults.BumpHeight)
				out << Yaml::Key << "BumpHeight" << Yaml::Value << surface.BumpHeight;
			if (surface.UVTiling != defaults.UVTiling)
			{
				out << Yaml::Key << "UVTiling" << Yaml::Value;
				WriteVec2(out, surface.UVTiling);
			}
			if (surface.UVOffset != defaults.UVOffset)
			{
				out << Yaml::Key << "UVOffset" << Yaml::Value;
				WriteVec2(out, surface.UVOffset);
			}
			if (surface.UVRotation != defaults.UVRotation)
				out << Yaml::Key << "UVRotation" << Yaml::Value << surface.UVRotation;
			if (surface.AlphaMode != defaults.AlphaMode)
				out << Yaml::Key << "AlphaMode" << Yaml::Value << AlphaModeToString(surface.AlphaMode);
			if (surface.AlphaThreshold != defaults.AlphaThreshold)
				out << Yaml::Key << "AlphaThreshold" << Yaml::Value << surface.AlphaThreshold;
			if (surface.TwoSided != defaults.TwoSided)
				out << Yaml::Key << "TwoSided" << Yaml::Value << surface.TwoSided;
			if (surface.Transmission != defaults.Transmission)
				out << Yaml::Key << "Transmission" << Yaml::Value << surface.Transmission;
			if (surface.IOR != defaults.IOR)
				out << Yaml::Key << "IOR" << Yaml::Value << surface.IOR;
			if (surface.Thickness != defaults.Thickness)
				out << Yaml::Key << "Thickness" << Yaml::Value << surface.Thickness;
			if (surface.AttenuationColor != defaults.AttenuationColor)
			{
				out << Yaml::Key << "AttenuationColor" << Yaml::Value;
				WriteVec3(out, surface.AttenuationColor);
			}
			if (surface.AttenuationDistance != defaults.AttenuationDistance)
				out << Yaml::Key << "AttenuationDistance" << Yaml::Value << surface.AttenuationDistance;
			if (surface.DiffuseTransmission != defaults.DiffuseTransmission)
				out << Yaml::Key << "DiffuseTransmission" << Yaml::Value << surface.DiffuseTransmission;
			if (surface.DiffuseTransmissionColor != defaults.DiffuseTransmissionColor)
			{
				out << Yaml::Key << "DiffuseTransmissionColor" << Yaml::Value;
				WriteVec3(out, surface.DiffuseTransmissionColor);
			}
			if (surface.DiffuseTransmissionMap)
				WriteTextureReference(out, "DiffuseTransmissionMap", surface.DiffuseTransmissionMap, textureReferenceSerialization);
			if (surface.Clearcoat != defaults.Clearcoat)
				out << Yaml::Key << "Clearcoat" << Yaml::Value << surface.Clearcoat;
			if (surface.ClearcoatRoughness != defaults.ClearcoatRoughness)
				out << Yaml::Key << "ClearcoatRoughness" << Yaml::Value << surface.ClearcoatRoughness;
			if (surface.SheenColor != defaults.SheenColor)
			{
				out << Yaml::Key << "SheenColor" << Yaml::Value;
				WriteVec3(out, surface.SheenColor);
			}
			if (surface.SheenRoughness != defaults.SheenRoughness)
				out << Yaml::Key << "SheenRoughness" << Yaml::Value << surface.SheenRoughness;
			if (surface.SubsurfaceColor != defaults.SubsurfaceColor)
			{
				out << Yaml::Key << "SubsurfaceColor" << Yaml::Value;
				WriteVec3(out, surface.SubsurfaceColor);
			}
			if (surface.SubsurfaceThickness != defaults.SubsurfaceThickness)
				out << Yaml::Key << "SubsurfaceThickness" << Yaml::Value << surface.SubsurfaceThickness;
			if (surface.Anisotropy != defaults.Anisotropy)
				out << Yaml::Key << "Anisotropy" << Yaml::Value << surface.Anisotropy;
			if (surface.AnisotropyRotation != defaults.AnisotropyRotation)
				out << Yaml::Key << "AnisotropyRotation" << Yaml::Value << surface.AnisotropyRotation;
			if (surface.Mapping != defaults.Mapping)
				out << Yaml::Key << "Mapping" << Yaml::Value << MappingToString(surface.Mapping);
			if (surface.TriplanarSharpness != defaults.TriplanarSharpness)
				out << Yaml::Key << "TriplanarSharpness" << Yaml::Value << surface.TriplanarSharpness;
			if (surface.DetailAlbedoMap)
				WriteTextureReference(out, "DetailAlbedoMap", surface.DetailAlbedoMap, textureReferenceSerialization);
			if (surface.DetailNormalMap)
				WriteTextureReference(out, "DetailNormalMap", surface.DetailNormalMap, textureReferenceSerialization);
			if (surface.DetailMaskMap)
				WriteTextureReference(out, "DetailMaskMap", surface.DetailMaskMap, textureReferenceSerialization);
			if (surface.DetailTiling != defaults.DetailTiling)
			{
				out << Yaml::Key << "DetailTiling" << Yaml::Value;
				WriteVec2(out, surface.DetailTiling);
			}
			if (surface.DetailNormalStrength != defaults.DetailNormalStrength)
				out << Yaml::Key << "DetailNormalStrength" << Yaml::Value << surface.DetailNormalStrength;
			const int32_t acousticTag = materialAsset->GetAcousticTag();
			if (acousticTag >= 0 && IsValidAcousticMaterial(static_cast<AcousticMaterial>(acousticTag)))
				out << Yaml::Key << "AcousticMaterial" << Yaml::Value << AcousticMaterialName(static_cast<AcousticMaterial>(acousticTag));

			out << Yaml::Key << "MaterialFlags" << Yaml::Value << materialFlags;

			// A graph: written by the material that owns it; an asset pack also gives every instance its
			// own copy and the graph's index in the exported shaders.
			const bool packed = textureReferenceSerialization == TextureReferenceSerialization::AssetHandles;
			if (Ref<MaterialGraph> graph = materialAsset->GetGraph(); graph && (packed || !materialAsset->GetParent()))
			{
				out << Yaml::Key << "Graph" << Yaml::Value;
				graph->Serialize(out);
				if (packed)
				{
					out << Yaml::Key << "GraphIndex" << Yaml::Value << graph->DispatchIndex;
					out << Yaml::Key << "GraphLayout" << Yaml::Value << Yaml::BeginSeq;
					for (const MaterialGraphParameter& parameter : graph->GetParameters())
					{
						out << Yaml::BeginMap;
						out << Yaml::Key << "Name" << Yaml::Value << parameter.Name;
						out << Yaml::Key << "Kind" << Yaml::Value << static_cast<uint32_t>(parameter.Kind);
						out << Yaml::Key << "Slot" << Yaml::Value << parameter.Slot;
						out << Yaml::Key << "Default" << Yaml::Value << parameter.Default;
						out << Yaml::Key << "DefaultTexture" << Yaml::Value << static_cast<uint64_t>(parameter.DefaultTexture);
						out << Yaml::Key << "SRGB" << Yaml::Value << parameter.SRGB;
						out << Yaml::Key << "Exposed" << Yaml::Value << parameter.Exposed;
						out << Yaml::EndMap;
					}
					out << Yaml::EndSeq;
				}
			}
			if (!materialAsset->GetGraphParameters().empty())
			{
				out << Yaml::Key << "GraphParameters" << Yaml::Value << Yaml::BeginMap;
				for (const auto& [name, value] : materialAsset->GetGraphParameters())
				{
					out << Yaml::Key << name << Yaml::Value << Yaml::BeginMap;
					out << Yaml::Key << "Value" << Yaml::Value << value.Value;
					if (value.Texture)
						WriteTextureReference(out, "Texture", value.Texture, textureReferenceSerialization);
					out << Yaml::EndMap;
				}
				out << Yaml::EndMap;
			}

			// A material instance: its parent and the values it sets itself. The values above are
			// all written anyway, so the file loads as it was saved without the parent.
			if (const AssetHandle parent = materialAsset->GetParent())
			{
				out << Yaml::Key << "Parent" << Yaml::Value << static_cast<uint64_t>(parent);
				out << Yaml::Key << "Overrides" << Yaml::Value << Yaml::BeginSeq;
				for (const std::string& key : materialAsset->GetOverrides())
					out << key;
				out << Yaml::EndSeq;
			}

			out << Yaml::EndMap;
			out << Yaml::EndMap;
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

			Yaml::Node root = Yaml::Load(yamlString);
			Yaml::Node materialNode = root["Material"];

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
			AssetManager::RegisterDependency(ResolveTextureReference(materialNode["DiffuseTransmissionMap"]), handle);
			AssetManager::RegisterDependency(ResolveTextureReference(materialNode["DetailAlbedoMap"]), handle);
			AssetManager::RegisterDependency(ResolveTextureReference(materialNode["DetailNormalMap"]), handle);
			AssetManager::RegisterDependency(ResolveTextureReference(materialNode["DetailMaskMap"]), handle);
		}

		static bool DeserializeMaterialFromYAML(const std::string& yamlString, Ref<MaterialAsset>& targetMaterialAsset, AssetHandle handle)
		{
			if (yamlString.empty())
				return false;

			RegisterMaterialDependenciesFromYAML(yamlString, handle);

			Yaml::Node root = Yaml::Load(yamlString);
			Yaml::Node materialNode = root["Material"];
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

			const auto tryAssignTexture = [&targetMaterialAsset](const Yaml::Node& textureNode, auto&& assignFn)
			{
				if (AssetHandle textureHandle = ResolveTextureReference(textureNode))
					assignFn(textureHandle);
			};

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
			surface.AlphaMode = ReadAlphaMode(materialNode["AlphaMode"], surface.AlphaMode);
			surface.AlphaThreshold = materialNode["AlphaThreshold"].as<float>(surface.AlphaThreshold);
			surface.TwoSided = materialNode["TwoSided"].as<bool>(surface.TwoSided);
			surface.Transmission = materialNode["Transmission"].as<float>(surface.Transmission);
			surface.IOR = materialNode["IOR"].as<float>(surface.IOR);
			surface.Thickness = materialNode["Thickness"].as<float>(surface.Thickness);
			surface.AttenuationColor = ReadVec3(materialNode["AttenuationColor"], surface.AttenuationColor);
			surface.AttenuationDistance = materialNode["AttenuationDistance"].as<float>(surface.AttenuationDistance);
			surface.DiffuseTransmission = materialNode["DiffuseTransmission"].as<float>(surface.DiffuseTransmission);
			surface.DiffuseTransmissionColor = ReadVec3(materialNode["DiffuseTransmissionColor"], surface.DiffuseTransmissionColor);
			surface.DiffuseTransmissionMap = ResolveTextureReference(materialNode["DiffuseTransmissionMap"]);
			surface.Clearcoat = materialNode["Clearcoat"].as<float>(surface.Clearcoat);
			surface.ClearcoatRoughness = materialNode["ClearcoatRoughness"].as<float>(surface.ClearcoatRoughness);
			surface.SheenColor = ReadVec3(materialNode["SheenColor"], surface.SheenColor);
			surface.SheenRoughness = materialNode["SheenRoughness"].as<float>(surface.SheenRoughness);
			surface.SubsurfaceColor = ReadVec3(materialNode["SubsurfaceColor"], surface.SubsurfaceColor);
			surface.SubsurfaceThickness = materialNode["SubsurfaceThickness"].as<float>(surface.SubsurfaceThickness);
			surface.Anisotropy = materialNode["Anisotropy"].as<float>(surface.Anisotropy);
			surface.AnisotropyRotation = materialNode["AnisotropyRotation"].as<float>(surface.AnisotropyRotation);
			surface.Mapping = ReadMapping(materialNode["Mapping"], surface.Mapping);
			surface.TriplanarSharpness = materialNode["TriplanarSharpness"].as<float>(surface.TriplanarSharpness);
			surface.DetailAlbedoMap = ResolveTextureReference(materialNode["DetailAlbedoMap"]);
			surface.DetailNormalMap = ResolveTextureReference(materialNode["DetailNormalMap"]);
			surface.DetailMaskMap = ResolveTextureReference(materialNode["DetailMaskMap"]);
			surface.DetailTiling = ReadVec2(materialNode["DetailTiling"], surface.DetailTiling);
			surface.DetailNormalStrength = materialNode["DetailNormalStrength"].as<float>(surface.DetailNormalStrength);

			// Before the maps: the albedo's mip chain depends on the alpha mode and cutoff, so it is built
			// once, for the ones the file has.
			targetMaterialAsset->SetSurfaceParameters(surface);

			tryAssignTexture(materialNode["AlbedoMap"], [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetAlbedoMap(handle); });
			tryAssignTexture(materialNode["NormalMap"], [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetNormalMap(handle); });
			tryAssignTexture(materialNode["MetalnessMap"], [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetMetalnessMap(handle); });
			tryAssignTexture(materialNode["RoughnessMap"], [&targetMaterialAsset](AssetHandle handle) { targetMaterialAsset->SetRoughnessMap(handle); });

			// Files written before emissive colour existed tinted emission by the base colour and its
			// map. Carry that over as data so they render exactly as they did.
			if (!materialNode["EmissiveColor"] && targetMaterialAsset->GetEmission() > 0.0f)
			{
				surface.EmissiveColor = targetMaterialAsset->GetAlbedoColor();
				surface.EmissiveMap = targetMaterialAsset->GetAlbedoMapHandle();
			}
			targetMaterialAsset->SetSurfaceParameters(surface);

			// Optional: materials saved before acoustic tags, or without one, carry no tag.
			int32_t acousticTag = -1;
			if (const Yaml::Node acousticNode = materialNode["AcousticMaterial"])
			{
				AcousticMaterial acoustic = AcousticMaterial::Default;
				if (ParseAcousticMaterial(acousticNode.as<std::string>(""), acoustic))
					acousticTag = static_cast<int32_t>(acoustic);
				else
					LUX_CORE_WARN_TAG("AssetManager", "Material {} has unknown acoustic material '{}'; ignoring it", static_cast<uint64_t>(handle), acousticNode.as<std::string>(""));
			}
			targetMaterialAsset->SetAcousticTag(acousticTag);

			if (materialNode["MaterialFlags"])
				targetMaterialAsset->GetMaterial()->SetFlags(materialNode["MaterialFlags"].as<uint32_t>());

			if (const Yaml::Node graphNode = materialNode["Graph"])
			{
				if (Ref<MaterialGraph> graph = MaterialGraph::Deserialize(graphNode))
				{
					graph->DispatchIndex = materialNode["GraphIndex"].as<uint32_t>(0);
					if (const Yaml::Node layout = materialNode["GraphLayout"])
					{
						graph->HasPackedParameters = true;
						for (const Yaml::Node item : layout)
						{
							MaterialGraphParameter parameter;
							parameter.Name = item["Name"].as<std::string>("");
							parameter.Kind = static_cast<MaterialGraphParameterKind>(item["Kind"].as<uint32_t>(0));
							parameter.Slot = item["Slot"].as<uint32_t>(0);
							parameter.Default = item["Default"].as<glm::vec4>(glm::vec4(0.0f));
							parameter.DefaultTexture = item["DefaultTexture"].as<uint64_t>(0);
							parameter.SRGB = item["SRGB"].as<bool>(true);
							parameter.Exposed = item["Exposed"].as<bool>(true);
							graph->PackedParameters.push_back(parameter);
						}
					}
					targetMaterialAsset->SetGraph(graph);
				}
			}
			std::map<std::string, MaterialGraphParameterValue, std::less<>> graphParameters;
			for (const auto& entry : materialNode["GraphParameters"])
			{
				MaterialGraphParameterValue value;
				value.Value = entry.second["Value"].as<glm::vec4>(glm::vec4(0.0f));
				value.Texture = ResolveTextureReference(entry.second["Texture"]);
				graphParameters[entry.first.as<std::string>("")] = value;
			}
			targetMaterialAsset->SetGraphParameters(std::move(graphParameters));

			if (const AssetHandle parent = materialNode["Parent"].as<uint64_t>(0))
			{
				std::set<std::string, std::less<>> overrides;
				for (const Yaml::Node key : materialNode["Overrides"])
					overrides.emplace(key.as<std::string>(""));
				targetMaterialAsset->SetOverrides(std::move(overrides));
				targetMaterialAsset->Handle = handle;
				AssetManager::RegisterDependency(parent, handle);

				// In the editor, take the parent's current values; a pack already holds the resolved
				// ones. Loading the parent loads its own parent, so a loop in the files is cut off.
				static thread_local int s_ParentDepth = 0;
				if (!Project::GetEditorAssetManager())
				{
					targetMaterialAsset->SetParent(0);
				}
				else if (s_ParentDepth >= 32 || !targetMaterialAsset->SetParent(parent))
					LUX_CORE_WARN_TAG("AssetManager", "Material {}: parent {} is not a material or makes a loop; keeping its own values", static_cast<uint64_t>(handle), static_cast<uint64_t>(parent));
				else
				{
					s_ParentDepth++;
					Ref<MaterialAsset> parentAsset = AssetManager::GetAsset<MaterialAsset>(parent);
					s_ParentDepth--;
					if (parentAsset)
						targetMaterialAsset->InheritFrom(*parentAsset);
					else
						LUX_CORE_WARN_TAG("AssetManager", "Material {}: parent {} is missing; keeping its own values", static_cast<uint64_t>(handle), static_cast<uint64_t>(parent));
				}
			}

			// The editor compiles the graphs a project's materials own into the surface shaders.
			if (targetMaterialAsset->GetGraph() && !targetMaterialAsset->GetParent() && Project::GetEditorAssetManager())
				MaterialGraphRegistry::Register(handle);

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

		// The YAML writer ends a document with a newline; files from the older emitter (every committed
		// .lmat) do not. That alone is no change, and rewriting for it would dirty each imported material
		// whenever its mesh source loads.
		if (Utils::String::TrimWhitespace(ReadMaterialYAML(metadata)) == Utils::String::TrimWhitespace(serializedMaterial))
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
