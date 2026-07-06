#include "lpch.h"
#include "ProjectSerializer.h"

#include "ProjectRuntimeFormat.h"

#include "Lux/Core/Log.h"
#include "Lux/Serialization/FileStream.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <yaml-cpp/yaml.h>

namespace YAML
{
	template<>
	struct convert<glm::vec3>
	{
		static Node encode(const glm::vec3& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.push_back(rhs.z);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec3& rhs)
		{
			if (!node.IsSequence() || node.size() != 3)
				return false;

			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			rhs.z = node[2].as<float>();
			return true;
		}
	};
}

namespace Lux
{
	namespace
	{
		YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& value)
		{
			out << YAML::Flow;
			out << YAML::BeginSeq << value.x << value.y << value.z << YAML::EndSeq;
			return out;
		}

		std::filesystem::path NormalizeRegistryPath(const std::filesystem::path& projectDirectory, const std::filesystem::path& assetDirectory, const std::filesystem::path& rawPath)
		{
			if (rawPath.empty())
				return (assetDirectory / "AssetRegistry.lzr").lexically_normal();

			if (rawPath.is_absolute())
				return std::filesystem::relative(rawPath, projectDirectory).lexically_normal();

			const auto normalizedAssetDirectory = assetDirectory.lexically_normal();
			const auto normalizedRawPath = rawPath.lexically_normal();

			std::string rawString = normalizedRawPath.generic_string();
			std::string assetDirString = normalizedAssetDirectory.generic_string();
			if (!assetDirString.empty() && rawString.rfind(assetDirString, 0) == 0)
				return normalizedRawPath;

			return (normalizedAssetDirectory / normalizedRawPath).lexically_normal();
		}

		bool IsNumericString(const std::string& value)
		{
			if (value.empty())
				return false;

			for (char ch : value)
			{
				if (!std::isdigit((unsigned char)ch))
					return false;
			}

			return true;
		}

		void CreateDirectoriesIfNeeded(const std::filesystem::path& path)
		{
			const auto directory = path.parent_path();
			if (!directory.empty() && !std::filesystem::exists(directory))
				std::filesystem::create_directories(directory);
		}

		const char* PhysicsCaptureMethodToString(PhysicsCaptureMethod method)
		{
			switch (method)
			{
				case PhysicsCaptureMethod::LiveDebug: return "LiveDebug";
				case PhysicsCaptureMethod::CaptureToFile: return "CaptureToFile";
			}

			return "LiveDebug";
		}

		PhysicsCaptureMethod PhysicsCaptureMethodFromString(std::string_view value)
		{
			if (value == "LiveDebug")
				return PhysicsCaptureMethod::LiveDebug;
			if (value == "CaptureToFile")
				return PhysicsCaptureMethod::CaptureToFile;
			return PhysicsCaptureMethod::LiveDebug;
		}

		const char* RenderScaleModeToString(uint32_t mode)
		{
			switch (mode)
			{
				case 1: return "Scale75";
				case 2: return "Scale50";
				case 3: return "Dynamic";
				case 0:
				default:
					return "Scale100";
			}
		}

		uint32_t RenderScaleModeFromString(std::string_view value)
		{
			if (value == "75%" || value == "Scale75" || value == "0.75" || value == "1")
				return 1;
			if (value == "50%" || value == "Scale50" || value == "0.50" || value == "0.5" || value == "2")
				return 2;
			if (value == "Dynamic" || value == "3")
				return 3;
			return 0;
		}

		const char* QualityPresetToString(uint32_t preset)
		{
			switch (preset)
			{
				case 0: return "Low";
				case 1: return "Medium";
				case 2: return "High";
				case 3: return "Ultra";
				case 4: return "Cinematic";
				default:
					return "Medium";
			}
		}

		uint32_t QualityPresetFromString(std::string_view value)
		{
			if (value == "Low" || value == "0")
				return 0;
			if (value == "High" || value == "2")
				return 2;
			if (value == "Ultra" || value == "3")
				return 3;
			if (value == "Cinematic" || value == "4")
				return 4;
			return 1;
		}

		const char* ShadowResolutionToString(uint32_t resolution)
		{
			switch (resolution)
			{
				case 0: return "1K";
				case 1: return "2K";
				case 2: return "4K";
				case 3: return "8K";
				default:
					return "4K";
			}
		}

		uint32_t ShadowResolutionFromString(std::string_view value)
		{
			if (value == "1K" || value == "1024" || value == "0")
				return 0;
			if (value == "2K" || value == "2048" || value == "1")
				return 1;
			if (value == "8K" || value == "8192" || value == "3")
				return 3;
			return 2;
		}

		const char* SSRQualityToString(uint32_t quality)
		{
			switch (quality)
			{
				case 0: return "Full";
				case 2: return "QuarterDebug";
				case 1:
				default:
					return "HalfBilateral";
			}
		}

		uint32_t SSRQualityFromString(std::string_view value)
		{
			if (value == "Full" || value == "100%" || value == "0")
				return 0;
			if (value == "QuarterDebug" || value == "Quarter Debug Only" || value == "25%" || value == "2")
				return 2;
			return 1;
		}

		uint32_t SSRResolutionScaleFromQuality(uint32_t quality)
		{
			switch (quality)
			{
				case 0: return 1;
				case 2: return 4;
				case 1:
				default:
					return 2;
			}
		}

		uint32_t SSRQualityFromLegacyScale(uint32_t resolutionScale, bool halfRes)
		{
			if (!halfRes && resolutionScale == 2)
				return 0;

			switch (resolutionScale)
			{
				case 1: return 0;
				case 4: return 2;
				case 2:
				default:
					return 1;
			}
		}

		void SerializeSceneRendererSettings(YAML::Emitter& out, const ProjectSceneRendererSettings& settings)
		{
			out << YAML::Key << "SceneRenderer" << YAML::Value;
			out << YAML::BeginMap;

			out << YAML::Key << "Rendering" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "QualityPreset" << YAML::Value << QualityPresetToString(settings.QualityPreset);
			out << YAML::Key << "FrustumCulling" << YAML::Value << settings.EnableFrustumCulling;
			out << YAML::Key << "OcclusionCulling" << YAML::Value << settings.EnableOcclusionCulling;
			out << YAML::Key << "GPUDrivenRendering" << YAML::Value << settings.EnableGPUDrivenRendering;
			out << YAML::Key << "MeshLODs" << YAML::Value << settings.EnableMeshLODs;
			out << YAML::Key << "MeshLODDistanceScale" << YAML::Value << settings.MeshLODDistanceScale;
			out << YAML::Key << "VariableRateShading" << YAML::Value << settings.EnableVariableRateShading;
			out << YAML::Key << "MeshShaders" << YAML::Value << settings.EnableMeshShaders;
			out << YAML::Key << "GTAO" << YAML::Value << settings.EnableGTAO;
			out << YAML::Key << "GTAOBentNormals" << YAML::Value << settings.GTAOBentNormals;
			out << YAML::Key << "GTAODenoisePasses" << YAML::Value << settings.GTAODenoisePasses;
			out << YAML::Key << "AOShadowTolerance" << YAML::Value << settings.AOShadowTolerance;
			out << YAML::Key << "SSR" << YAML::Value << settings.EnableSSR;
			out << YAML::Key << "JumpFloodOutline" << YAML::Value << settings.EnableJumpFlood;
			out << YAML::Key << "RenderScaleMode" << YAML::Value << RenderScaleModeToString(settings.RenderScaleMode);
			out << YAML::Key << "DynamicResolutionMinScale" << YAML::Value << settings.DynamicResolutionMinScale;
			out << YAML::Key << "DynamicResolutionMaxScale" << YAML::Value << settings.DynamicResolutionMaxScale;
			out << YAML::Key << "DynamicResolutionTargetGPUTime" << YAML::Value << settings.DynamicResolutionTargetGPUTime;
			out << YAML::Key << "TextureMipBias" << YAML::Value << settings.TextureMipBias;
			out << YAML::Key << "DistanceMipBias" << YAML::Value << settings.EnableDistanceMipBias;
			out << YAML::Key << "DistanceMipBiasStart" << YAML::Value << settings.DistanceMipBiasStart;
			out << YAML::Key << "DistanceMipBiasEnd" << YAML::Value << settings.DistanceMipBiasEnd;
			out << YAML::Key << "DistanceMipBiasMax" << YAML::Value << settings.DistanceMipBiasMax;
			out << YAML::Key << "OcclusionDepthBias" << YAML::Value << settings.OcclusionDepthBias;
			out << YAML::Key << "OcclusionBoundsScale" << YAML::Value << settings.OcclusionBoundsScale;
			out << YAML::Key << "GTAOResolutionScale" << YAML::Value << settings.GTAOResolutionScale;
			out << YAML::Key << "GTAOTemporalAccumulation" << YAML::Value << settings.GTAOTemporalAccumulation;
			out << YAML::Key << "GTAOTemporalBlend" << YAML::Value << settings.GTAOTemporalBlend;
			out << YAML::Key << "SSRQuality" << YAML::Value << SSRQualityToString(settings.SSRQuality);
			out << YAML::Key << "SSRResolutionScale" << YAML::Value << settings.SSRResolutionScale;
			out << YAML::Key << "SSRTemporalAccumulation" << YAML::Value << settings.SSRTemporalAccumulation;
			out << YAML::Key << "SSRTemporalBlend" << YAML::Value << settings.SSRTemporalBlend;
			out << YAML::EndMap;

			out << YAML::Key << "Shadows" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "SoftShadows" << YAML::Value << settings.SoftShadows;
			out << YAML::Key << "ShadowCulling" << YAML::Value << settings.EnableShadowCulling;
			out << YAML::Key << "MaxDistance" << YAML::Value << settings.MaxShadowDistance;
			out << YAML::Key << "DistanceFade" << YAML::Value << settings.ShadowFade;
			out << YAML::Key << "ActiveCascadeCount" << YAML::Value << settings.ActiveShadowCascadeCount;
			out << YAML::Key << "SplitLambda" << YAML::Value << settings.ShadowCascadeSplitLambda;
			out << YAML::Key << "NearOffset" << YAML::Value << settings.ShadowCascadeNearPlaneOffset;
			out << YAML::Key << "FarOffset" << YAML::Value << settings.ShadowCascadeFarPlaneOffset;
			out << YAML::Key << "CascadeFade" << YAML::Value << settings.ShadowCascadeTransitionFade;
			out << YAML::Key << "FilterMode" << YAML::Value << settings.ShadowFilterMode;
			out << YAML::Key << "DirectionalPCSSCascades" << YAML::Value << settings.DirectionalPCSSCascadeCount;
			out << YAML::Key << "PCFRadiusTexels" << YAML::Value << settings.ShadowPCFRadiusTexels;
			out << YAML::Key << "SpotPCFRadiusTexels" << YAML::Value << settings.SpotShadowPCFRadiusTexels;
			out << YAML::Key << "ResolutionLimit" << YAML::Value << ShadowResolutionToString(settings.ShadowResolution);
			out << YAML::EndMap;

			out << YAML::Key << "PostFX" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "Bloom" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "Enabled" << YAML::Value << settings.BloomEnabled;
			out << YAML::Key << "ResolutionScale" << YAML::Value << settings.BloomResolutionScale;
			out << YAML::Key << "Threshold" << YAML::Value << settings.BloomThreshold;
			out << YAML::Key << "Knee" << YAML::Value << settings.BloomKnee;
			out << YAML::Key << "UpsampleScale" << YAML::Value << settings.BloomUpsampleScale;
			out << YAML::Key << "Intensity" << YAML::Value << settings.BloomIntensity;
			out << YAML::Key << "DirtIntensity" << YAML::Value << settings.BloomDirtIntensity;
			out << YAML::EndMap;

			out << YAML::Key << "DOF" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "Enabled" << YAML::Value << settings.DOFEnabled;
			out << YAML::Key << "ResolutionScale" << YAML::Value << settings.DOFResolutionScale;
			out << YAML::Key << "FocusDistance" << YAML::Value << settings.DOFFocusDistance;
			out << YAML::Key << "BlurSize" << YAML::Value << settings.DOFBlurSize;
			out << YAML::EndMap;

			out << YAML::Key << "SSR" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "HalfRes" << YAML::Value << settings.SSRHalfRes;
			out << YAML::Key << "MaxSteps" << YAML::Value << settings.SSRMaxSteps;
			out << YAML::Key << "Brightness" << YAML::Value << settings.SSRBrightness;
			out << YAML::Key << "DepthTolerance" << YAML::Value << settings.SSRDepthTolerance;
			out << YAML::EndMap;
			out << YAML::EndMap;

			out << YAML::EndMap;
		}

		void DeserializeSceneRendererSettings(const YAML::Node& node, ProjectSceneRendererSettings& settings)
		{
			if (!node)
				return;

			bool hasSSRQuality = false;
			if (auto rendering = node["Rendering"])
			{
				settings.QualityPreset = QualityPresetFromString(rendering["QualityPreset"].as<std::string>(QualityPresetToString(settings.QualityPreset)));
				settings.EnableFrustumCulling = rendering["FrustumCulling"].as<bool>(settings.EnableFrustumCulling);
				settings.EnableOcclusionCulling = rendering["OcclusionCulling"].as<bool>(settings.EnableOcclusionCulling);
				settings.EnableGPUDrivenRendering = rendering["GPUDrivenRendering"].as<bool>(settings.EnableGPUDrivenRendering);
				settings.EnableMeshLODs = rendering["MeshLODs"].as<bool>(settings.EnableMeshLODs);
				settings.MeshLODDistanceScale = rendering["MeshLODDistanceScale"].as<float>(settings.MeshLODDistanceScale);
				settings.EnableVariableRateShading = rendering["VariableRateShading"].as<bool>(settings.EnableVariableRateShading);
				settings.EnableMeshShaders = rendering["MeshShaders"].as<bool>(settings.EnableMeshShaders);
				settings.EnableGTAO = rendering["GTAO"].as<bool>(settings.EnableGTAO);
				settings.GTAOBentNormals = rendering["GTAOBentNormals"].as<bool>(settings.GTAOBentNormals);
				settings.GTAODenoisePasses = rendering["GTAODenoisePasses"].as<int>(settings.GTAODenoisePasses);
				settings.AOShadowTolerance = rendering["AOShadowTolerance"].as<float>(settings.AOShadowTolerance);
				settings.EnableSSR = rendering["SSR"].as<bool>(settings.EnableSSR);
				settings.EnableJumpFlood = rendering["JumpFloodOutline"].as<bool>(settings.EnableJumpFlood);
				settings.RenderScaleMode = RenderScaleModeFromString(rendering["RenderScaleMode"].as<std::string>(RenderScaleModeToString(settings.RenderScaleMode)));
				settings.DynamicResolutionMinScale = rendering["DynamicResolutionMinScale"].as<float>(settings.DynamicResolutionMinScale);
				settings.DynamicResolutionMaxScale = rendering["DynamicResolutionMaxScale"].as<float>(settings.DynamicResolutionMaxScale);
				settings.DynamicResolutionTargetGPUTime = rendering["DynamicResolutionTargetGPUTime"].as<float>(settings.DynamicResolutionTargetGPUTime);
				settings.TextureMipBias = rendering["TextureMipBias"].as<float>(settings.TextureMipBias);
				settings.EnableDistanceMipBias = rendering["DistanceMipBias"].as<bool>(settings.EnableDistanceMipBias);
				settings.DistanceMipBiasStart = rendering["DistanceMipBiasStart"].as<float>(settings.DistanceMipBiasStart);
				settings.DistanceMipBiasEnd = rendering["DistanceMipBiasEnd"].as<float>(settings.DistanceMipBiasEnd);
				settings.DistanceMipBiasMax = rendering["DistanceMipBiasMax"].as<float>(settings.DistanceMipBiasMax);
				settings.OcclusionDepthBias = rendering["OcclusionDepthBias"].as<float>(settings.OcclusionDepthBias);
				settings.OcclusionBoundsScale = rendering["OcclusionBoundsScale"].as<float>(settings.OcclusionBoundsScale);
				settings.GTAOResolutionScale = rendering["GTAOResolutionScale"].as<uint32_t>(settings.GTAOResolutionScale);
				settings.GTAOTemporalAccumulation = rendering["GTAOTemporalAccumulation"].as<bool>(settings.GTAOTemporalAccumulation);
				settings.GTAOTemporalBlend = rendering["GTAOTemporalBlend"].as<float>(settings.GTAOTemporalBlend);
				hasSSRQuality = !!rendering["SSRQuality"];
				settings.SSRQuality = SSRQualityFromString(rendering["SSRQuality"].as<std::string>(SSRQualityToString(settings.SSRQuality)));
				settings.SSRResolutionScale = rendering["SSRResolutionScale"].as<uint32_t>(settings.SSRResolutionScale);
				settings.SSRTemporalAccumulation = rendering["SSRTemporalAccumulation"].as<bool>(settings.SSRTemporalAccumulation);
				settings.SSRTemporalBlend = rendering["SSRTemporalBlend"].as<float>(settings.SSRTemporalBlend);
			}

			if (auto shadows = node["Shadows"])
			{
				settings.SoftShadows = shadows["SoftShadows"].as<bool>(settings.SoftShadows);
				settings.EnableShadowCulling = shadows["ShadowCulling"].as<bool>(settings.EnableShadowCulling);
				settings.MaxShadowDistance = shadows["MaxDistance"].as<float>(settings.MaxShadowDistance);
				settings.ShadowFade = shadows["DistanceFade"].as<float>(settings.ShadowFade);
				settings.ActiveShadowCascadeCount = shadows["ActiveCascadeCount"].as<uint32_t>(settings.ActiveShadowCascadeCount);
				settings.ShadowCascadeSplitLambda = shadows["SplitLambda"].as<float>(settings.ShadowCascadeSplitLambda);
				settings.ShadowCascadeNearPlaneOffset = shadows["NearOffset"].as<float>(settings.ShadowCascadeNearPlaneOffset);
				settings.ShadowCascadeFarPlaneOffset = shadows["FarOffset"].as<float>(settings.ShadowCascadeFarPlaneOffset);
				settings.ShadowCascadeTransitionFade = shadows["CascadeFade"].as<float>(settings.ShadowCascadeTransitionFade);
				settings.ShadowFilterMode = shadows["FilterMode"].as<uint32_t>(settings.ShadowFilterMode);
				settings.DirectionalPCSSCascadeCount = shadows["DirectionalPCSSCascades"].as<uint32_t>(settings.DirectionalPCSSCascadeCount);
				settings.ShadowPCFRadiusTexels = shadows["PCFRadiusTexels"].as<float>(settings.ShadowPCFRadiusTexels);
				settings.SpotShadowPCFRadiusTexels = shadows["SpotPCFRadiusTexels"].as<float>(settings.SpotShadowPCFRadiusTexels);
				YAML::Node shadowResolution = shadows["ResolutionLimit"] ? shadows["ResolutionLimit"] : shadows["ShadowResolution"];
				settings.ShadowResolution = ShadowResolutionFromString(shadowResolution.as<std::string>(ShadowResolutionToString(settings.ShadowResolution)));
			}

			if (auto postFX = node["PostFX"])
			{
				if (auto bloom = postFX["Bloom"])
				{
					settings.BloomEnabled = bloom["Enabled"].as<bool>(settings.BloomEnabled);
					settings.BloomResolutionScale = bloom["ResolutionScale"].as<uint32_t>(settings.BloomResolutionScale);
					settings.BloomThreshold = bloom["Threshold"].as<float>(settings.BloomThreshold);
					settings.BloomKnee = bloom["Knee"].as<float>(settings.BloomKnee);
					settings.BloomUpsampleScale = bloom["UpsampleScale"].as<float>(settings.BloomUpsampleScale);
					settings.BloomIntensity = bloom["Intensity"].as<float>(settings.BloomIntensity);
					settings.BloomDirtIntensity = bloom["DirtIntensity"].as<float>(settings.BloomDirtIntensity);
				}

				if (auto dof = postFX["DOF"])
				{
					settings.DOFEnabled = dof["Enabled"].as<bool>(settings.DOFEnabled);
					settings.DOFResolutionScale = dof["ResolutionScale"].as<uint32_t>(settings.DOFResolutionScale);
					settings.DOFFocusDistance = dof["FocusDistance"].as<float>(settings.DOFFocusDistance);
					settings.DOFBlurSize = dof["BlurSize"].as<float>(settings.DOFBlurSize);
				}

				if (auto ssr = postFX["SSR"])
				{
					settings.SSRHalfRes = ssr["HalfRes"].as<bool>(settings.SSRHalfRes);
					settings.SSRMaxSteps = ssr["MaxSteps"].as<int>(settings.SSRMaxSteps);
					settings.SSRBrightness = ssr["Brightness"].as<float>(settings.SSRBrightness);
					settings.SSRDepthTolerance = ssr["DepthTolerance"].as<float>(settings.SSRDepthTolerance);
				}
			}

			if (hasSSRQuality)
				settings.SSRResolutionScale = SSRResolutionScaleFromQuality(settings.SSRQuality);
			else
				settings.SSRQuality = SSRQualityFromLegacyScale(settings.SSRResolutionScale, settings.SSRHalfRes);
		}

		void WriteSceneRendererRuntimeSettings(FileStreamWriter& serializer, const ProjectSceneRendererSettings& settings)
		{
			serializer.WriteRaw(settings.EnableFrustumCulling);
			serializer.WriteRaw(settings.EnableOcclusionCulling);
			serializer.WriteRaw(settings.EnableGPUDrivenRendering);
			serializer.WriteRaw(settings.EnableMeshLODs);
			serializer.WriteRaw(settings.MeshLODDistanceScale);
			serializer.WriteRaw(settings.EnableVariableRateShading);
			serializer.WriteRaw(settings.EnableMeshShaders);
			serializer.WriteRaw(settings.EnableGTAO);
			serializer.WriteRaw(settings.GTAOBentNormals);
			serializer.WriteRaw(settings.GTAODenoisePasses);
			serializer.WriteRaw(settings.AOShadowTolerance);
			serializer.WriteRaw(settings.EnableSSR);
			serializer.WriteRaw(settings.EnableJumpFlood);
			serializer.WriteRaw(settings.RenderScaleMode);
			serializer.WriteRaw(settings.DynamicResolutionMinScale);
			serializer.WriteRaw(settings.DynamicResolutionMaxScale);
			serializer.WriteRaw(settings.DynamicResolutionTargetGPUTime);
			serializer.WriteRaw(settings.TextureMipBias);
			serializer.WriteRaw(settings.EnableDistanceMipBias);
			serializer.WriteRaw(settings.DistanceMipBiasStart);
			serializer.WriteRaw(settings.DistanceMipBiasEnd);
			serializer.WriteRaw(settings.DistanceMipBiasMax);
			serializer.WriteRaw(settings.OcclusionDepthBias);
			serializer.WriteRaw(settings.OcclusionBoundsScale);
			serializer.WriteRaw(settings.GTAOResolutionScale);
			serializer.WriteRaw(settings.GTAOTemporalAccumulation);
			serializer.WriteRaw(settings.GTAOTemporalBlend);
			serializer.WriteRaw(settings.SSRResolutionScale);
			serializer.WriteRaw(settings.SSRQuality);
			serializer.WriteRaw(settings.SSRTemporalAccumulation);
			serializer.WriteRaw(settings.SSRTemporalBlend);

			serializer.WriteRaw(settings.SoftShadows);
			serializer.WriteRaw(settings.EnableShadowCulling);
			serializer.WriteRaw(settings.MaxShadowDistance);
			serializer.WriteRaw(settings.ShadowFade);
			serializer.WriteRaw(settings.ShadowCascadeSplitLambda);
			serializer.WriteRaw(settings.ShadowCascadeNearPlaneOffset);
			serializer.WriteRaw(settings.ShadowCascadeFarPlaneOffset);
			serializer.WriteRaw(settings.ShadowCascadeTransitionFade);

			serializer.WriteRaw(settings.BloomEnabled);
			serializer.WriteRaw(settings.BloomResolutionScale);
			serializer.WriteRaw(settings.BloomThreshold);
			serializer.WriteRaw(settings.BloomKnee);
			serializer.WriteRaw(settings.BloomUpsampleScale);
			serializer.WriteRaw(settings.BloomIntensity);
			serializer.WriteRaw(settings.BloomDirtIntensity);

			serializer.WriteRaw(settings.DOFEnabled);
			serializer.WriteRaw(settings.DOFResolutionScale);
			serializer.WriteRaw(settings.DOFFocusDistance);
			serializer.WriteRaw(settings.DOFBlurSize);

			serializer.WriteRaw(settings.SSRHalfRes);
			serializer.WriteRaw(settings.SSRMaxSteps);
			serializer.WriteRaw(settings.SSRBrightness);
			serializer.WriteRaw(settings.SSRDepthTolerance);
			serializer.WriteRaw(settings.QualityPreset);
			serializer.WriteRaw(settings.ShadowResolution);
			serializer.WriteRaw(settings.ActiveShadowCascadeCount);
			serializer.WriteRaw(settings.ShadowFilterMode);
			serializer.WriteRaw(settings.DirectionalPCSSCascadeCount);
			serializer.WriteRaw(settings.ShadowPCFRadiusTexels);
			serializer.WriteRaw(settings.SpotShadowPCFRadiusTexels);
		}

		void ReadSceneRendererRuntimeSettings(FileStreamReader& stream, ProjectSceneRendererSettings& settings, uint32_t version)
		{
			stream.ReadRaw(settings.EnableFrustumCulling);
			if (version >= 3)
				stream.ReadRaw(settings.EnableOcclusionCulling);
			stream.ReadRaw(settings.EnableGPUDrivenRendering);
			if (version >= 10)
			{
				stream.ReadRaw(settings.EnableMeshLODs);
				stream.ReadRaw(settings.MeshLODDistanceScale);
			}
			if (version >= 13)
				stream.ReadRaw(settings.EnableVariableRateShading);
			if (version >= 11)
				stream.ReadRaw(settings.EnableMeshShaders);
			stream.ReadRaw(settings.EnableGTAO);
			stream.ReadRaw(settings.GTAOBentNormals);
			stream.ReadRaw(settings.GTAODenoisePasses);
			stream.ReadRaw(settings.AOShadowTolerance);
			stream.ReadRaw(settings.EnableSSR);
			stream.ReadRaw(settings.EnableJumpFlood);
			if (version >= 4)
			{
				stream.ReadRaw(settings.RenderScaleMode);
				stream.ReadRaw(settings.DynamicResolutionMinScale);
				stream.ReadRaw(settings.DynamicResolutionMaxScale);
				stream.ReadRaw(settings.DynamicResolutionTargetGPUTime);
			}
			if (version >= 5)
			{
				stream.ReadRaw(settings.TextureMipBias);
				stream.ReadRaw(settings.EnableDistanceMipBias);
				stream.ReadRaw(settings.DistanceMipBiasStart);
				stream.ReadRaw(settings.DistanceMipBiasEnd);
				stream.ReadRaw(settings.DistanceMipBiasMax);
			}
			if (version >= 6)
			{
				stream.ReadRaw(settings.OcclusionDepthBias);
				stream.ReadRaw(settings.OcclusionBoundsScale);
				stream.ReadRaw(settings.GTAOResolutionScale);
				stream.ReadRaw(settings.GTAOTemporalAccumulation);
				stream.ReadRaw(settings.GTAOTemporalBlend);
				stream.ReadRaw(settings.SSRResolutionScale);
				if (version >= 7)
					stream.ReadRaw(settings.SSRQuality);
				stream.ReadRaw(settings.SSRTemporalAccumulation);
				stream.ReadRaw(settings.SSRTemporalBlend);
			}

			stream.ReadRaw(settings.SoftShadows);
			if (version >= 3)
				stream.ReadRaw(settings.EnableShadowCulling);
			stream.ReadRaw(settings.MaxShadowDistance);
			stream.ReadRaw(settings.ShadowFade);
			stream.ReadRaw(settings.ShadowCascadeSplitLambda);
			stream.ReadRaw(settings.ShadowCascadeNearPlaneOffset);
			stream.ReadRaw(settings.ShadowCascadeFarPlaneOffset);
			stream.ReadRaw(settings.ShadowCascadeTransitionFade);

			stream.ReadRaw(settings.BloomEnabled);
			if (version >= 6)
				stream.ReadRaw(settings.BloomResolutionScale);
			stream.ReadRaw(settings.BloomThreshold);
			stream.ReadRaw(settings.BloomKnee);
			stream.ReadRaw(settings.BloomUpsampleScale);
			stream.ReadRaw(settings.BloomIntensity);
			stream.ReadRaw(settings.BloomDirtIntensity);

			stream.ReadRaw(settings.DOFEnabled);
			if (version >= 6)
				stream.ReadRaw(settings.DOFResolutionScale);
			stream.ReadRaw(settings.DOFFocusDistance);
			stream.ReadRaw(settings.DOFBlurSize);

			stream.ReadRaw(settings.SSRHalfRes);
			stream.ReadRaw(settings.SSRMaxSteps);
			stream.ReadRaw(settings.SSRBrightness);
			stream.ReadRaw(settings.SSRDepthTolerance);

			if (version >= 8)
			{
				stream.ReadRaw(settings.QualityPreset);
				stream.ReadRaw(settings.ShadowResolution);
			}

			if (version >= 12)
			{
				stream.ReadRaw(settings.ActiveShadowCascadeCount);
				stream.ReadRaw(settings.ShadowFilterMode);
				stream.ReadRaw(settings.DirectionalPCSSCascadeCount);
				stream.ReadRaw(settings.ShadowPCFRadiusTexels);
				stream.ReadRaw(settings.SpotShadowPCFRadiusTexels);
			}

			if (version >= 7)
				settings.SSRResolutionScale = SSRResolutionScaleFromQuality(settings.SSRQuality);
			else
				settings.SSRQuality = SSRQualityFromLegacyScale(settings.SSRResolutionScale, settings.SSRHalfRes);
		}
	}

	ProjectSerializer::ProjectSerializer(Ref<Project> project)
		: m_Project(project)
	{
	}

	bool ProjectSerializer::Serialize(const std::filesystem::path& filepath)
	{
		const auto& config = m_Project->GetConfig();

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Project" << YAML::Value;
		{
			out << YAML::BeginMap;
			out << YAML::Key << "Name" << YAML::Value << config.Name;
			out << YAML::Key << "AssetDirectory" << YAML::Value << config.AssetDirectory.generic_string();
			out << YAML::Key << "AssetRegistry" << YAML::Value << config.AssetRegistryPath.generic_string();
			out << YAML::Key << "AudioCommandsRegistryPath" << YAML::Value << config.AudioCommandsRegistryPath.generic_string();
			out << YAML::Key << "MeshPath" << YAML::Value << config.MeshPath.generic_string();
			out << YAML::Key << "MeshSourcePath" << YAML::Value << config.MeshSourcePath.generic_string();
			out << YAML::Key << "AnimationPath" << YAML::Value << config.AnimationPath.generic_string();
			out << YAML::Key << "ScriptModulePath" << YAML::Value << config.ScriptModulePath.generic_string();
			out << YAML::Key << "DefaultNamespace" << YAML::Value << config.DefaultNamespace;
			out << YAML::Key << "StartScene" << YAML::Value << config.StartScene;
			out << YAML::Key << "AutomaticallyReloadAssembly" << YAML::Value << config.AutomaticallyReloadAssembly;
			out << YAML::Key << "AutoSave" << YAML::Value << config.EnableAutoSave;
			out << YAML::Key << "AutoSaveInterval" << YAML::Value << config.AutoSaveIntervalSeconds;
			out << YAML::Key << "RuntimeExport" << YAML::Value;
			{
				out << YAML::BeginMap;
				out << YAML::Key << "GameName" << YAML::Value << config.RuntimeExport.GameName;
				out << YAML::Key << "WindowWidth" << YAML::Value << config.RuntimeExport.WindowWidth;
				out << YAML::Key << "WindowHeight" << YAML::Value << config.RuntimeExport.WindowHeight;
				out << YAML::Key << "Fullscreen" << YAML::Value << config.RuntimeExport.Fullscreen;
				out << YAML::Key << "VSync" << YAML::Value << config.RuntimeExport.VSync;
				out << YAML::Key << "IconPath" << YAML::Value << config.RuntimeExport.IconPath.generic_string();
				out << YAML::Key << "IconHandle" << YAML::Value << (uint64_t)config.RuntimeExport.IconHandle;
				out << YAML::Key << "TargetConfig" << YAML::Value << RuntimeExportTargetToString(config.RuntimeExport.TargetConfig);
				out << YAML::EndMap;
			}
			SerializeSceneRendererSettings(out, config.SceneRenderer);

			out << YAML::Key << "Audio" << YAML::Value;
			{
				out << YAML::BeginMap;
				out << YAML::Key << "FileStreamingDurationThreshold" << YAML::Value << config.Audio.FileStreamingDurationThreshold;
				out << YAML::EndMap;
			}

			out << YAML::Key << "Physics" << YAML::Value;
			{
				out << YAML::BeginMap;
				out << YAML::Key << "FixedTimestep" << YAML::Value << config.Physics.FixedTimestep;
				out << YAML::Key << "Gravity" << YAML::Value << config.Physics.Gravity;
				out << YAML::Key << "SolverPositionIterations" << YAML::Value << config.Physics.PositionSolverIterations;
				out << YAML::Key << "SolverVelocityIterations" << YAML::Value << config.Physics.VelocitySolverIterations;
				out << YAML::Key << "MaxBodies" << YAML::Value << config.Physics.MaxBodies;
				out << YAML::Key << "CaptureOnPlay" << YAML::Value << config.Physics.CaptureOnPlay;
				out << YAML::Key << "CaptureMethod" << YAML::Value << PhysicsCaptureMethodToString(config.Physics.CaptureMethod);

				if (!config.Physics.Layers.empty())
				{
					out << YAML::Key << "Layers" << YAML::Value << YAML::BeginSeq;
					for (const auto& layer : config.Physics.Layers)
					{
						out << YAML::BeginMap;
						out << YAML::Key << "Name" << YAML::Value << layer.Name;
						out << YAML::Key << "CollidesWithSelf" << YAML::Value << layer.CollidesWithSelf;
						out << YAML::Key << "CollidesWith" << YAML::Value << YAML::BeginSeq;
						for (const auto& collidingLayer : layer.CollidesWith)
						{
							out << YAML::BeginMap;
							out << YAML::Key << "Name" << YAML::Value << collidingLayer;
							out << YAML::EndMap;
						}
						out << YAML::EndSeq;
						out << YAML::EndMap;
					}
					out << YAML::EndSeq;
				}

				out << YAML::EndMap;
			}

			out << YAML::Key << "Log" << YAML::Value;
			{
				out << YAML::BeginMap;
				for (auto& [name, details] : Log::EnabledTags())
				{
					if (name.empty())
						continue;

					out << YAML::Key << name << YAML::Value;
					out << YAML::BeginMap;
					out << YAML::Key << "Enabled" << YAML::Value << details.Enabled;
					out << YAML::Key << "LevelFilter" << YAML::Value << Log::LevelToString(details.LevelFilter);
					out << YAML::EndMap;
				}
				out << YAML::EndMap;
			}

			out << YAML::EndMap;
		}
		out << YAML::EndMap;

		CreateDirectoriesIfNeeded(filepath);
		std::ofstream fout(filepath);
		if (!fout.is_open())
			return false;

		fout << out.c_str();
		m_Project->OnSerialized();
		return true;
	}

	bool ProjectSerializer::SerializeRuntime(const std::filesystem::path& filepath)
	{
		ProjectInfo projectInfo;

		{
			const auto& config = m_Project->GetConfig();
			AssetHandle startScene = config.StartSceneHandle;
			if (!startScene && !config.StartScene.empty() && Project::GetEditorAssetManager())
				startScene = Project::GetEditorAssetManager()->GetAssetHandleFromFilePath(config.StartScene);

			if (!startScene)
			{
				LUX_CORE_ERROR("Error building runtime project - no start scene could be found! (StartScene: {})", config.StartScene);
				return false;
			}

			projectInfo.StartScene = startScene;
			projectInfo.AudioInfo.FileStreamingDurationThreshold = config.Audio.FileStreamingDurationThreshold;
		}

		CreateDirectoriesIfNeeded(filepath);
		FileStreamWriter serializer(filepath);
		if (!serializer.IsStreamGood())
			return false;

		serializer.WriteRaw<ProjectInfo>(projectInfo);

		const auto& physics = m_Project->GetConfig().Physics;
		serializer.WriteRaw<float>(physics.FixedTimestep);
		serializer.WriteRaw<glm::vec3>(physics.Gravity);
		serializer.WriteRaw<uint32_t>(physics.PositionSolverIterations);
		serializer.WriteRaw<uint32_t>(physics.VelocitySolverIterations);
		serializer.WriteRaw<uint32_t>(physics.MaxBodies);
		serializer.WriteRaw<bool>(physics.CaptureOnPlay);
		serializer.WriteRaw<uint8_t>((uint8_t)physics.CaptureMethod);

		serializer.WriteRaw<uint32_t>((uint32_t)physics.Layers.size());
		for (const auto& layer : physics.Layers)
			serializer.WriteString(layer.Name);

		for (const auto& layer : physics.Layers)
		{
			serializer.WriteRaw<bool>(layer.CollidesWithSelf);
			serializer.WriteArray(layer.CollidesWith);
		}

		uint32_t tagCount = 0;
		for (auto& [name, details] : Log::EnabledTags())
		{
			if (!name.empty())
				tagCount++;
		}

		serializer.WriteRaw<uint32_t>(tagCount);
		for (auto& [name, details] : Log::EnabledTags())
		{
			if (name.empty())
				continue;

			serializer.WriteString(name);
			serializer.WriteRaw<bool>(details.Enabled);
			serializer.WriteRaw<uint8_t>((uint8_t)details.LevelFilter);
		}

		WriteSceneRendererRuntimeSettings(serializer, m_Project->GetConfig().SceneRenderer);
		serializer.WriteString(m_Project->GetConfig().Name);
		serializer.WriteString(m_Project->GetConfig().ScriptModulePath.generic_string());

		return true;
	}

	bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath)
	{
		auto& config = m_Project->GetConfig();

		YAML::Node data;
		try
		{
			data = YAML::LoadFile(filepath.string());
		}
		catch (const YAML::ParserException& e)
		{
			LUX_CORE_ERROR("Failed to load project file '{0}'\n     {1}", filepath.string(), e.what());
			return false;
		}

		auto projectNode = data["Project"];
		if (!projectNode)
			return false;

		config.Name = projectNode["Name"].as<std::string>("Untitled");
		config.AssetDirectory = projectNode["AssetDirectory"].as<std::string>("Assets");
		config.ProjectDirectory = filepath.parent_path();
		config.ProjectFileName = filepath.filename().string();

		if (projectNode["AssetRegistry"])
			config.AssetRegistryPath = NormalizeRegistryPath(filepath.parent_path(), config.AssetDirectory, projectNode["AssetRegistry"].as<std::string>());
		else if (projectNode["AssetRegistryPath"])
			config.AssetRegistryPath = NormalizeRegistryPath(filepath.parent_path(), config.AssetDirectory, projectNode["AssetRegistryPath"].as<std::string>());
		else
			config.AssetRegistryPath = NormalizeRegistryPath(filepath.parent_path(), config.AssetDirectory, {});

		config.AudioCommandsRegistryPath = projectNode["AudioCommandsRegistryPath"].as<std::string>(config.AudioCommandsRegistryPath.generic_string());
		config.MeshPath = projectNode["MeshPath"].as<std::string>(config.MeshPath.generic_string());
		config.MeshSourcePath = projectNode["MeshSourcePath"].as<std::string>(config.MeshSourcePath.generic_string());
		config.AnimationPath = projectNode["AnimationPath"].as<std::string>(config.AnimationPath.generic_string());
		config.ScriptModulePath = projectNode["ScriptModulePath"].as<std::string>(config.ScriptModulePath.generic_string());
		config.DefaultNamespace = projectNode["DefaultNamespace"].as<std::string>(config.Name);
		config.AutomaticallyReloadAssembly = projectNode["AutomaticallyReloadAssembly"].as<bool>(true);
		config.EnableAutoSave = projectNode["AutoSave"].as<bool>(false);
		config.AutoSaveIntervalSeconds = projectNode["AutoSaveInterval"].as<int>(300);
		// "RenderingTechnique" key is obsolete (deferred is the only path now) and
		// is intentionally ignored when present in legacy project files.
		config.RuntimeExport = {};
		if (auto runtimeExportNode = projectNode["RuntimeExport"])
		{
			config.RuntimeExport.GameName = runtimeExportNode["GameName"].as<std::string>(config.Name);
			config.RuntimeExport.WindowWidth = runtimeExportNode["WindowWidth"].as<uint32_t>(config.RuntimeExport.WindowWidth);
			config.RuntimeExport.WindowHeight = runtimeExportNode["WindowHeight"].as<uint32_t>(config.RuntimeExport.WindowHeight);
			config.RuntimeExport.Fullscreen = runtimeExportNode["Fullscreen"].as<bool>(config.RuntimeExport.Fullscreen);
			config.RuntimeExport.VSync = runtimeExportNode["VSync"].as<bool>(config.RuntimeExport.VSync);
			config.RuntimeExport.IconPath = runtimeExportNode["IconPath"].as<std::string>(config.RuntimeExport.IconPath.generic_string());
			config.RuntimeExport.IconHandle = runtimeExportNode["IconHandle"].as<uint64_t>((uint64_t)config.RuntimeExport.IconHandle);

			if (auto targetConfigNode = runtimeExportNode["TargetConfig"])
			{
				if (targetConfigNode.IsScalar() && !IsNumericString(targetConfigNode.Scalar()))
					config.RuntimeExport.TargetConfig = RuntimeExportTargetFromString(targetConfigNode.as<std::string>());
				else
					config.RuntimeExport.TargetConfig = RuntimeExportTargetFromString(std::to_string(targetConfigNode.as<int>((int)RuntimeExportTarget::Release)));
			}
		}
		else
		{
			config.RuntimeExport.GameName = config.Name;
		}
		DeserializeSceneRendererSettings(projectNode["SceneRenderer"], config.SceneRenderer);
		config.StartScene.clear();
		config.StartSceneHandle = 0;

		if (auto startSceneNode = projectNode["StartScene"])
		{
			std::string rawStartScene = startSceneNode.IsScalar() ? startSceneNode.Scalar() : std::string{};
			if (IsNumericString(rawStartScene))
				config.StartSceneHandle = (uint64_t)std::stoull(rawStartScene);
			else
				config.StartScene = rawStartScene;
		}

		if (auto audioNode = projectNode["Audio"])
			config.Audio.FileStreamingDurationThreshold = audioNode["FileStreamingDurationThreshold"].as<double>(config.Audio.FileStreamingDurationThreshold);

		config.Physics = {};
		if (auto physicsNode = projectNode["Physics"])
		{
			config.Physics.FixedTimestep = physicsNode["FixedTimestep"].as<float>(config.Physics.FixedTimestep);
			config.Physics.Gravity = physicsNode["Gravity"].as<glm::vec3>(config.Physics.Gravity);
			config.Physics.PositionSolverIterations = physicsNode["SolverPositionIterations"].as<uint32_t>(config.Physics.PositionSolverIterations);
			config.Physics.VelocitySolverIterations = physicsNode["SolverVelocityIterations"].as<uint32_t>(config.Physics.VelocitySolverIterations);
			config.Physics.MaxBodies = physicsNode["MaxBodies"].as<uint32_t>(config.Physics.MaxBodies);
			config.Physics.CaptureOnPlay = physicsNode["CaptureOnPlay"].as<bool>(config.Physics.CaptureOnPlay);

			if (physicsNode["CaptureMethod"])
			{
				if (physicsNode["CaptureMethod"].IsScalar() && !IsNumericString(physicsNode["CaptureMethod"].Scalar()))
					config.Physics.CaptureMethod = PhysicsCaptureMethodFromString(physicsNode["CaptureMethod"].as<std::string>());
				else
					config.Physics.CaptureMethod = (PhysicsCaptureMethod)physicsNode["CaptureMethod"].as<int>((int)config.Physics.CaptureMethod);
			}

			YAML::Node physicsLayers = physicsNode["Layers"];
			if (!physicsLayers)
				physicsLayers = physicsNode["PhysicsLayers"];

			if (physicsLayers)
			{
				for (auto layerNode : physicsLayers)
				{
					ProjectPhysicsLayer layer;
					layer.Name = layerNode["Name"].as<std::string>("");
					layer.CollidesWithSelf = layerNode["CollidesWithSelf"].as<bool>(true);

					if (auto collidesWith = layerNode["CollidesWith"])
					{
						for (auto collisionLayer : collidesWith)
							layer.CollidesWith.emplace_back(collisionLayer["Name"].as<std::string>(""));
					}

					config.Physics.Layers.emplace_back(std::move(layer));
				}
			}
		}

		Log::SetDefaultTagSettings();
		if (auto logNode = projectNode["Log"])
		{
			for (auto node : logNode)
			{
				const std::string name = node.first.as<std::string>();
				auto& details = Log::EnabledTags()[name];
				details.Enabled = node.second["Enabled"].as<bool>(details.Enabled);
				details.LevelFilter = Log::LevelFromString(node.second["LevelFilter"].as<std::string>(Log::LevelToString(details.LevelFilter)));
			}
		}

		m_Project->OnDeserialized();
		return true;
	}

	bool ProjectSerializer::DeserializeRuntime(const std::filesystem::path& filepath)
	{
		FileStreamReader stream(filepath);
		if (!stream.IsStreamGood())
			return false;

		ProjectInfo projectInfo;
		stream.ReadRaw<ProjectInfo>(projectInfo);

		ProjectInfo current;
		const bool validHeader = std::memcmp(projectInfo.HeaderData.Header, current.HeaderData.Header, sizeof(current.HeaderData.Header)) == 0;
		if (!validHeader)
		{
			LUX_CORE_ERROR("Project file '{}' has an invalid runtime header", filepath.string());
			return false;
		}

		if (projectInfo.HeaderData.Version == 0 || projectInfo.HeaderData.Version > current.HeaderData.Version)
		{
			LUX_CORE_ERROR("Project version {} is not compatible with current version {}", projectInfo.HeaderData.Version, current.HeaderData.Version);
			return false;
		}

		auto& config = m_Project->GetConfig();
		config.ProjectDirectory = filepath.parent_path();
		config.ProjectFileName = filepath.filename().string();
		config.AssetDirectory = ".";
		config.StartSceneHandle = projectInfo.StartScene;
		config.Audio.FileStreamingDurationThreshold = projectInfo.AudioInfo.FileStreamingDurationThreshold;

		stream.ReadRaw<float>(config.Physics.FixedTimestep);
		stream.ReadRaw<glm::vec3>(config.Physics.Gravity);
		stream.ReadRaw<uint32_t>(config.Physics.PositionSolverIterations);
		stream.ReadRaw<uint32_t>(config.Physics.VelocitySolverIterations);
		stream.ReadRaw<uint32_t>(config.Physics.MaxBodies);
		stream.ReadRaw<bool>(config.Physics.CaptureOnPlay);

		uint8_t captureMethod = 0;
		stream.ReadRaw<uint8_t>(captureMethod);
		config.Physics.CaptureMethod = (PhysicsCaptureMethod)captureMethod;

		uint32_t physicsLayerCount = 0;
		stream.ReadRaw<uint32_t>(physicsLayerCount);
		config.Physics.Layers.clear();
		config.Physics.Layers.resize(physicsLayerCount);

		for (uint32_t i = 0; i < physicsLayerCount; i++)
			stream.ReadString(config.Physics.Layers[i].Name);

		for (uint32_t i = 0; i < physicsLayerCount; i++)
		{
			stream.ReadRaw<bool>(config.Physics.Layers[i].CollidesWithSelf);
			stream.ReadArray(config.Physics.Layers[i].CollidesWith);
		}

		Log::SetDefaultTagSettings();
		uint32_t tagCount = 0;
		stream.ReadRaw<uint32_t>(tagCount);
		for (uint32_t i = 0; i < tagCount; i++)
		{
			std::string name;
			stream.ReadString(name);

			auto& details = Log::EnabledTags()[name];
			stream.ReadRaw(details.Enabled);

			uint8_t levelFilter = 0;
			stream.ReadRaw<uint8_t>(levelFilter);
			details.LevelFilter = (Log::Level)levelFilter;
		}

		if (projectInfo.HeaderData.Version >= 2)
			ReadSceneRendererRuntimeSettings(stream, config.SceneRenderer, projectInfo.HeaderData.Version);

		if (projectInfo.HeaderData.Version >= 9)
		{
			std::string projectName;
			stream.ReadString(projectName);
			if (!projectName.empty())
				config.Name = projectName;

			std::string scriptModulePath;
			stream.ReadString(scriptModulePath);
			if (!scriptModulePath.empty())
				config.ScriptModulePath = scriptModulePath;
		}

		const std::filesystem::path overridesFile = filepath.parent_path() / "Project.yaml";
		if (std::filesystem::exists(overridesFile))
		{
			std::ifstream overridesStream(overridesFile);
			std::stringstream overridesString;
			overridesString << overridesStream.rdbuf();

			YAML::Node overridesData = YAML::Load(overridesString.str());
			YAML::Node rootNode = overridesData["Project"];
			if (rootNode)
			{
				if (auto logNode = rootNode["Log"])
				{
					for (auto node : logNode)
					{
						const std::string name = node.first.as<std::string>();
						auto& details = Log::EnabledTags()[name];
						details.Enabled = node.second["Enabled"].as<bool>(details.Enabled);
						details.LevelFilter = Log::LevelFromString(node.second["LevelFilter"].as<std::string>(Log::LevelToString(details.LevelFilter)));
					}
				}

				DeserializeSceneRendererSettings(rootNode["SceneRenderer"], config.SceneRenderer);
			}
		}

		m_Project->OnDeserialized();
		return true;
	}
}
