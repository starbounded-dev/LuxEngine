#pragma once


#include "Lux/Core/UUID.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace Lux {

	struct Frustum;


	enum class RenderVolumeShape : uint32_t
	{
		Box = 0,
		Sphere = 1
	};

	// How the final scene exposure (linear HDR multiplier applied before tonemapping) is determined.
	enum class ExposureMode : uint32_t
	{
		Manual = 0,    // Exposure is used directly as a linear multiplier (legacy default).
		ManualEV = 1,  // ExposureEV100 sets the exposure value directly.
		Camera = 2,    // EV100 computed from physical Aperture / ShutterSpeed / ISO.
		Automatic = 3  // Histogram auto-exposure (see SceneRenderer auto-exposure passes).
	};

	// Tonemapping operator applied in the scene composite.
	enum class TonemapOperator : uint32_t
	{
		ACES = 0,  // ACES filmic (default)
		AgX = 1,   // AgX (neutral highlights, less hue-shift on saturated brights)
		None = 2   // No tonemap (clamp); useful for debugging
	};

	enum RenderVolumePostProcessOverride : uint64_t
	{
		PostProcessOverride_None = 0,
		PostProcessOverride_Exposure = 1ull << 0,
		PostProcessOverride_BloomEnabled = 1ull << 1,
		PostProcessOverride_BloomThreshold = 1ull << 2,
		PostProcessOverride_BloomKnee = 1ull << 3,
		PostProcessOverride_BloomUpsampleScale = 1ull << 4,
		PostProcessOverride_BloomIntensity = 1ull << 5,
		PostProcessOverride_BloomDirtIntensity = 1ull << 6,
		PostProcessOverride_DOFEnabled = 1ull << 7,
		PostProcessOverride_DOFFocusDistance = 1ull << 8,
		PostProcessOverride_DOFBlurSize = 1ull << 9,
		PostProcessOverride_ColorFilter = 1ull << 10,
		PostProcessOverride_Saturation = 1ull << 11,
		PostProcessOverride_Contrast = 1ull << 12,
		PostProcessOverride_Gamma = 1ull << 13,
		PostProcessOverride_ExposureMode = 1ull << 14,
		PostProcessOverride_Aperture = 1ull << 15,
		PostProcessOverride_ShutterSpeed = 1ull << 16,
		PostProcessOverride_ISO = 1ull << 17,
		PostProcessOverride_ExposureEV100 = 1ull << 18,
		PostProcessOverride_ExposureCompensation = 1ull << 19,
		PostProcessOverride_AutoMinEV100 = 1ull << 20,
		PostProcessOverride_AutoMaxEV100 = 1ull << 21,
		PostProcessOverride_AutoAdaptationSpeedUp = 1ull << 22,
		PostProcessOverride_AutoAdaptationSpeedDown = 1ull << 23,
		PostProcessOverride_Tonemap = 1ull << 24,
		PostProcessOverride_WhiteBalance = 1ull << 25,
		PostProcessOverride_LiftGammaGain = 1ull << 26
	};

	struct RenderVolumeComponent
	{
		bool Enabled = true;
		RenderVolumeShape Shape = RenderVolumeShape::Box;
		bool Unbound = false;
		float BlendDistance = 10.0f;
		float BlendWeight = 1.0f;
		int32_t Priority = 0;
		glm::vec4 DebugColor = { 0.25f, 0.65f, 1.0f, 1.0f };
	};

	struct RenderVolumePostProcessSettings
	{
		float Exposure = 1.0f;

		// Physical / photographic exposure (Filament-style EV100 model).
		// NOTE: Camera and Automatic modes assume scene lighting is in physical units
		// (lux / nits). With arbitrary (non-physical) light intensities, the "sunny-16"
		// Camera defaults below (f/16, 1/125s, ISO 100 ~= EV100 15) yield an exposure
		// multiplier ~2.6e-5, i.e. a near-black image. To use Camera mode with such lights,
		// raise ExposureCompensation to calibrate; otherwise prefer Automatic (adapts to the
		// rendered scene luminance via the histogram) or Manual. Manual is the default and is
		// unit-agnostic.
		ExposureMode ExposureControl = ExposureMode::Manual;
		float Aperture = 16.0f;             // f-stops (N)
		float ShutterSpeed = 1.0f / 125.0f; // seconds (t)
		float ISO = 100.0f;                 // sensor sensitivity (S)
		float ExposureEV100 = 12.0f;        // used in ManualEV mode
		float ExposureCompensation = 0.0f;  // EV offset applied in all physical modes (+ = brighter)

		// Auto-exposure (histogram) parameters; used when ExposureControl == Automatic.
		float AutoMinEV100 = -2.0f;
		float AutoMaxEV100 = 16.0f;
		float AutoAdaptationSpeedUp = 3.0f;   // EV/sec when scene gets brighter
		float AutoAdaptationSpeedDown = 1.0f; // EV/sec when scene gets darker

		bool BloomEnabled = true;
		float BloomThreshold = 1.0f;
		float BloomKnee = 0.1f;
		float BloomUpsampleScale = 1.0f;
		float BloomIntensity = 1.0f;
		float BloomDirtIntensity = 1.0f;
		bool DOFEnabled = false;
		float DOFFocusDistance = 0.0f;
		float DOFBlurSize = 1.0f;
		glm::vec3 ColorFilter = { 1.0f, 1.0f, 1.0f };
		float Saturation = 1.0f;
		float Contrast = 1.0f;
		float Gamma = 2.2f; // display gamma applied after tonemapping

		// Tonemapping + color grading.
		TonemapOperator Tonemap = TonemapOperator::ACES;
		float WhiteTemperature = 0.0f; // [-1, 1] relative (cool -> warm)
		float WhiteTint = 0.0f;        // [-1, 1] relative (magenta -> green)
		glm::vec3 Lift = { 0.0f, 0.0f, 0.0f }; // shadows (additive)
		glm::vec3 GradeGamma = { 1.0f, 1.0f, 1.0f }; // midtones (power)
		glm::vec3 Gain = { 1.0f, 1.0f, 1.0f };       // highlights (multiplicative)
	};

	struct PostProcessVolumeComponent
	{
		uint64_t OverrideMask = PostProcessOverride_None;
		RenderVolumePostProcessSettings Settings;
	};

	struct RenderVolumeBaseSettings
	{
		RenderVolumePostProcessSettings PostProcess;
	};

	struct RenderVolumeEnvironment
	{
		RenderVolumePostProcessSettings PostProcess;
		uint32_t ActiveVolumeCount = 0;
		uint32_t ActivePostProcessVolumeCount = 0;
		float SelectedVolumeInfluence = 0.0f;
	};

	struct RenderVolumeEvaluationInput
	{
		UUID EntityID = 0;
		glm::mat4 WorldTransform = glm::mat4(1.0f);
		RenderVolumeComponent Volume;
		const PostProcessVolumeComponent* PostProcess = nullptr;
		bool Selected = false;
	};

	class RenderVolumeEvaluator
	{
	public:
		static float ComputeWeight(const RenderVolumeComponent& volume, const glm::mat4& worldTransform, const glm::vec3& cameraPosition);
		static RenderVolumeEnvironment Evaluate(
			const std::vector<RenderVolumeEvaluationInput>& volumes,
			const glm::vec3& cameraPosition,
			const Frustum* viewFrustum,
			const RenderVolumeBaseSettings& baseSettings);
		static bool RunSelfTests();
		static bool HasOverride(uint64_t mask, uint64_t flag) { return (mask & flag) != 0; }
	};

} // namespace Lux
