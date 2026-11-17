// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Animation/Animation.h"
#include "Lux/Animation/Skeleton.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Lux
{
	// Local (parent-relative) translation, rotation and scale of every bone of a skeleton.
	struct LocalPose
	{
		std::vector<glm::vec3> Translations;
		std::vector<glm::quat> Rotations;
		std::vector<glm::vec3> Scales;
	};

	// The skeleton's rest pose as a local pose.
	LocalPose MakeRestPose(const Skeleton& skeleton);

	// `time` brought into [0, duration]: wrapped when looping, clamped otherwise.
	float WrapAnimationTime(float time, float duration, bool loop);

	// Samples `clip` at `time` into `pose` (sized for the skeleton). A bone the clip has no track for, or a
	// channel it leaves empty, keeps the value `pose` already holds, so start from MakeRestPose.
	void SampleAnimation(const Animation& clip, float time, LocalPose& pose);

	// out = lerp(a, b, weight) per channel, quaternions by shortest-path slerp. `weight` is clamped to [0, 1].
	void BlendPoses(const LocalPose& a, const LocalPose& b, float weight, LocalPose& out);

	// Model-space transform of every bone for a local pose (parents come before children in a Skeleton).
	std::vector<glm::mat4> BuildModelPose(const Skeleton& skeleton, const LocalPose& pose);

	// The horizontal (model-space X and Z) offset of `bone` at `time` relative to its offset at time 0:
	// how far the clip has carried that bone from where it started. Used for root motion.
	glm::vec3 RootMotionOffset(const Skeleton& skeleton, const Animation& clip, uint32_t bone, float time);

	// Removes that offset from `pose`, so the bone stays at its start position and the motion can be applied
	// to the entity instead.
	void RemoveRootMotion(const Skeleton& skeleton, uint32_t bone, const glm::vec3& offset, LocalPose& pose);
}
