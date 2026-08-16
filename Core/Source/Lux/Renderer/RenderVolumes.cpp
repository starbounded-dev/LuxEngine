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

		float GetApproximateVolumeRadius(const glm::mat4& transform)
		{
			const float x = glm::length(glm::vec3(transform[0]));
			const float y = glm::length(glm::vec3(transform[1]));
			const float z = glm::length(glm::vec3(transform[2]));
			return 0.5f * glm::max(x, glm::max(y, z));
		}

	} // namespace

	float RenderVolumeEvaluator::ComputeWeight(const RenderVolumeComponent& volume, const glm::mat4& worldTransform, const glm::vec3& cameraPosition)
	{
		LUX_PROFILE_FUNCTION_AUTO;
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
		LUX_PROFILE_FUNCTION_AUTO;
		RenderVolumeEnvironment result;
		result.PostProcess = baseSettings.PostProcess;

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
			if (weight <= 0.0f)
				continue;

			if (weight > 0.0f)
			{
				result.ActiveVolumeCount++;
				if (volume.PostProcess)
					result.ActivePostProcessVolumeCount++;
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
		}

		return result;
	}

	bool RenderVolumeEvaluator::RunSelfTests()
	{
		LUX_PROFILE_FUNCTION_AUTO;
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

		const bool success = insideBox && fadedBox && priorityBlend;
		if (!success)
			LUX_CORE_WARN_TAG("Renderer", "RenderVolumeEvaluator self tests failed: inside={}, fade={}, priority={}", insideBox, fadedBox, priorityBlend);
		return success;
	}

} // namespace Lux
