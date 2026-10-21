// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "StereoRig.h"

#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/VR/LuxOpticConfig.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Lux::StereoRig {

	namespace {

		constexpr glm::vec3 kUp = { 0.0f, 1.0f, 0.0f };
		constexpr glm::vec3 kForward = { 0.0f, 0.0f, -1.0f };
		// Below this horizontal length (squared) the forward vector is too close to vertical to give a heading.
		constexpr float kVerticalEpsilon = 1e-6f;

		// Heading angle about +Y of a view that looks along `forward` with `up` as its up vector.
		float GetHeading(const glm::vec3& forward, const glm::vec3& up)
		{
			glm::vec2 horizontal = { forward.x, forward.z };
			if (glm::dot(horizontal, horizontal) < kVerticalEpsilon)
			{
				// Looking straight down the up vector points ahead; looking straight up it points back.
				horizontal = forward.y < 0.0f ? glm::vec2(up.x, up.z) : glm::vec2(-up.x, -up.z);
			}
			return std::atan2(-horizontal.x, -horizontal.y);
		}

	}

	glm::quat ExtractYaw(const glm::quat& rotation)
	{
		return glm::angleAxis(GetHeading(rotation * kForward, rotation * kUp), kUp);
	}

	glm::vec3 ToYawPitchRollDegrees(const glm::quat& q)
	{
		// vr/LuxTracker.h's toEuler, so the numbers match the tracker's own test program.
		const float yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.y * q.y + q.x * q.x));
		const float pitch = std::asin(glm::clamp(2.0f * (q.w * q.x - q.y * q.z), -1.0f, 1.0f));
		const float roll = std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.x * q.x + q.z * q.z));
		return glm::degrees(glm::vec3(yaw, pitch, roll));
	}

	glm::mat4 ComputeHeadTransform(const glm::mat4& cameraWorld, const glm::quat& head)
	{
		const glm::vec3 position = glm::vec3(cameraWorld[3]);
		// Columns may carry the camera entity's scale; only their directions matter here.
		const glm::vec3 up = glm::normalize(glm::vec3(cameraWorld[1]));
		const glm::vec3 forward = -glm::normalize(glm::vec3(cameraWorld[2]));
		const glm::quat body = glm::angleAxis(GetHeading(forward, up), kUp);

		return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(body * head);
	}

	void ApplyEyeCamera(SceneRendererCamera& camera, const glm::mat4& headWorld, Eye eye, const LuxOpticConfig& config, const glm::uvec2& targetSize)
	{
		const float halfIPD = 0.5f * config.IPD;
		const float offset = eye == Eye::Left ? -halfIPD : (eye == Eye::Right ? halfIPD : 0.0f);
		const glm::mat4 eyeWorld = headWorld * glm::translate(glm::mat4(1.0f), glm::vec3(offset, 0.0f, 0.0f));

		camera.ViewMatrix = glm::inverse(eyeWorld);
		camera.Camera.SetPerspectiveProjectionMatrix(glm::radians(config.FOV),
			static_cast<float>(glm::max(targetSize.x, 1u)), static_cast<float>(glm::max(targetSize.y, 1u)), config.Near, config.Far);
		camera.Near = config.Near;
		camera.Far = config.Far;
		camera.FOV = config.FOV;
	}

}
