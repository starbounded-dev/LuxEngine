// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/SceneRenderer.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Lux {

	// 4 vec4 per light, mirrored by DirectLight in PathTrace.glsl:
	//   directional: A = (toward light, 0)  B = (radiance * intensity)
	//   point:       A = (position, 1)      B = (radiance * intensity, range)
	//   spot:        A = (position, 2)      B = (radiance * intensity, range)   C = (direction, cos half angle)
	//   area:        A = (centre, 3 + shape: rect 3, disk 4, sphere 5, tube 6)        B = (radiance * intensity)   C = half right   D = half up
	// D.w of every light holds the running CDF of the light-choice probabilities (the last one is exactly 1), so DirectLight picks
	// a light with probability pmf_i = cdf_i - cdf_(i-1) and divides by it. powerSampling weights the choice by each light's expected
	// irradiance (an importance heuristic: still unbiased, far less noise when light powers differ); off, every light is equally likely.
	inline std::vector<glm::vec4> PackLights(const LightEnvironment& environment, bool powerSampling = true)
	{
		std::vector<glm::vec4> packed;
		auto add = [&](const glm::vec4& a, const glm::vec4& b, const glm::vec4& c, const glm::vec4& d)
		{
			packed.push_back(a);
			packed.push_back(b);
			packed.push_back(c);
			packed.push_back(d);
		};

		for (const DirectionalLight& light : environment.DirectionalLights)
		{
			if (light.Intensity > 0.0f)
				add(glm::vec4(glm::normalize(light.Direction), 0.0f), glm::vec4(light.Radiance * light.Intensity, 0.0f), glm::vec4(0.0f), glm::vec4(0.0f));
		}
		for (const PointLight& light : environment.PointLights)
		{
			if (light.Intensity > 0.0f)
				add(glm::vec4(light.Position, 1.0f), glm::vec4(light.Radiance * light.Intensity, light.Radius), glm::vec4(0.0f), glm::vec4(0.0f));
		}
		for (const SpotLight& light : environment.SpotLights)
		{
			if (light.Intensity > 0.0f)
				add(glm::vec4(light.Position, 2.0f), glm::vec4(light.Radiance * light.Intensity, light.Range),
					glm::vec4(glm::normalize(light.Direction), std::cos(glm::radians(light.Angle))), glm::vec4(0.0f));
		}
		for (const AreaLight& light : environment.AreaLights)
		{
			// Type code 3 + shape (rect 3, disk 4, sphere 5, tube 6); the half vectors mean what AreaLight documents.
			if (light.Intensity > 0.0f)
				add(glm::vec4(light.Position, 3.0f + static_cast<float>(light.Shape)), glm::vec4(light.Radiance * light.Intensity, 0.0f), glm::vec4(light.HalfRight, 0.0f), glm::vec4(light.HalfUp, 0.0f));
		}

		const size_t count = packed.size() / 4;
		if (count > 0)
		{
			// Expected irradiance at a reference distance of 4 m: directional lum, point / spot lum / d^2, area lum * area / d^2.
			auto luminance = [](const glm::vec4& v) { return 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z; };
			constexpr float referenceDistance2 = 16.0f;
			std::vector<float> weights(count, 1.0f);
			float total = 0.0f;
			for (size_t i = 0; i < count; i++)
			{
				const int type = static_cast<int>(packed[4 * i].w + 0.5f);
				const float lum = std::max(luminance(packed[4 * i + 1]), 0.0f);
				float area = 1.0f;
				if (type >= 3)
				{
					const glm::vec3 c = glm::vec3(packed[4 * i + 2]), d = glm::vec3(packed[4 * i + 3]);
					const float crossLength = glm::length(glm::cross(c, d));
					if (type == 3)
						area = 4.0f * crossLength;
					else if (type == 4)
						area = 3.14159265f * crossLength;
					else if (type == 5)
						area = 4.0f * 3.14159265f * glm::dot(c, c);
					else
						area = 4.0f * 3.14159265f * glm::length(c) * glm::length(d);
				}
				const float power = type == 0 ? lum : lum * area / referenceDistance2;
				weights[i] = powerSampling ? std::max(power, 0.0f) : 1.0f;
				total += weights[i];
			}
			// Mixed with a uniform floor so no light has zero probability (unbiasedness needs p > 0 wherever it contributes).
			float cdf = 0.0f;
			for (size_t i = 0; i < count; i++)
			{
				const float pmf = powerSampling && total > 0.0f ? 0.9f * weights[i] / total + 0.1f / static_cast<float>(count) : 1.0f / static_cast<float>(count);
				cdf += pmf;
				packed[4 * i + 3].w = i + 1 == count ? 1.0f : cdf;
			}
		}
		return packed;
	}


	template<typename Push>
	inline void ApplyPathTraceEnvironment(Push& push, const PathTraceEnvParams& environment)
	{
		push.Counts.y |= environment.Flags;
		push.SkyRadiance.a = environment.Intensity;
		push.GroundRadiance.a = environment.Rotation;
	}

}
