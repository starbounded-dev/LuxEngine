#pragma once

#include "Lux/Scene/Components.h" // LightUnit

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace Lux {

	// ─────────────────────────────────────────────────────────────────────────
	// Physical light helpers. Lights store their Intensity in a chosen LightUnit;
	// these convert to the radiometric multiplier the lighting shaders consume
	// (Radiance * Multiplier), and convert a colour temperature to a luminance-
	// preserving linear RGB tint. Pairs with the EV100 exposure model so that
	// physical light values map to plausible images. See [[Exposure.h]].
	// ─────────────────────────────────────────────────────────────────────────
	namespace PhysicalLight {

		// Correlated colour temperature (Kelvin) -> linear RGB, normalised to unit
		// luminance so it only tints a light's colour, not its brightness.
		// Tanner Helland approximation.
		inline glm::vec3 KelvinToRGB(float kelvin)
		{
			kelvin = glm::clamp(kelvin, 1000.0f, 40000.0f);
			const float temp = kelvin / 100.0f;

			float r, g, b;
			if (temp <= 66.0f)
			{
				r = 255.0f;
				g = 99.4708025861f * std::log(temp) - 161.1195681661f;
			}
			else
			{
				r = 329.698727446f * std::pow(temp - 60.0f, -0.1332047592f);
				g = 288.1221695283f * std::pow(temp - 60.0f, -0.0755148492f);
			}

			if (temp >= 66.0f)
				b = 255.0f;
			else if (temp <= 19.0f)
				b = 0.0f;
			else
				b = 138.5177312231f * std::log(temp - 10.0f) - 305.0447927307f;

			glm::vec3 srgb = glm::clamp(glm::vec3(r, g, b) / 255.0f, glm::vec3(0.0f), glm::vec3(1.0f));
			glm::vec3 linear = glm::pow(srgb, glm::vec3(2.2f));

			const float luminance = glm::dot(linear, glm::vec3(0.2126f, 0.7152f, 0.0722f));
			if (luminance > 1e-4f)
				linear /= luminance;
			return linear;
		}

		// Effective radiance for a light: its colour, optionally tinted by a Kelvin
		// temperature.
		inline glm::vec3 EffectiveRadiance(const glm::vec3& radiance, bool useColorTemperature, float kelvin)
		{
			return useColorTemperature ? radiance * KelvinToRGB(kelvin) : radiance;
		}

		// Directional light: Lux is the illuminance multiplier directly.
		inline float DirectionalIntensity(LightUnit /*unit*/, float intensity)
		{
			return intensity;
		}

		// Point light: lumens (luminous power) -> candela (luminous intensity).
		inline float PointIntensity(LightUnit unit, float intensity)
		{
			if (unit == LightUnit::Lumens)
				return intensity / (4.0f * glm::pi<float>());
			return intensity; // Unitless or Candela
		}

		// Spot light: lumens -> candela over the cone's solid angle.
		inline float SpotIntensity(LightUnit unit, float intensity, float outerAngleDegrees)
		{
			if (unit == LightUnit::Lumens)
			{
				const float halfAngle = glm::clamp(glm::radians(outerAngleDegrees) * 0.5f, 0.0f, glm::half_pi<float>());
				const float solidAngle = 2.0f * glm::pi<float>() * (1.0f - std::cos(halfAngle));
				return intensity / glm::max(solidAngle, 1e-4f);
			}
			return intensity; // Unitless or Candela
		}

	} // namespace PhysicalLight

} // namespace Lux
