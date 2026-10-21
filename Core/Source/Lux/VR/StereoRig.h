// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Lux {

	struct LuxOpticConfig;
	struct SceneRendererCamera;

	// The views of a 3DoF headset (docs/VR_PLAN.md). The scene camera is the body: its position and
	// heading carry the player, and the tracked head rotates on top. The camera's pitch and roll are
	// dropped, so only the user's own head ever tilts the horizon (VR_SUPPORT.md: world = playerYaw * head).
	namespace StereoRig {

		enum class Eye { Center, Left, Right };

		// The heading of `rotation` (the direction its -Z forward points, seen from above) as a rotation
		// about +Y alone. Looking straight up or down, the heading comes from the up vector instead.
		glm::quat ExtractYaw(const glm::quat& rotation);
		// Tracker-style angles in degrees: yaw about +Y (positive = left), pitch about +X (positive =
		// up), roll about the view axis.
		glm::vec3 ToYawPitchRollDegrees(const glm::quat& rotation);

		// World transform of the head: the camera's position and heading, then the head rotation.
		glm::mat4 ComputeHeadTransform(const glm::mat4& cameraWorld, const glm::quat& head);

		// Points `camera` (a frame packet's camera) at one eye: the head offset by half the IPD along
		// its own X (no offset for Center), with a symmetric reverse-Z projection of config.FOV
		// (vertical) fitted to `targetSize`. Leaves the camera's other settings, such as exposure, alone.
		void ApplyEyeCamera(SceneRendererCamera& camera, const glm::mat4& headWorld, Eye eye, const LuxOpticConfig& config, const glm::uvec2& targetSize);

	}

}
