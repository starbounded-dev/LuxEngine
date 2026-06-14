#pragma once

#include "Atmosphere.h"

#include "Lux/Core/UUID.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace Lux {

	struct Frustum;

	static constexpr uint32_t MaxVisibleLocalFogVolumes = 64;

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

	enum RenderVolumeSkyOverride : uint64_t
	{
		SkyOverride_None = 0,
		SkyOverride_Enabled = 1ull << 0,
		SkyOverride_PlanetRadius = 1ull << 1,
		SkyOverride_AtmosphereHeight = 1ull << 2,
		SkyOverride_RayleighScaleHeight = 1ull << 3,
		SkyOverride_MieScaleHeight = 1ull << 4,
		SkyOverride_RayleighScattering = 1ull << 5,
		SkyOverride_RayleighScatteringScale = 1ull << 6,
		SkyOverride_MieScattering = 1ull << 7,
		SkyOverride_MieScatteringScale = 1ull << 8,
		SkyOverride_MieAbsorption = 1ull << 9,
		SkyOverride_MieAnisotropy = 1ull << 10,
		SkyOverride_Absorption = 1ull << 11,
		SkyOverride_AbsorptionScale = 1ull << 12,
		SkyOverride_GroundAlbedo = 1ull << 13,
		SkyOverride_GroundContribution = 1ull << 14,
		SkyOverride_SunIntensity = 1ull << 15,
		SkyOverride_SunAngularRadius = 1ull << 16,
		SkyOverride_MultiScattering = 1ull << 17,
		SkyOverride_AerialPerspectiveViewDistanceScale = 1ull << 18
	};

	enum RenderVolumeCloudOverride : uint64_t
	{
		CloudOverride_None = 0,
		CloudOverride_Enabled = 1ull << 0,
		CloudOverride_Coverage = 1ull << 1,
		CloudOverride_Density = 1ull << 2,
		CloudOverride_Altitude = 1ull << 3,
		CloudOverride_Thickness = 1ull << 4,
		CloudOverride_WindDirection = 1ull << 5,
		CloudOverride_WindSpeed = 1ull << 6,
		CloudOverride_ShapeScale = 1ull << 7,
		CloudOverride_DetailScale = 1ull << 8,
		CloudOverride_DetailStrength = 1ull << 9,
		CloudOverride_Absorption = 1ull << 10,
		CloudOverride_SilverIntensity = 1ull << 11,
		CloudOverride_Albedo = 1ull << 12,
		CloudOverride_AmbientBoost = 1ull << 13,
		CloudOverride_MaxTraceDistance = 1ull << 14,
		CloudOverride_DistanceFade = 1ull << 15,
		CloudOverride_LODStartDistance = 1ull << 16,
		CloudOverride_ShadowTraceDistance = 1ull << 17,
		CloudOverride_MarchSteps = 1ull << 18,
		CloudOverride_ShadowSteps = 1ull << 19,
		CloudOverride_RenderScale = 1ull << 20
	};

	enum RenderVolumeFogOverride : uint64_t
	{
		FogOverride_None = 0,
		FogOverride_Enabled = 1ull << 0,
		FogOverride_FogColor = 1ull << 1,
		FogOverride_FogDensity = 1ull << 2,
		FogOverride_FogHeightFalloff = 1ull << 3,
		FogOverride_StartDistance = 1ull << 4,
		FogOverride_MaxOpacity = 1ull << 5,
		FogOverride_CutoffDistance = 1ull << 6,
		FogOverride_DirectionalInscatteringColor = 1ull << 7,
		FogOverride_DirectionalInscatteringExponent = 1ull << 8,
		FogOverride_DirectionalInscatteringStartDistance = 1ull << 9,
		FogOverride_VolumetricFog = 1ull << 10,
		FogOverride_VolumetricScatteringIntensity = 1ull << 11,
		FogOverride_Anisotropy = 1ull << 12,
		FogOverride_VolumetricFogSteps = 1ull << 13
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

	struct AtmosphereVolumeComponent
	{
		uint64_t SkyOverrideMask = SkyOverride_None;
		uint64_t CloudOverrideMask = CloudOverride_None;
		uint64_t FogOverrideMask = FogOverride_None;
		AtmosphereEnvironment Settings;
	};

	struct LocalFogVolumeComponent
	{
		glm::vec3 Color = { 0.52f, 0.62f, 0.72f };
		float Density = 0.015f;
		float Anisotropy = 0.2f;
		float HeightFalloff = 0.0f;
		float NoiseScale = 0.025f;
		float NoiseStrength = 0.0f;
		float MaxDistance = 120.0f;
		float Falloff = 1.0f;
	};

	struct LocalFogVolumeGPUData
	{
		glm::mat4 WorldToLocal = glm::mat4(1.0f);
		glm::vec4 ColorDensity = { 0.52f, 0.62f, 0.72f, 0.0f };
		glm::vec4 Params0 = { 0.2f, 0.0f, 0.025f, 0.0f }; // anisotropy, height falloff, noise scale, noise strength
		glm::vec4 Params1 = { 120.0f, 1.0f, 0.0f, 0.0f }; // max distance, edge falloff, reserved, reserved
		glm::uvec4 Metadata = { 0u, 0u, 0u, 0u };         // shape, entity low bits, priority, reserved
	};

	struct RenderVolumeBaseSettings
	{
		RenderVolumePostProcessSettings PostProcess;
		AtmosphereEnvironment Atmosphere;
	};

	struct RenderVolumeEnvironment
	{
		RenderVolumePostProcessSettings PostProcess;
		AtmosphereEnvironment Atmosphere;
		std::array<LocalFogVolumeGPUData, MaxVisibleLocalFogVolumes> LocalFogVolumes{};
		uint32_t LocalFogVolumeCount = 0;
		uint32_t ActiveVolumeCount = 0;
		uint32_t ActivePostProcessVolumeCount = 0;
		uint32_t ActiveAtmosphereVolumeCount = 0;
		uint32_t ActiveLocalFogVolumeCount = 0;
		uint32_t CulledLocalFogVolumeCount = 0;
		uint32_t DroppedLocalFogVolumeCount = 0;
		float SelectedVolumeInfluence = 0.0f;
	};

	struct RenderVolumeEvaluationInput
	{
		UUID EntityID = 0;
		glm::mat4 WorldTransform = glm::mat4(1.0f);
		RenderVolumeComponent Volume;
		const PostProcessVolumeComponent* PostProcess = nullptr;
		const AtmosphereVolumeComponent* Atmosphere = nullptr;
		const LocalFogVolumeComponent* LocalFog = nullptr;
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
