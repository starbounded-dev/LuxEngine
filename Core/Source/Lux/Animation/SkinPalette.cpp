// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SkinPalette.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Lux
{
	std::vector<glm::mat4> BuildModelPose(const Skeleton& skeleton, uint32_t bendBone, float bendDegrees)
	{
		const uint32_t count = skeleton.GetNumBones();
		std::vector<glm::mat4> pose(count);
		for (uint32_t i = 0; i < count; i++)
		{
			glm::mat4 local = glm::translate(glm::mat4(1.0f), skeleton.GetBoneTranslations()[i])
				* glm::mat4_cast(skeleton.GetBoneRotations()[i])
				* glm::scale(glm::mat4(1.0f), skeleton.GetBoneScales()[i]);
			if (i == bendBone && bendDegrees != 0.0f)
				local = local * glm::rotate(glm::mat4(1.0f), glm::radians(bendDegrees), glm::vec3(1.0f, 0.0f, 0.0f));

			const uint32_t parent = skeleton.GetParentBoneIndex(i);
			pose[i] = parent == Skeleton::NullIndex ? local : pose[parent] * local;
		}
		return pose;
	}

	std::vector<glm::mat4> BuildSkinPalette(const Skeleton& skeleton, const std::vector<glm::mat4>& modelPose,
		const std::vector<BoneInfo>& meshBones, uint32_t* missingBones)
	{
		std::vector<glm::mat4> palette(meshBones.size(), glm::mat4(1.0f));
		uint32_t missing = 0;
		for (size_t i = 0; i < meshBones.size(); i++)
		{
			const uint32_t bone = skeleton.GetBoneIndex(meshBones[i].Name);
			if (bone == Skeleton::NullIndex || bone >= modelPose.size())
			{
				missing++;
				continue;
			}
			palette[i] = modelPose[bone] * meshBones[i].InverseBindPose;
		}
		if (missingBones)
			*missingBones = missing;
		return palette;
	}
}
