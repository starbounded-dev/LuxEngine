#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace Lux {

	struct SkyAtmosphereSettings
	{
		bool Enabled = true;
		float PlanetRadius = 6360.0f;
		float AtmosphereHeight = 100.0f;
		float RayleighScaleHeight = 8.0f;
		float MieScaleHeight = 1.2f;
		glm::vec3 RayleighScattering = { 5.802f, 13.558f, 33.100f };
		float RayleighScatteringScale = 0.0025f;
		glm::vec3 MieScattering = { 3.996f, 3.996f, 3.996f };
		float MieScatteringScale = 0.0015f;
		glm::vec3 MieAbsorption = { 4.400f, 4.400f, 4.400f };
		float MieAnisotropy = 0.76f;
		glm::vec3 Absorption = { 0.650f, 1.881f, 0.085f };
		float AbsorptionScale = 0.00065f;
		glm::vec3 GroundAlbedo = { 0.18f, 0.18f, 0.18f };
		float GroundContribution = 0.15f;
		float SunIntensity = 20.0f;
		float SunAngularRadius = 0.00935f;
		float MultiScattering = 0.35f;
		float AerialPerspectiveViewDistanceScale = 1.0f;
	};

	// Atmospheric tier a cloud layer occupies. Drives the combined raymarch shell.
	enum class CloudTier : uint32_t
	{
		Low = 0,  // surface .. ~2 km
		Mid = 1,  // ~2 .. 7 km
		High = 2  // ~5 .. 13 km
	};

	// Real meteorological cloud genera. Selecting one applies a preset to a layer.
	enum class CloudType : uint32_t
	{
		Stratus = 0,    // low flat overcast
		Stratocumulus,  // low rolling sheets
		Cumulus,        // low fair-weather heaps
		Cumulonimbus,   // low towering storm cells (anvil)
		Altocumulus,    // mid rolling patches
		Altostratus,    // mid grey veil
		Cirrus,         // high wispy streaks
		Cirrocumulus,   // high ripples
		Cirrostratus    // high thin veil
	};

	// One configurable cloud tier. Three of these (low/mid/high) build the sky.
	struct CloudLayerSettings
	{
		bool Enabled = true;
		CloudType Type = CloudType::Cumulus;
		CloudTier Tier = CloudTier::Low;
		float BottomAltitude = 1200.0f; // metres above the ground
		float Thickness = 1500.0f;      // metres
		float Coverage = 0.5f;          // [0,1] layer coverage multiplier
		float CloudShape = 0.85f;       // [0,1] density-height morph (flat -> towering)
		float DensityScale = 1.0f;
		float ShapeScale = 1.0f;        // base-noise frequency multiplier
		float DetailScale = 1.0f;       // detail-noise frequency multiplier
		float DetailStrength = 0.5f;
		float WindSpeedScale = 1.0f;
		float WindAngleOffset = 0.0f;   // radians, relative to the global wind
		float AnvilBias = 0.0f;         // [0,1] cumulonimbus anvil spread
		float ErosionStrength = 0.5f;
		float CoverageBias = 0.0f;      // [-1,1] additive coverage offset
		float HeightSkew = 0.0f;
	};

	// Applies the canonical altitude / shape / density preset for a cloud genus.
	// Keeps the layer's Tier; callers may override individual fields afterwards.
	inline void ApplyCloudTypePreset(CloudLayerSettings& layer, CloudType type)
	{
		layer.Type = type;
		layer.AnvilBias = 0.0f;
		switch (type)
		{
		case CloudType::Stratus:
			layer.Tier = CloudTier::Low;  layer.BottomAltitude = 600.0f;  layer.Thickness = 500.0f;
			layer.Coverage = 0.7f; layer.CloudShape = 0.05f; layer.DensityScale = 0.8f;
			layer.ShapeScale = 0.7f; layer.DetailScale = 1.0f; layer.DetailStrength = 0.35f; break;
		case CloudType::Stratocumulus:
			layer.Tier = CloudTier::Low;  layer.BottomAltitude = 900.0f;  layer.Thickness = 900.0f;
			layer.Coverage = 0.55f; layer.CloudShape = 0.4f; layer.DensityScale = 1.0f;
			layer.ShapeScale = 1.0f; layer.DetailScale = 1.0f; layer.DetailStrength = 0.5f; break;
		case CloudType::Cumulus:
			layer.Tier = CloudTier::Low;  layer.BottomAltitude = 1200.0f; layer.Thickness = 1600.0f;
			layer.Coverage = 0.45f; layer.CloudShape = 0.85f; layer.DensityScale = 1.1f;
			layer.ShapeScale = 1.0f; layer.DetailScale = 1.2f; layer.DetailStrength = 0.6f; break;
		case CloudType::Cumulonimbus:
			layer.Tier = CloudTier::Low;  layer.BottomAltitude = 1000.0f; layer.Thickness = 5000.0f;
			layer.Coverage = 0.5f; layer.CloudShape = 1.0f; layer.DensityScale = 1.3f;
			layer.ShapeScale = 0.9f; layer.DetailScale = 1.1f; layer.DetailStrength = 0.7f;
			layer.AnvilBias = 0.8f; break;
		case CloudType::Altocumulus:
			layer.Tier = CloudTier::Mid;  layer.BottomAltitude = 4000.0f; layer.Thickness = 700.0f;
			layer.Coverage = 0.45f; layer.CloudShape = 0.45f; layer.DensityScale = 0.8f;
			layer.ShapeScale = 1.6f; layer.DetailScale = 1.6f; layer.DetailStrength = 0.5f; break;
		case CloudType::Altostratus:
			layer.Tier = CloudTier::Mid;  layer.BottomAltitude = 4500.0f; layer.Thickness = 900.0f;
			layer.Coverage = 0.7f; layer.CloudShape = 0.1f; layer.DensityScale = 0.55f;
			layer.ShapeScale = 1.2f; layer.DetailScale = 1.3f; layer.DetailStrength = 0.3f; break;
		case CloudType::Cirrus:
			layer.Tier = CloudTier::High; layer.BottomAltitude = 8000.0f; layer.Thickness = 700.0f;
			layer.Coverage = 0.4f; layer.CloudShape = 0.2f; layer.DensityScale = 0.35f;
			layer.ShapeScale = 2.4f; layer.DetailScale = 3.0f; layer.DetailStrength = 0.8f; break;
		case CloudType::Cirrocumulus:
			layer.Tier = CloudTier::High; layer.BottomAltitude = 8200.0f; layer.Thickness = 500.0f;
			layer.Coverage = 0.45f; layer.CloudShape = 0.35f; layer.DensityScale = 0.4f;
			layer.ShapeScale = 3.0f; layer.DetailScale = 3.5f; layer.DetailStrength = 0.7f; break;
		case CloudType::Cirrostratus:
			layer.Tier = CloudTier::High; layer.BottomAltitude = 8500.0f; layer.Thickness = 800.0f;
			layer.Coverage = 0.6f; layer.CloudShape = 0.08f; layer.DensityScale = 0.3f;
			layer.ShapeScale = 1.8f; layer.DetailScale = 2.4f; layer.DetailStrength = 0.4f; break;
		}
	}

	struct VolumetricCloudSettings
	{
		bool Enabled = false;

		// Global controls (shared by all tiers).
		float Coverage = 0.6f; // global coverage multiplier
		float Density = 1.3f;  // global density multiplier
		glm::vec2 WindDirection = { 1.0f, 0.15f };
		float WindSpeed = 15.0f;
		float WeatherScale = 0.00004f; // 1/m, large-scale weather frequency

		// Lighting.
		glm::vec3 Albedo = { 1.0f, 0.98f, 0.92f };
		float AmbientBoost = 0.1f;        // sky ambient contribution (kept low: clouds go blue/washed if too high)
		float Extinction = 0.1f;          // scattering/extinction coefficient (per metre)
		float ScatterMultiplier = 1.0f;
		float SilverIntensity = 0.4f;
		float PowderStrength = 1.0f;
		float PhaseG0 = 0.62f;            // forward HG lobe
		float PhaseG1 = -0.15f;           // back HG lobe
		float PhaseBlend = 0.5f;
		float MultiScatter = 0.5f;        // [0,1] multiple-scattering octave amount
		float AerialPerspective = 0.5f;   // [0,1] how strongly distant clouds tint toward the sky

		// Render budget.
		float MaxTraceDistance = 9000.0f;
		float DistanceFade = 3000.0f;
		float LODStartDistance = 3000.0f;
		float ShadowTraceDistance = 3000.0f;
		uint32_t MarchSteps = 48;
		uint32_t ShadowSteps = 6;  // sun cone-march steps
		uint32_t RenderScale = 2;  // 1 = full, 2 = half, 4 = quarter

		std::array<CloudLayerSettings, 3> Layers;

		VolumetricCloudSettings()
		{
			Layers[0].Tier = CloudTier::Low;
			ApplyCloudTypePreset(Layers[0], CloudType::Cumulus);
			Layers[1].Tier = CloudTier::Mid;
			ApplyCloudTypePreset(Layers[1], CloudType::Altocumulus);
			Layers[2].Tier = CloudTier::High;
			ApplyCloudTypePreset(Layers[2], CloudType::Cirrus);
			Layers[2].Enabled = true;
		}
	};

	struct ExponentialHeightFogSettings
	{
		bool Enabled = false;
		glm::vec3 FogColor = { 0.52f, 0.62f, 0.72f };
		float FogDensity = 0.015f;
		float FogHeightFalloff = 0.12f;
		float StartDistance = 0.0f;
		float MaxOpacity = 0.85f;
		float CutoffDistance = 10000.0f;
		glm::vec3 DirectionalInscatteringColor = { 1.0f, 0.88f, 0.65f };
		float DirectionalInscatteringExponent = 8.0f;
		float DirectionalInscatteringStartDistance = 50.0f;
		bool VolumetricFog = false;
		float VolumetricScatteringIntensity = 1.0f;
		float Anisotropy = 0.2f;
		uint32_t VolumetricFogSteps = 16;
	};

	struct AtmosphereEnvironment
	{
		SkyAtmosphereSettings SkyAtmosphere;
		VolumetricCloudSettings VolumetricClouds;
		ExponentialHeightFogSettings HeightFog;
	};

} // namespace Lux
