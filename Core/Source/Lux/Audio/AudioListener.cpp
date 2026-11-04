// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioListener.h"

#include <algorithm>
#include <cmath>

namespace Lux {

	namespace {

		constexpr double kMinUpLength = 1.0e-6;
		constexpr double kParallelUpThreshold = 0.9;

		bool IsFinite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}
	}

	bool AudioListener::SetTransform(AudioListenerState& listener, const glm::mat4& transform)
	{
		if (!IsFinite(glm::vec3(transform[3])) || !IsFinite(glm::vec3(transform[2])) || !IsFinite(glm::vec3(transform[1])))
			return false;

		// World-space columns preserve parent rotations. Remove scale/shear and handle collapsed
		// axes without sending NaNs or a non-orthonormal frame to either mixer.
		glm::dvec3 forward = -glm::dvec3(transform[2]);
		forward = glm::length(forward) > 0.0 ? glm::normalize(forward) : glm::dvec3(0.0, 0.0, -1.0);
		glm::dvec3 up = glm::dvec3(transform[1]);
		if (glm::length(up) > 0.0)
			up = glm::normalize(up);
		up -= forward * glm::dot(up, forward);
		if (glm::length(up) < kMinUpLength)
		{
			up = std::abs(forward.y) < kParallelUpThreshold ? glm::dvec3(0.0, 1.0, 0.0) : glm::dvec3(1.0, 0.0, 0.0);
			up -= forward * glm::dot(up, forward);
		}
		listener.Position = glm::vec3(transform[3]);
		listener.Forward = glm::vec3(forward);
		listener.Up = glm::vec3(glm::normalize(up));
		return true;
	}

	int AudioListener::GetPrimaryIndex(const States& listeners)
	{
		int primary = -1;
		for (int index = 0; index < MaxListeners; ++index)
		{
			if (std::isfinite(listeners[index].Weight) && listeners[index].Weight > 0.0f &&
				(primary < 0 || listeners[index].Weight > listeners[primary].Weight))
				primary = index;
		}
		return primary;
	}
}
