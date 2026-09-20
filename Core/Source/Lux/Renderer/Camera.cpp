// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Camera.h"

namespace Lux {
	Camera::Camera(const glm::mat4& projection, const glm::mat4& unReversedProjection)
		: m_ProjectionMatrix(projection), m_UnReversedProjectionMatrix(unReversedProjection)
	{
	}

	Camera::Camera(const float degFov, const float width, const float height, const float nearP, const float farP)
		: m_ProjectionMatrix(glm::perspectiveFov(glm::radians(degFov), width, height, farP, nearP)), m_UnReversedProjectionMatrix(glm::perspectiveFov(glm::radians(degFov), width, height, nearP, farP))
	{
	}
}
