#pragma once

#include <glm/glm.hpp>

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

	struct VolumetricCloudSettings
	{
		bool Enabled = false;
		float Coverage = 0.55f;
		float Density = 0.55f;
		float Altitude = 1500.0f;
		float Thickness = 1200.0f;
		glm::vec2 WindDirection = { 1.0f, 0.15f };
		float WindSpeed = 15.0f;
		float ShapeScale = 0.00065f;
		float DetailScale = 0.0045f;
		float DetailStrength = 0.35f;
		float Absorption = 1.2f;
		float SilverIntensity = 0.35f;
		glm::vec3 Albedo = { 1.0f, 0.98f, 0.92f };
		float AmbientBoost = 0.25f;
		float MaxTraceDistance = 12000.0f;
		float DistanceFade = 3000.0f;
		float LODStartDistance = 2500.0f;
		float ShadowTraceDistance = 4000.0f;
		uint32_t MarchSteps = 32;
		uint32_t ShadowSteps = 2;
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
		uint32_t VolumetricFogSteps = 32;
	};

	struct AtmosphereEnvironment
	{
		SkyAtmosphereSettings SkyAtmosphere;
		VolumetricCloudSettings VolumetricClouds;
		ExponentialHeightFogSettings HeightFog;
	};

} // namespace Lux
