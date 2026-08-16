#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Lux {

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

	// Scene-wide post-processing. Authored once per scene rather than per placed volume:
	// the spatial render-volume system (box/sphere volumes blended by proximity and
	// priority) was removed, so these values apply to the whole scene.
	//
	// Bloom, DOF and Exposure are additionally driven by the renderer's own
	// m_BloomSettings / m_DOFSettings / camera exposure and are overlaid onto a copy of
	// this struct each frame - see SceneRenderer::ResolveFrameEnvironment. Everything
	// else here (exposure mode, physical camera, auto-exposure, grading, tonemap) has no
	// other home and is authored only through this struct.
	struct PostProcessSettings
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
		glm::vec3 Lift = { 0.0f, 0.0f, 0.0f };      // shadows (additive)
		glm::vec3 GradeGamma = { 1.0f, 1.0f, 1.0f }; // midtones (power)
		glm::vec3 Gain = { 1.0f, 1.0f, 1.0f };       // highlights (multiplicative)
	};

} // namespace Lux
