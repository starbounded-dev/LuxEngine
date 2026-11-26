// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/SceneRenderer.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <vector>

namespace Lux {

	inline constexpr size_t k_PathTraceLightElementsPerEntry = 5;

	// Four consecutive vec4 per light, followed by one alias-table vec4 per light:
	//   directional: A = (toward light, 0)  B = (radiance * intensity)
	//   point:       A = (position, 1)      B = (radiance * intensity, range)
	//   spot:        A = (position, 2)      B = (radiance * intensity, range)   C = (direction, cos half angle)
	//   area:        A = (centre, 3 + shape: rect 3, disk 4, sphere 5, tube 6)        B = (radiance * intensity)   C = half right   D = half up
	// D.w stores the light's probability mass. In the alias table, x is the probability of keeping the selected
	// column and y is the alternative light index (as bits). powerSampling weights the choice by expected irradiance.
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
			std::vector<double> weights(count, 1.0);
			double total = 0.0;
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
			// The uniform component preserves a nonzero probability for every light. Build the
			// alias table in linear time so every shader can sample in constant time.
			const size_t aliasBase = packed.size();
			packed.resize(count * k_PathTraceLightElementsPerEntry);
			std::vector<uint32_t> underfull, overfull;
			underfull.reserve(count);
			overfull.reserve(count);
			const double uniform = 1.0 / static_cast<double>(count);
			for (uint32_t i = 0; i < count; i++)
			{
				const double pmf = powerSampling && total > 0.0 ? 0.9 * weights[i] / total + 0.1 * uniform : uniform;
				packed[4 * i + 3].w = static_cast<float>(pmf);
				packed[aliasBase + i] = glm::vec4(1.0f, std::bit_cast<float>(i), 0.0f, 0.0f);
				weights[i] = pmf * static_cast<double>(count);
				(weights[i] < 1.0 ? underfull : overfull).push_back(i);
			}
			while (!underfull.empty() && !overfull.empty())
			{
				const uint32_t slot = underfull.back();
				underfull.pop_back();
				const uint32_t replacement = overfull.back();
				overfull.pop_back();
				packed[aliasBase + slot].x = static_cast<float>(std::clamp(weights[slot], 0.0, 1.0));
				packed[aliasBase + slot].y = std::bit_cast<float>(replacement);
				weights[replacement] -= 1.0 - weights[slot];
				(weights[replacement] < 1.0 ? underfull : overfull).push_back(replacement);
			}
		}
		return packed;
	}

	inline uint32_t PathTraceLightCount(const std::vector<glm::vec4>& packed)
	{
		return static_cast<uint32_t>(packed.size() / k_PathTraceLightElementsPerEntry);
	}

	template<typename Push>
	inline void ApplyPathTraceEnvironment(Push& push, const PathTraceEnvParams& environment)
	{
		push.Counts.y |= environment.Flags;
		push.SkyRadiance.a = environment.Intensity;
		push.GroundRadiance.a = environment.Rotation;
		push.Medium = environment.FogMedium;
		push.MediumAlbedo = environment.FogAlbedo;
		push.MediumCenter = environment.FogCenter;
	}

}
