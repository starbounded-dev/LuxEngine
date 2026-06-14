#include "lpch.h"
#include "RenderVolumes.h"

#include "Lux/Core/Math/AABB.h"
#include "Lux/Core/Math/Frustum.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>

namespace Lux {

	namespace {

		template<typename T>
		T BlendValue(const T& a, const T& b, float weight)
		{
			return glm::mix(a, b, weight);
		}

		bool BlendBool(bool a, bool b, float weight)
		{
			return weight >= 0.5f ? b : a;
		}

		uint32_t BlendUInt(uint32_t a, uint32_t b, float weight)
		{
			return weight >= 0.5f ? b : a;
		}

		void BlendPostProcess(RenderVolumePostProcessSettings& target, const PostProcessVolumeComponent& volume, float weight)
		{
			const RenderVolumePostProcessSettings& settings = volume.Settings;
			const uint64_t mask = volume.OverrideMask;

			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_Exposure))
				target.Exposure = BlendValue(target.Exposure, settings.Exposure, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_BloomEnabled))
				target.BloomEnabled = BlendBool(target.BloomEnabled, settings.BloomEnabled, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_BloomThreshold))
				target.BloomThreshold = BlendValue(target.BloomThreshold, settings.BloomThreshold, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_BloomKnee))
				target.BloomKnee = BlendValue(target.BloomKnee, settings.BloomKnee, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_BloomUpsampleScale))
				target.BloomUpsampleScale = BlendValue(target.BloomUpsampleScale, settings.BloomUpsampleScale, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_BloomIntensity))
				target.BloomIntensity = BlendValue(target.BloomIntensity, settings.BloomIntensity, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_BloomDirtIntensity))
				target.BloomDirtIntensity = BlendValue(target.BloomDirtIntensity, settings.BloomDirtIntensity, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_DOFEnabled))
				target.DOFEnabled = BlendBool(target.DOFEnabled, settings.DOFEnabled, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_DOFFocusDistance))
				target.DOFFocusDistance = BlendValue(target.DOFFocusDistance, settings.DOFFocusDistance, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_DOFBlurSize))
				target.DOFBlurSize = BlendValue(target.DOFBlurSize, settings.DOFBlurSize, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_ColorFilter))
				target.ColorFilter = BlendValue(target.ColorFilter, settings.ColorFilter, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_Saturation))
				target.Saturation = BlendValue(target.Saturation, settings.Saturation, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_Contrast))
				target.Contrast = BlendValue(target.Contrast, settings.Contrast, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_Gamma))
				target.Gamma = BlendValue(target.Gamma, settings.Gamma, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_ExposureMode))
				target.ExposureControl = weight >= 0.5f ? settings.ExposureControl : target.ExposureControl;
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_Aperture))
				target.Aperture = BlendValue(target.Aperture, settings.Aperture, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_ShutterSpeed))
				target.ShutterSpeed = BlendValue(target.ShutterSpeed, settings.ShutterSpeed, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_ISO))
				target.ISO = BlendValue(target.ISO, settings.ISO, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_ExposureEV100))
				target.ExposureEV100 = BlendValue(target.ExposureEV100, settings.ExposureEV100, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_ExposureCompensation))
				target.ExposureCompensation = BlendValue(target.ExposureCompensation, settings.ExposureCompensation, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_AutoMinEV100))
				target.AutoMinEV100 = BlendValue(target.AutoMinEV100, settings.AutoMinEV100, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_AutoMaxEV100))
				target.AutoMaxEV100 = BlendValue(target.AutoMaxEV100, settings.AutoMaxEV100, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_AutoAdaptationSpeedUp))
				target.AutoAdaptationSpeedUp = BlendValue(target.AutoAdaptationSpeedUp, settings.AutoAdaptationSpeedUp, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_AutoAdaptationSpeedDown))
				target.AutoAdaptationSpeedDown = BlendValue(target.AutoAdaptationSpeedDown, settings.AutoAdaptationSpeedDown, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_Tonemap))
				target.Tonemap = weight >= 0.5f ? settings.Tonemap : target.Tonemap;
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_WhiteBalance))
			{
				target.WhiteTemperature = BlendValue(target.WhiteTemperature, settings.WhiteTemperature, weight);
				target.WhiteTint = BlendValue(target.WhiteTint, settings.WhiteTint, weight);
			}
			if (RenderVolumeEvaluator::HasOverride(mask, PostProcessOverride_LiftGammaGain))
			{
				target.Lift = BlendValue(target.Lift, settings.Lift, weight);
				target.GradeGamma = BlendValue(target.GradeGamma, settings.GradeGamma, weight);
				target.Gain = BlendValue(target.Gain, settings.Gain, weight);
			}
		}

		void BlendSky(SkyAtmosphereSettings& target, const SkyAtmosphereSettings& settings, uint64_t mask, float weight)
		{
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_Enabled))
				target.Enabled = BlendBool(target.Enabled, settings.Enabled, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_PlanetRadius))
				target.PlanetRadius = BlendValue(target.PlanetRadius, settings.PlanetRadius, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_AtmosphereHeight))
				target.AtmosphereHeight = BlendValue(target.AtmosphereHeight, settings.AtmosphereHeight, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_RayleighScaleHeight))
				target.RayleighScaleHeight = BlendValue(target.RayleighScaleHeight, settings.RayleighScaleHeight, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_MieScaleHeight))
				target.MieScaleHeight = BlendValue(target.MieScaleHeight, settings.MieScaleHeight, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_RayleighScattering))
				target.RayleighScattering = BlendValue(target.RayleighScattering, settings.RayleighScattering, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_RayleighScatteringScale))
				target.RayleighScatteringScale = BlendValue(target.RayleighScatteringScale, settings.RayleighScatteringScale, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_MieScattering))
				target.MieScattering = BlendValue(target.MieScattering, settings.MieScattering, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_MieScatteringScale))
				target.MieScatteringScale = BlendValue(target.MieScatteringScale, settings.MieScatteringScale, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_MieAbsorption))
				target.MieAbsorption = BlendValue(target.MieAbsorption, settings.MieAbsorption, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_MieAnisotropy))
				target.MieAnisotropy = BlendValue(target.MieAnisotropy, settings.MieAnisotropy, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_Absorption))
				target.Absorption = BlendValue(target.Absorption, settings.Absorption, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_AbsorptionScale))
				target.AbsorptionScale = BlendValue(target.AbsorptionScale, settings.AbsorptionScale, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_GroundAlbedo))
				target.GroundAlbedo = BlendValue(target.GroundAlbedo, settings.GroundAlbedo, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_GroundContribution))
				target.GroundContribution = BlendValue(target.GroundContribution, settings.GroundContribution, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_SunIntensity))
				target.SunIntensity = BlendValue(target.SunIntensity, settings.SunIntensity, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_SunAngularRadius))
				target.SunAngularRadius = BlendValue(target.SunAngularRadius, settings.SunAngularRadius, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_MultiScattering))
				target.MultiScattering = BlendValue(target.MultiScattering, settings.MultiScattering, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, SkyOverride_AerialPerspectiveViewDistanceScale))
				target.AerialPerspectiveViewDistanceScale = BlendValue(target.AerialPerspectiveViewDistanceScale, settings.AerialPerspectiveViewDistanceScale, weight);
		}

		void BlendClouds(VolumetricCloudSettings& target, const VolumetricCloudSettings& settings, uint64_t mask, float weight)
		{
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_Enabled))
				target.Enabled = BlendBool(target.Enabled, settings.Enabled, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_Coverage))
				target.Coverage = BlendValue(target.Coverage, settings.Coverage, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_Density))
				target.Density = BlendValue(target.Density, settings.Density, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_Altitude))
				target.Altitude = BlendValue(target.Altitude, settings.Altitude, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_Thickness))
				target.Thickness = BlendValue(target.Thickness, settings.Thickness, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_WindDirection))
				target.WindDirection = BlendValue(target.WindDirection, settings.WindDirection, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_WindSpeed))
				target.WindSpeed = BlendValue(target.WindSpeed, settings.WindSpeed, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_ShapeScale))
				target.ShapeScale = BlendValue(target.ShapeScale, settings.ShapeScale, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_DetailScale))
				target.DetailScale = BlendValue(target.DetailScale, settings.DetailScale, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_DetailStrength))
				target.DetailStrength = BlendValue(target.DetailStrength, settings.DetailStrength, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_Absorption))
				target.Absorption = BlendValue(target.Absorption, settings.Absorption, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_SilverIntensity))
				target.SilverIntensity = BlendValue(target.SilverIntensity, settings.SilverIntensity, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_Albedo))
				target.Albedo = BlendValue(target.Albedo, settings.Albedo, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_AmbientBoost))
				target.AmbientBoost = BlendValue(target.AmbientBoost, settings.AmbientBoost, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_MaxTraceDistance))
				target.MaxTraceDistance = BlendValue(target.MaxTraceDistance, settings.MaxTraceDistance, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_DistanceFade))
				target.DistanceFade = BlendValue(target.DistanceFade, settings.DistanceFade, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_LODStartDistance))
				target.LODStartDistance = BlendValue(target.LODStartDistance, settings.LODStartDistance, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_ShadowTraceDistance))
				target.ShadowTraceDistance = BlendValue(target.ShadowTraceDistance, settings.ShadowTraceDistance, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_MarchSteps))
				target.MarchSteps = BlendUInt(target.MarchSteps, settings.MarchSteps, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_ShadowSteps))
				target.ShadowSteps = BlendUInt(target.ShadowSteps, settings.ShadowSteps, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, CloudOverride_RenderScale))
				target.RenderScale = BlendUInt(target.RenderScale, settings.RenderScale, weight);
		}

		void BlendFog(ExponentialHeightFogSettings& target, const ExponentialHeightFogSettings& settings, uint64_t mask, float weight)
		{
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_Enabled))
				target.Enabled = BlendBool(target.Enabled, settings.Enabled, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_FogColor))
				target.FogColor = BlendValue(target.FogColor, settings.FogColor, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_FogDensity))
				target.FogDensity = BlendValue(target.FogDensity, settings.FogDensity, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_FogHeightFalloff))
				target.FogHeightFalloff = BlendValue(target.FogHeightFalloff, settings.FogHeightFalloff, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_StartDistance))
				target.StartDistance = BlendValue(target.StartDistance, settings.StartDistance, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_MaxOpacity))
				target.MaxOpacity = BlendValue(target.MaxOpacity, settings.MaxOpacity, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_CutoffDistance))
				target.CutoffDistance = BlendValue(target.CutoffDistance, settings.CutoffDistance, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_DirectionalInscatteringColor))
				target.DirectionalInscatteringColor = BlendValue(target.DirectionalInscatteringColor, settings.DirectionalInscatteringColor, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_DirectionalInscatteringExponent))
				target.DirectionalInscatteringExponent = BlendValue(target.DirectionalInscatteringExponent, settings.DirectionalInscatteringExponent, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_DirectionalInscatteringStartDistance))
				target.DirectionalInscatteringStartDistance = BlendValue(target.DirectionalInscatteringStartDistance, settings.DirectionalInscatteringStartDistance, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_VolumetricFog))
				target.VolumetricFog = BlendBool(target.VolumetricFog, settings.VolumetricFog, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_VolumetricScatteringIntensity))
				target.VolumetricScatteringIntensity = BlendValue(target.VolumetricScatteringIntensity, settings.VolumetricScatteringIntensity, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_Anisotropy))
				target.Anisotropy = BlendValue(target.Anisotropy, settings.Anisotropy, weight);
			if (RenderVolumeEvaluator::HasOverride(mask, FogOverride_VolumetricFogSteps))
				target.VolumetricFogSteps = BlendUInt(target.VolumetricFogSteps, settings.VolumetricFogSteps, weight);
		}

		float GetApproximateVolumeRadius(const glm::mat4& transform)
		{
			const float x = glm::length(glm::vec3(transform[0]));
			const float y = glm::length(glm::vec3(transform[1]));
			const float z = glm::length(glm::vec3(transform[2]));
			return 0.5f * glm::max(x, glm::max(y, z));
		}

		LocalFogVolumeGPUData BuildLocalFogGPUData(const RenderVolumeEvaluationInput& input)
		{
			LocalFogVolumeGPUData data;
			data.WorldToLocal = glm::inverse(input.WorldTransform);
			data.ColorDensity = glm::vec4(glm::max(input.LocalFog->Color, glm::vec3(0.0f)), glm::max(input.LocalFog->Density, 0.0f));
			data.Params0 = {
				glm::clamp(input.LocalFog->Anisotropy, -0.8f, 0.8f),
				glm::max(input.LocalFog->HeightFalloff, 0.0f),
				glm::max(input.LocalFog->NoiseScale, 0.000001f),
				glm::clamp(input.LocalFog->NoiseStrength, 0.0f, 1.0f)
			};
			data.Params1 = {
				glm::max(input.LocalFog->MaxDistance, 0.0f),
				glm::max(input.LocalFog->Falloff, 0.001f),
				0.0f,
				0.0f
			};
			data.Metadata = {
				static_cast<uint32_t>(input.Volume.Shape),
				static_cast<uint32_t>(static_cast<uint64_t>(input.EntityID) & 0xffffffffull),
				static_cast<uint32_t>(glm::max(input.Volume.Priority, 0)),
				0u
			};
			return data;
		}

	} // namespace

	float RenderVolumeEvaluator::ComputeWeight(const RenderVolumeComponent& volume, const glm::mat4& worldTransform, const glm::vec3& cameraPosition)
	{
		if (!volume.Enabled)
			return 0.0f;

		const float blendWeight = glm::clamp(volume.BlendWeight, 0.0f, 1.0f);
		if (volume.Unbound)
			return blendWeight;

		const glm::mat4 localFromWorld = glm::inverse(worldTransform);
		const glm::vec3 localPosition = glm::vec3(localFromWorld * glm::vec4(cameraPosition, 1.0f));

		bool inside = false;
		glm::vec3 nearestLocal = localPosition;
		if (volume.Shape == RenderVolumeShape::Sphere)
		{
			const float radius = 0.5f;
			const float length = glm::length(localPosition);
			inside = length <= radius;
			if (length > 0.00001f)
				nearestLocal = localPosition / length * radius;
		}
		else
		{
			const glm::vec3 halfExtent(0.5f);
			const glm::vec3 absPosition = glm::abs(localPosition);
			inside = absPosition.x <= halfExtent.x && absPosition.y <= halfExtent.y && absPosition.z <= halfExtent.z;
			nearestLocal = glm::clamp(localPosition, -halfExtent, halfExtent);
		}

		if (inside)
			return blendWeight;

		const glm::vec3 nearestWorld = glm::vec3(worldTransform * glm::vec4(nearestLocal, 1.0f));
		const float distance = glm::length(cameraPosition - nearestWorld);
		const float blendDistance = glm::max(volume.BlendDistance, 0.0001f);
		return blendWeight * glm::clamp(1.0f - distance / blendDistance, 0.0f, 1.0f);
	}

	RenderVolumeEnvironment RenderVolumeEvaluator::Evaluate(
		const std::vector<RenderVolumeEvaluationInput>& volumes,
		const glm::vec3& cameraPosition,
		const Frustum* viewFrustum,
		const RenderVolumeBaseSettings& baseSettings)
	{
		RenderVolumeEnvironment result;
		result.PostProcess = baseSettings.PostProcess;
		result.Atmosphere = baseSettings.Atmosphere;

		struct WeightedVolume
		{
			const RenderVolumeEvaluationInput* Input = nullptr;
			float Weight = 0.0f;
		};

		std::vector<WeightedVolume> weightedVolumes;
		weightedVolumes.reserve(volumes.size());
		for (const RenderVolumeEvaluationInput& volume : volumes)
		{
			if (!volume.Volume.Enabled)
				continue;

			const float weight = ComputeWeight(volume.Volume, volume.WorldTransform, cameraPosition);
			if (weight <= 0.0f && !volume.LocalFog)
				continue;

			if (weight > 0.0f)
			{
				result.ActiveVolumeCount++;
				if (volume.PostProcess)
					result.ActivePostProcessVolumeCount++;
				if (volume.Atmosphere)
					result.ActiveAtmosphereVolumeCount++;
				if (volume.Selected)
					result.SelectedVolumeInfluence = glm::max(result.SelectedVolumeInfluence, weight);

				weightedVolumes.push_back({ &volume, weight });
			}
		}

		std::sort(weightedVolumes.begin(), weightedVolumes.end(), [](const WeightedVolume& a, const WeightedVolume& b)
		{
			if (a.Input->Volume.Priority != b.Input->Volume.Priority)
				return a.Input->Volume.Priority < b.Input->Volume.Priority;
			return static_cast<uint64_t>(a.Input->EntityID) < static_cast<uint64_t>(b.Input->EntityID);
		});

		for (const WeightedVolume& weighted : weightedVolumes)
		{
			const RenderVolumeEvaluationInput& input = *weighted.Input;
			if (input.PostProcess)
				BlendPostProcess(result.PostProcess, *input.PostProcess, weighted.Weight);
			if (input.Atmosphere)
			{
				BlendSky(result.Atmosphere.SkyAtmosphere, input.Atmosphere->Settings.SkyAtmosphere, input.Atmosphere->SkyOverrideMask, weighted.Weight);
				BlendClouds(result.Atmosphere.VolumetricClouds, input.Atmosphere->Settings.VolumetricClouds, input.Atmosphere->CloudOverrideMask, weighted.Weight);
				BlendFog(result.Atmosphere.HeightFog, input.Atmosphere->Settings.HeightFog, input.Atmosphere->FogOverrideMask, weighted.Weight);
			}
		}

		struct LocalFogCandidate
		{
			const RenderVolumeEvaluationInput* Input = nullptr;
			float Distance = 0.0f;
		};

		std::vector<LocalFogCandidate> fogCandidates;
		for (const RenderVolumeEvaluationInput& volume : volumes)
		{
			if (!volume.Volume.Enabled || !volume.LocalFog)
				continue;

			result.ActiveLocalFogVolumeCount++;

			const glm::vec3 center = glm::vec3(volume.WorldTransform[3]);
			const float radius = GetApproximateVolumeRadius(volume.WorldTransform);
			const float cameraDistance = glm::max(0.0f, glm::length(center - cameraPosition) - radius);
			if (cameraDistance > volume.LocalFog->MaxDistance)
			{
				result.CulledLocalFogVolumeCount++;
				continue;
			}

			if (viewFrustum && !viewFrustum->IsSphereVisible({ center, radius }))
			{
				result.CulledLocalFogVolumeCount++;
				continue;
			}

			fogCandidates.push_back({ &volume, cameraDistance });
		}

		std::sort(fogCandidates.begin(), fogCandidates.end(), [](const LocalFogCandidate& a, const LocalFogCandidate& b)
		{
			if (a.Input->Volume.Priority != b.Input->Volume.Priority)
				return a.Input->Volume.Priority > b.Input->Volume.Priority;
			if (std::abs(a.Distance - b.Distance) > 0.001f)
				return a.Distance < b.Distance;
			return static_cast<uint64_t>(a.Input->EntityID) < static_cast<uint64_t>(b.Input->EntityID);
		});

		result.DroppedLocalFogVolumeCount = fogCandidates.size() > MaxVisibleLocalFogVolumes ? static_cast<uint32_t>(fogCandidates.size() - MaxVisibleLocalFogVolumes) : 0u;
		result.LocalFogVolumeCount = glm::min(static_cast<uint32_t>(fogCandidates.size()), MaxVisibleLocalFogVolumes);
		for (uint32_t index = 0; index < result.LocalFogVolumeCount; index++)
			result.LocalFogVolumes[index] = BuildLocalFogGPUData(*fogCandidates[index].Input);

		return result;
	}

	bool RenderVolumeEvaluator::RunSelfTests()
	{
		RenderVolumeComponent box;
		box.Shape = RenderVolumeShape::Box;
		box.BlendDistance = 2.0f;
		box.BlendWeight = 1.0f;
		const glm::mat4 transform = glm::mat4(1.0f);

		const bool insideBox = ComputeWeight(box, transform, glm::vec3(0.0f)) > 0.999f;
		const bool fadedBox = std::abs(ComputeWeight(box, transform, glm::vec3(1.5f, 0.0f, 0.0f)) - 0.5f) < 0.01f;

		RenderVolumeBaseSettings baseSettings;
		baseSettings.PostProcess.Exposure = 1.0f;

		PostProcessVolumeComponent postA;
		postA.OverrideMask = PostProcessOverride_Exposure;
		postA.Settings.Exposure = 2.0f;

		PostProcessVolumeComponent postB;
		postB.OverrideMask = PostProcessOverride_Exposure;
		postB.Settings.Exposure = 4.0f;

		RenderVolumeEvaluationInput low;
		low.EntityID = 10;
		low.Volume = box;
		low.Volume.Priority = 0;
		low.PostProcess = &postA;

		RenderVolumeEvaluationInput high = low;
		high.EntityID = 11;
		high.Volume.Priority = 1;
		high.PostProcess = &postB;

		RenderVolumeEnvironment env = Evaluate({ low, high }, glm::vec3(0.0f), nullptr, baseSettings);
		const bool priorityBlend = std::abs(env.PostProcess.Exposure - 4.0f) < 0.001f;

		std::vector<RenderVolumeEvaluationInput> manyFog;
		manyFog.reserve(MaxVisibleLocalFogVolumes + 2);
		LocalFogVolumeComponent fog;
		for (uint32_t i = 0; i < MaxVisibleLocalFogVolumes + 2; i++)
		{
			RenderVolumeEvaluationInput input;
			input.EntityID = UUID(static_cast<uint64_t>(1000 + i));
			input.Volume = box;
			input.Volume.Priority = static_cast<int32_t>(i);
			input.LocalFog = &fog;
			manyFog.push_back(input);
		}

		env = Evaluate(manyFog, glm::vec3(0.0f), nullptr, baseSettings);
		const bool fogCap = env.LocalFogVolumeCount == MaxVisibleLocalFogVolumes && env.DroppedLocalFogVolumeCount == 2;

		const bool success = insideBox && fadedBox && priorityBlend && fogCap;
		if (!success)
			LUX_CORE_WARN_TAG("Renderer", "RenderVolumeEvaluator self tests failed: inside={}, fade={}, priority={}, fogCap={}", insideBox, fadedBox, priorityBlend, fogCap);
		return success;
	}

} // namespace Lux
