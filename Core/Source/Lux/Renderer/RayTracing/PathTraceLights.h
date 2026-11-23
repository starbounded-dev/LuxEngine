// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/SceneRenderer.h"

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

namespace Lux {

	// 4 vec4 per light, mirrored by DirectLight in PathTrace.glsl:
	//   directional: A = (toward light, 0)  B = (radiance * intensity)
	//   point:       A = (position, 1)      B = (radiance * intensity, range)
	//   spot:        A = (position, 2)      B = (radiance * intensity, range)   C = (direction, cos half angle)
	//   rect area:   A = (centre, 3)        B = (radiance * intensity)          C = half right   D = half up
inline std::vector<glm::vec4> PackLights(const LightEnvironment& environment)
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
			// Rectangles only in v1; other shapes need their own sampling.
			if (light.Intensity > 0.0f && light.Shape == 0)
				add(glm::vec4(light.Position, 3.0f), glm::vec4(light.Radiance * light.Intensity, 0.0f), glm::vec4(light.HalfRight, 0.0f), glm::vec4(light.HalfUp, 0.0f));
		}
		return packed;
	}


}
