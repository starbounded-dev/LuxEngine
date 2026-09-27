// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ProjectSerializer.h"

#include "ProjectRuntimeFormat.h"

#include "Lux/Core/Log.h"
#include "Lux/Serialization/FileStream.h"
#include "Lux/Serialization/Yaml.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace Lux
{
	namespace
	{
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
				case 4: return "FixedResolution";
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
			if (value == "FixedResolution" || value == "Fixed" || value == "4")
				return 4;
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

		void SerializeSceneRendererSettings(Yaml::Writer& out, const ProjectSceneRendererSettings& settings)
		{
			out << Yaml::Key << "SceneRenderer" << Yaml::Value;
			out << Yaml::BeginMap;

			out << Yaml::Key << "Rendering" << Yaml::Value;
			out << Yaml::BeginMap;
			out << Yaml::Key << "QualityPreset" << Yaml::Value << QualityPresetToString(settings.QualityPreset);
			out << Yaml::Key << "FrustumCulling" << Yaml::Value << settings.EnableFrustumCulling;
			out << Yaml::Key << "OcclusionCulling" << Yaml::Value << settings.EnableOcclusionCulling;
			out << Yaml::Key << "GPUDrivenRendering" << Yaml::Value << settings.EnableGPUDrivenRendering;
			out << Yaml::Key << "MeshLODs" << Yaml::Value << settings.EnableMeshLODs;
			out << Yaml::Key << "MeshLODDistanceScale" << Yaml::Value << settings.MeshLODDistanceScale;
			out << Yaml::Key << "VariableRateShading" << Yaml::Value << settings.EnableVariableRateShading;
			out << Yaml::Key << "MeshShaders" << Yaml::Value << settings.EnableMeshShaders;
			out << Yaml::Key << "GTAO" << Yaml::Value << settings.EnableGTAO;
			out << Yaml::Key << "GTAOBentNormals" << Yaml::Value << settings.GTAOBentNormals;
			out << Yaml::Key << "GTAODenoisePasses" << Yaml::Value << settings.GTAODenoisePasses;
			out << Yaml::Key << "GTAOSliceCount" << Yaml::Value << settings.GTAOSliceCount;
			out << Yaml::Key << "GTAOStepsPerSlice" << Yaml::Value << settings.GTAOStepsPerSlice;
			out << Yaml::Key << "AOShadowTolerance" << Yaml::Value << settings.AOShadowTolerance;
			out << Yaml::Key << "SSR" << Yaml::Value << settings.EnableSSR;
			out << Yaml::Key << "JumpFloodOutline" << Yaml::Value << settings.EnableJumpFlood;
			out << Yaml::Key << "RenderScaleMode" << Yaml::Value << RenderScaleModeToString(settings.RenderScaleMode);
			out << Yaml::Key << "FixedRenderWidth" << Yaml::Value << settings.FixedRenderWidth;
			out << Yaml::Key << "FixedRenderHeight" << Yaml::Value << settings.FixedRenderHeight;
			out << Yaml::Key << "DynamicResolutionMinScale" << Yaml::Value << settings.DynamicResolutionMinScale;
			out << Yaml::Key << "DynamicResolutionMaxScale" << Yaml::Value << settings.DynamicResolutionMaxScale;
			out << Yaml::Key << "DynamicResolutionTargetGPUTime" << Yaml::Value << settings.DynamicResolutionTargetGPUTime;
			out << Yaml::Key << "TextureMipBias" << Yaml::Value << settings.TextureMipBias;
			out << Yaml::Key << "DistanceMipBias" << Yaml::Value << settings.EnableDistanceMipBias;
			out << Yaml::Key << "DistanceMipBiasStart" << Yaml::Value << settings.DistanceMipBiasStart;
			out << Yaml::Key << "DistanceMipBiasEnd" << Yaml::Value << settings.DistanceMipBiasEnd;
			out << Yaml::Key << "DistanceMipBiasMax" << Yaml::Value << settings.DistanceMipBiasMax;
			out << Yaml::Key << "OcclusionDepthBias" << Yaml::Value << settings.OcclusionDepthBias;
			out << Yaml::Key << "OcclusionBoundsScale" << Yaml::Value << settings.OcclusionBoundsScale;
			out << Yaml::Key << "GTAOResolutionScale" << Yaml::Value << settings.GTAOResolutionScale;
			out << Yaml::Key << "SSRQuality" << Yaml::Value << SSRQualityToString(settings.SSRQuality);
			out << Yaml::Key << "SSRResolutionScale" << Yaml::Value << settings.SSRResolutionScale;
			out << Yaml::Key << "SMAA" << Yaml::Value << settings.EnableSMAA;
			out << Yaml::Key << "SMAAThreshold" << Yaml::Value << settings.SMAAThreshold;
			out << Yaml::Key << "SMAALocalContrastAdaptationFactor" << Yaml::Value << settings.SMAALocalContrastAdaptationFactor;
			out << Yaml::EndMap;

			out << Yaml::Key << "Shadows" << Yaml::Value;
			out << Yaml::BeginMap;
			out << Yaml::Key << "SoftShadows" << Yaml::Value << settings.SoftShadows;
			out << Yaml::Key << "ShadowCulling" << Yaml::Value << settings.EnableShadowCulling;
			out << Yaml::Key << "MaxDistance" << Yaml::Value << settings.MaxShadowDistance;
			out << Yaml::Key << "DistanceFade" << Yaml::Value << settings.ShadowFade;
			out << Yaml::Key << "ActiveCascadeCount" << Yaml::Value << settings.ActiveShadowCascadeCount;
			out << Yaml::Key << "SplitLambda" << Yaml::Value << settings.ShadowCascadeSplitLambda;
			out << Yaml::Key << "NearOffset" << Yaml::Value << settings.ShadowCascadeNearPlaneOffset;
			out << Yaml::Key << "FarOffset" << Yaml::Value << settings.ShadowCascadeFarPlaneOffset;
			out << Yaml::Key << "CascadeFade" << Yaml::Value << settings.ShadowCascadeTransitionFade;
			out << Yaml::Key << "FilterMode" << Yaml::Value << settings.ShadowFilterMode;
			out << Yaml::Key << "DirectionalPCSSCascades" << Yaml::Value << settings.DirectionalPCSSCascadeCount;
			out << Yaml::Key << "PCFRadiusTexels" << Yaml::Value << settings.ShadowPCFRadiusTexels;
			out << Yaml::Key << "SpotPCFRadiusTexels" << Yaml::Value << settings.SpotShadowPCFRadiusTexels;
			out << Yaml::Key << "ResolutionLimit" << Yaml::Value << ShadowResolutionToString(settings.ShadowResolution);
			out << Yaml::EndMap;

			out << Yaml::Key << "PostFX" << Yaml::Value;
			out << Yaml::BeginMap;
			out << Yaml::Key << "Bloom" << Yaml::Value;
			out << Yaml::BeginMap;
			out << Yaml::Key << "Enabled" << Yaml::Value << settings.BloomEnabled;
			out << Yaml::Key << "ResolutionScale" << Yaml::Value << settings.BloomResolutionScale;
			out << Yaml::Key << "Threshold" << Yaml::Value << settings.BloomThreshold;
			out << Yaml::Key << "Knee" << Yaml::Value << settings.BloomKnee;
			out << Yaml::Key << "UpsampleScale" << Yaml::Value << settings.BloomUpsampleScale;
			out << Yaml::Key << "Intensity" << Yaml::Value << settings.BloomIntensity;
			out << Yaml::Key << "DirtIntensity" << Yaml::Value << settings.BloomDirtIntensity;
			out << Yaml::EndMap;

			out << Yaml::Key << "DOF" << Yaml::Value;
			out << Yaml::BeginMap;
			out << Yaml::Key << "Enabled" << Yaml::Value << settings.DOFEnabled;
			out << Yaml::Key << "ResolutionScale" << Yaml::Value << settings.DOFResolutionScale;
			out << Yaml::Key << "FocusDistance" << Yaml::Value << settings.DOFFocusDistance;
			out << Yaml::Key << "BlurSize" << Yaml::Value << settings.DOFBlurSize;
			out << Yaml::EndMap;

			out << Yaml::Key << "SSR" << Yaml::Value;
			out << Yaml::BeginMap;
			out << Yaml::Key << "HalfRes" << Yaml::Value << settings.SSRHalfRes;
			out << Yaml::Key << "MaxSteps" << Yaml::Value << settings.SSRMaxSteps;
			out << Yaml::Key << "Brightness" << Yaml::Value << settings.SSRBrightness;
			out << Yaml::Key << "DepthTolerance" << Yaml::Value << settings.SSRDepthTolerance;
			out << Yaml::EndMap;
			out << Yaml::EndMap;

			out << Yaml::EndMap;
		}

		void DeserializeSceneRendererSettings(const Yaml::Node& node, ProjectSceneRendererSettings& settings)
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
				settings.GTAOSliceCount = rendering["GTAOSliceCount"].as<uint32_t>(settings.GTAOSliceCount);
				settings.GTAOStepsPerSlice = rendering["GTAOStepsPerSlice"].as<uint32_t>(settings.GTAOStepsPerSlice);
				settings.AOShadowTolerance = rendering["AOShadowTolerance"].as<float>(settings.AOShadowTolerance);
				settings.EnableSSR = rendering["SSR"].as<bool>(settings.EnableSSR);
				settings.EnableJumpFlood = rendering["JumpFloodOutline"].as<bool>(settings.EnableJumpFlood);
				settings.RenderScaleMode = RenderScaleModeFromString(rendering["RenderScaleMode"].as<std::string>(RenderScaleModeToString(settings.RenderScaleMode)));
				settings.FixedRenderWidth = rendering["FixedRenderWidth"].as<uint32_t>(settings.FixedRenderWidth);
				settings.FixedRenderHeight = rendering["FixedRenderHeight"].as<uint32_t>(settings.FixedRenderHeight);
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
				hasSSRQuality = !!rendering["SSRQuality"];
				settings.SSRQuality = SSRQualityFromString(rendering["SSRQuality"].as<std::string>(SSRQualityToString(settings.SSRQuality)));
				settings.SSRResolutionScale = rendering["SSRResolutionScale"].as<uint32_t>(settings.SSRResolutionScale);
				settings.EnableSMAA = rendering["SMAA"].as<bool>(settings.EnableSMAA);
				settings.SMAAThreshold = rendering["SMAAThreshold"].as<float>(settings.SMAAThreshold);
				settings.SMAALocalContrastAdaptationFactor = rendering["SMAALocalContrastAdaptationFactor"].as<float>(settings.SMAALocalContrastAdaptationFactor);
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
				Yaml::Node shadowResolution = shadows["ResolutionLimit"] ? shadows["ResolutionLimit"] : shadows["ShadowResolution"];
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
			serializer.WriteRaw(settings.GTAOSliceCount);
			serializer.WriteRaw(settings.GTAOStepsPerSlice);
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
			serializer.WriteRaw(settings.SSRResolutionScale);
			serializer.WriteRaw(settings.SSRQuality);
			serializer.WriteRaw(settings.EnableSMAA);
			serializer.WriteRaw(settings.SMAAThreshold);
			serializer.WriteRaw(settings.SMAALocalContrastAdaptationFactor);

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
			serializer.WriteRaw(settings.FixedRenderWidth);
			serializer.WriteRaw(settings.FixedRenderHeight);
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
			// Guarded so packs written before the sample counts existed still load; they
			// keep the defaults. Position must match the write order above.
			if (version >= 16)
			{
				stream.ReadRaw(settings.GTAOSliceCount);
				stream.ReadRaw(settings.GTAOStepsPerSlice);
			}
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
				// v15 removed GTAO temporal accumulation. Older packs still carry the two
				// fields in the stream, so they must be read past or every value after this
				// point is misaligned.
				if (version < 15)
				{
					bool discardTemporal = false; float discardBlend = 0.0f;
					stream.ReadRaw(discardTemporal);
					stream.ReadRaw(discardBlend);
				}
				stream.ReadRaw(settings.SSRResolutionScale);
				if (version >= 7)
					stream.ReadRaw(settings.SSRQuality);
				// v15 removed SSR temporal accumulation - same stream-alignment reason.
				if (version < 15)
				{
					bool discardTemporal = false; float discardBlend = 0.0f;
					stream.ReadRaw(discardTemporal);
					stream.ReadRaw(discardBlend);
				}
			}

			// Guarded so packs written before the SMAA settings existed still load; they
			// simply keep the defaults.
			if (version >= 14)
			{
				stream.ReadRaw(settings.EnableSMAA);
				// v15 removed SMAA T2x - same stream-alignment reason.
				if (version < 15)
				{
					bool discardSMAATemporal = false;
					stream.ReadRaw(discardSMAATemporal);
				}
				stream.ReadRaw(settings.SMAAThreshold);
				stream.ReadRaw(settings.SMAALocalContrastAdaptationFactor);
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

			if (version >= 24)
			{
				stream.ReadRaw(settings.FixedRenderWidth);
				stream.ReadRaw(settings.FixedRenderHeight);
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
		if (!config.Audio.Accessibility.Validate())
			return false;

		Yaml::Writer out;
		out << Yaml::BeginMap;
		out << Yaml::Key << "Project" << Yaml::Value;
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "Name" << Yaml::Value << config.Name;
			out << Yaml::Key << "AssetDirectory" << Yaml::Value << config.AssetDirectory.generic_string();
			out << Yaml::Key << "AssetRegistry" << Yaml::Value << config.AssetRegistryPath.generic_string();
			out << Yaml::Key << "AudioCommandsRegistryPath" << Yaml::Value << config.AudioCommandsRegistryPath.generic_string();
			out << Yaml::Key << "MeshPath" << Yaml::Value << config.MeshPath.generic_string();
			out << Yaml::Key << "MeshSourcePath" << Yaml::Value << config.MeshSourcePath.generic_string();
			out << Yaml::Key << "AnimationPath" << Yaml::Value << config.AnimationPath.generic_string();
			out << Yaml::Key << "ScriptModulePath" << Yaml::Value << config.ScriptModulePath.generic_string();
			out << Yaml::Key << "DefaultNamespace" << Yaml::Value << config.DefaultNamespace;
			out << Yaml::Key << "StartScene" << Yaml::Value << config.StartScene;
			out << Yaml::Key << "AutomaticallyReloadAssembly" << Yaml::Value << config.AutomaticallyReloadAssembly;
			out << Yaml::Key << "AutoSave" << Yaml::Value << config.EnableAutoSave;
			out << Yaml::Key << "AutoSaveInterval" << Yaml::Value << config.AutoSaveIntervalSeconds;
			out << Yaml::Key << "RuntimeExport" << Yaml::Value;
			{
				out << Yaml::BeginMap;
				out << Yaml::Key << "GameName" << Yaml::Value << config.RuntimeExport.GameName;
				out << Yaml::Key << "WindowWidth" << Yaml::Value << config.RuntimeExport.WindowWidth;
				out << Yaml::Key << "WindowHeight" << Yaml::Value << config.RuntimeExport.WindowHeight;
				out << Yaml::Key << "Fullscreen" << Yaml::Value << config.RuntimeExport.Fullscreen;
				out << Yaml::Key << "VSync" << Yaml::Value << config.RuntimeExport.VSync;
				out << Yaml::Key << "IconPath" << Yaml::Value << config.RuntimeExport.IconPath.generic_string();
				out << Yaml::Key << "IconHandle" << Yaml::Value << (uint64_t)config.RuntimeExport.IconHandle;
				out << Yaml::Key << "TargetConfig" << Yaml::Value << RuntimeExportTargetToString(config.RuntimeExport.TargetConfig);
				out << Yaml::EndMap;
			}
			SerializeSceneRendererSettings(out, config.SceneRenderer);

			out << Yaml::Key << "Audio" << Yaml::Value;
			{
				out << Yaml::BeginMap;
				out << Yaml::Key << "FileStreamingDurationThreshold" << Yaml::Value << config.Audio.FileStreamingDurationThreshold;
				out << Yaml::Key << "StudioProjectPath" << Yaml::Value << config.Audio.StudioProjectPath.generic_string();
				if (!IsValidStudioPlatform(config.Audio.StudioPlatform) || !config.Audio.Windows.Validate() || !config.Audio.Linux.Validate())
				{
					LUX_CORE_ERROR_TAG("Audio", "Cannot save invalid desktop audio profiles");
					return false;
				}
				out << Yaml::Key << "StudioPlatform" << Yaml::Value << config.Audio.StudioPlatform;
				out << Yaml::Key << "Windows" << Yaml::Value;
				config.Audio.Windows.SerializeYAML(out);
				out << Yaml::Key << "Linux" << Yaml::Value;
				config.Audio.Linux.SerializeYAML(out);
				out << Yaml::Key << "StudioBankOutputPath" << Yaml::Value << config.Audio.StudioBankOutputPath.generic_string();
				out << Yaml::Key << "RebuildBanksOnPlay" << Yaml::Value << config.Audio.RebuildBanksOnPlay;
				out << Yaml::Key << "EnableLiveUpdate" << Yaml::Value << config.Audio.EnableLiveUpdate;
				if (!config.Audio.AcousticMaterials.Validate())
					return false;
				out << Yaml::Key << "AcousticMaterials" << Yaml::Value;
				config.Audio.AcousticMaterials.SerializeYAML(out);
				out << Yaml::Key << "SurfaceTable" << Yaml::Value << static_cast<uint64_t>(config.Audio.SurfaceTable);
				out << Yaml::Key << "DialogueTable" << Yaml::Value << static_cast<uint64_t>(config.Audio.Dialogue.Table);
				out << Yaml::Key << "DialogueLanguage" << Yaml::Value << config.Audio.Dialogue.Language;
				out << Yaml::Key << "Accessibility" << Yaml::Value;
				config.Audio.Accessibility.SerializeYAML(out);
				out << Yaml::Key << "Performance" << Yaml::Value;
				config.Audio.Performance.SerializeYAML(out);
				out << Yaml::Key << "ZoneReverbMode" << Yaml::Value << static_cast<uint32_t>(config.Audio.ZoneReverbMode);
				out << Yaml::Key << "Occlusion" << Yaml::Value;
				config.Audio.Occlusion.SerializeYAML(out);
				out << Yaml::EndMap;
			}

			out << Yaml::Key << "Physics" << Yaml::Value;
			{
				out << Yaml::BeginMap;
				out << Yaml::Key << "FixedTimestep" << Yaml::Value << config.Physics.FixedTimestep;
				out << Yaml::Key << "Gravity" << Yaml::Value << config.Physics.Gravity;
				out << Yaml::Key << "SolverPositionIterations" << Yaml::Value << config.Physics.PositionSolverIterations;
				out << Yaml::Key << "SolverVelocityIterations" << Yaml::Value << config.Physics.VelocitySolverIterations;
				out << Yaml::Key << "MaxBodies" << Yaml::Value << config.Physics.MaxBodies;
				out << Yaml::Key << "CaptureOnPlay" << Yaml::Value << config.Physics.CaptureOnPlay;
				out << Yaml::Key << "CaptureMethod" << Yaml::Value << PhysicsCaptureMethodToString(config.Physics.CaptureMethod);

				if (!config.Physics.Layers.empty())
				{
					out << Yaml::Key << "Layers" << Yaml::Value << Yaml::BeginSeq;
					for (const auto& layer : config.Physics.Layers)
					{
						out << Yaml::BeginMap;
						out << Yaml::Key << "Name" << Yaml::Value << layer.Name;
						out << Yaml::Key << "CollidesWithSelf" << Yaml::Value << layer.CollidesWithSelf;
						out << Yaml::Key << "CollidesWith" << Yaml::Value << Yaml::BeginSeq;
						for (const auto& collidingLayer : layer.CollidesWith)
						{
							out << Yaml::BeginMap;
							out << Yaml::Key << "Name" << Yaml::Value << collidingLayer;
							out << Yaml::EndMap;
						}
						out << Yaml::EndSeq;
						out << Yaml::EndMap;
					}
					out << Yaml::EndSeq;
				}

				out << Yaml::EndMap;
			}

			out << Yaml::Key << "Log" << Yaml::Value;
			{
				out << Yaml::BeginMap;
				for (auto& [name, details] : Log::EnabledTags())
				{
					if (name.empty())
						continue;

					out << Yaml::Key << name << Yaml::Value;
					out << Yaml::BeginMap;
					out << Yaml::Key << "Enabled" << Yaml::Value << details.Enabled;
					out << Yaml::Key << "LevelFilter" << Yaml::Value << Log::LevelToString(details.LevelFilter);
					out << Yaml::EndMap;
				}
				out << Yaml::EndMap;
			}

			out << Yaml::EndMap;
		}
		out << Yaml::EndMap;

		CreateDirectoriesIfNeeded(filepath);
		std::ofstream fout(filepath);
		if (!fout.is_open())
			return false;

		fout << out.c_str();
		m_Project->OnSerialized();
		return true;
	}

	bool ProjectSerializer::SerializeRuntime(const std::filesystem::path& filepath, const AudioBankManifest& banks)
	{
		if (!m_Project->GetConfig().Audio.Accessibility.Validate())
			return false;
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
		if (!banks.Serialize(serializer) || !m_Project->GetConfig().Audio.AcousticMaterials.Serialize(serializer))
			return false;

		const auto zoneMode = m_Project->GetConfig().Audio.ZoneReverbMode;
		if (zoneMode > AudioZoneReverbMode::PreferRaytraced)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot export an invalid zone reverb mode");
			return false;
		}
		serializer.WriteRaw<uint8_t>(static_cast<uint8_t>(zoneMode));
		serializer.WriteRaw<uint64_t>(m_Project->GetConfig().Audio.SurfaceTable);
		if (!m_Project->GetConfig().Audio.Dialogue.Serialize(serializer) || !m_Project->GetConfig().Audio.Accessibility.Serialize(serializer) || !m_Project->GetAudioPerformance().Serialize(serializer)
			|| !m_Project->GetConfig().Audio.Occlusion.Serialize(serializer))
			return false;

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

		if (!serializer.IsStreamGood())
		{
			LUX_CORE_ERROR_TAG("Project", "Failed to write runtime project: {0}", filepath.string());
			return false;
		}
		return true;
	}

	bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath)
	{
		auto& config = m_Project->GetConfig();

		Yaml::Node data;
		try
		{
			data = Yaml::LoadFile(filepath.string());
		}
		catch (const Yaml::Exception& e)
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
				if (targetConfigNode.IsScalar() && !IsNumericString(std::string(targetConfigNode.Scalar())))
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
			std::string rawStartScene = startSceneNode.IsScalar() ? std::string(startSceneNode.Scalar()) : std::string{};
			if (IsNumericString(rawStartScene))
				config.StartSceneHandle = (uint64_t)std::stoull(rawStartScene);
			else
				config.StartScene = rawStartScene;
		}

		config.Audio.RuntimeBanks = {};
		config.Audio.SurfaceTable = 0;
		config.Audio.Dialogue = {};
		config.Audio.Accessibility = {};
		config.Audio.Performance = {};
		config.Audio.Windows = {};
		config.Audio.Linux = {};
		config.Audio.StudioPlatform = "Desktop";
		config.Audio.AcousticMaterials = {};
		config.Audio.ZoneReverbMode = AudioZoneReverbMode::Layered;
		config.Audio.Occlusion = {};
		if (auto audioNode = projectNode["Audio"])
		{
			config.Audio.FileStreamingDurationThreshold = audioNode["FileStreamingDurationThreshold"].as<double>(config.Audio.FileStreamingDurationThreshold);
			config.Audio.StudioProjectPath = audioNode["StudioProjectPath"].as<std::string>(config.Audio.StudioProjectPath.generic_string());
			config.Audio.StudioBankOutputPath = audioNode["StudioBankOutputPath"].as<std::string>(config.Audio.StudioBankOutputPath.generic_string());
			config.Audio.RebuildBanksOnPlay = audioNode["RebuildBanksOnPlay"].as<bool>(config.Audio.RebuildBanksOnPlay);
			config.Audio.EnableLiveUpdate = audioNode["EnableLiveUpdate"].as<bool>(config.Audio.EnableLiveUpdate);
			config.Audio.SurfaceTable = audioNode["SurfaceTable"].as<uint64_t>(0);
			config.Audio.StudioPlatform = audioNode["StudioPlatform"].as<std::string>("Desktop");
			if (!IsValidStudioPlatform(config.Audio.StudioPlatform))
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot load invalid FMOD Studio platform name");
				return false;
			}
			if (!config.Audio.Windows.DeserializeYAML(audioNode["Windows"]) || !config.Audio.Linux.DeserializeYAML(audioNode["Linux"]))
				return false;
			if (!config.Audio.Performance.DeserializeYAML(audioNode["Performance"]))
				return false;
			if (!config.Audio.Accessibility.DeserializeYAML(audioNode["Accessibility"]))
				return false;
			config.Audio.Dialogue.Table = audioNode["DialogueTable"].as<uint64_t>(0);
			config.Audio.Dialogue.Language = audioNode["DialogueLanguage"].as<std::string>("en");
			if (!DialogueTable::ValidLanguage(config.Audio.Dialogue.Language))
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid project dialogue language");
				return false;
			}
			const auto zoneMode = audioNode["ZoneReverbMode"].as<uint32_t>(0);
			if (zoneMode > static_cast<uint32_t>(AudioZoneReverbMode::PreferRaytraced))
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid project audio zone reverb mode {0}", zoneMode);
				return false;
			}
			config.Audio.ZoneReverbMode = static_cast<AudioZoneReverbMode>(zoneMode);
			if (!config.Audio.AcousticMaterials.DeserializeYAML(audioNode["AcousticMaterials"]))
				return false;
			if (!config.Audio.Occlusion.DeserializeYAML(audioNode["Occlusion"]))
				return false;
		}

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
				if (physicsNode["CaptureMethod"].IsScalar() && !IsNumericString(std::string(physicsNode["CaptureMethod"].Scalar())))
					config.Physics.CaptureMethod = PhysicsCaptureMethodFromString(physicsNode["CaptureMethod"].as<std::string>());
				else
					config.Physics.CaptureMethod = (PhysicsCaptureMethod)physicsNode["CaptureMethod"].as<int>((int)config.Physics.CaptureMethod);
			}

			Yaml::Node physicsLayers = physicsNode["Layers"];
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
		if (!stream.ReadData(reinterpret_cast<char*>(&projectInfo), sizeof(projectInfo)) || !stream.IsStreamGood())
		{
			LUX_CORE_ERROR_TAG("Project", "Truncated runtime project header: {0}", filepath.string());
			return false;
		}

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
		config.Audio = {};
		config.Audio.FileStreamingDurationThreshold = projectInfo.AudioInfo.FileStreamingDurationThreshold;
		config.Audio.StudioProjectPath.clear();
		config.Audio.StudioBankOutputPath.clear();
		config.Audio.RebuildBanksOnPlay = false;
		config.Audio.EnableLiveUpdate = false;
		if (projectInfo.HeaderData.Version >= 17)
		{
			if (!config.Audio.RuntimeBanks.Deserialize(stream))
				return false;
			config.Audio.EnableLiveUpdate = config.Audio.RuntimeBanks.EnableLiveUpdate;
		}
		else
			LUX_CORE_WARN_TAG("Audio", "Runtime project predates packaged FMOD banks; re-export it to enable Studio events");

		if (projectInfo.HeaderData.Version >= 18 && !config.Audio.AcousticMaterials.Deserialize(stream))
			return false;

		config.Audio.ZoneReverbMode = AudioZoneReverbMode::Layered;
		if (projectInfo.HeaderData.Version >= 19)
		{
			uint8_t mode = 0;
			if (!stream.ReadData(reinterpret_cast<char*>(&mode), sizeof(mode)) || !stream.IsStreamGood() || mode > static_cast<uint8_t>(AudioZoneReverbMode::PreferRaytraced))
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid or truncated runtime zone reverb settings");
				return false;
			}
			config.Audio.ZoneReverbMode = static_cast<AudioZoneReverbMode>(mode);
		}

		if (projectInfo.HeaderData.Version >= 20)
		{
			uint64_t table = 0;
			if (!stream.ReadData(reinterpret_cast<char*>(&table), sizeof(table)))
			{
				LUX_CORE_ERROR_TAG("Audio", "Truncated runtime surface table reference");
				return false;
			}
			config.Audio.SurfaceTable = table;
		}

		config.Audio.Dialogue = {};
		config.Audio.Accessibility = {};
		config.Audio.Performance = {};
		config.Audio.Windows = {};
		config.Audio.Linux = {};
		config.Audio.StudioPlatform = "Desktop";
		if (projectInfo.HeaderData.Version >= 21 && !config.Audio.Dialogue.Deserialize(stream))
			return false;
		if (projectInfo.HeaderData.Version >= 22 && !config.Audio.Accessibility.Deserialize(stream))
			return false;
		if (projectInfo.HeaderData.Version >= 23 && !config.Audio.Performance.Deserialize(stream))
			return false;
		config.Audio.Occlusion = {};
		if (projectInfo.HeaderData.Version >= 25 && !config.Audio.Occlusion.Deserialize(stream))
			return false;

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

			Yaml::Node overridesData = Yaml::Load(overridesString.str());
			Yaml::Node rootNode = overridesData["Project"];
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
