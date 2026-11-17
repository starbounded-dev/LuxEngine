// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Animation/Skeleton.h"
#include "Lux/Renderer/Mesh.h"

#include <glm/glm.hpp>

#include <vector>

namespace Lux
{
	// Model-space transform of every skeleton bone at its rest pose. A bone's bend rotates it about its
	// own X axis by `bendDegrees` first (its children follow); `bendBone` == Skeleton::NullIndex or a
	// zero angle is the plain rest pose. Needs the skeleton's derived data.
	std::vector<glm::mat4> BuildModelPose(const Skeleton& skeleton, uint32_t bendBone = Skeleton::NullIndex, float bendDegrees = 0.0f);

	// The matrices a skinned mesh's vertices are blended with: for each of the mesh's bones (in
	// `meshBones` order, which the vertex bone indices refer to), the model-space pose of the skeleton
	// bone with that name times the mesh's inverse bind pose. At rest this is the same for every bone: the
	// transform of the node the inverse binds were authored below. A mesh bone the skeleton does not have
	// gets the identity and is counted in `missingBones`.
	std::vector<glm::mat4> BuildSkinPalette(const Skeleton& skeleton, const std::vector<glm::mat4>& modelPose,
		const std::vector<BoneInfo>& meshBones, uint32_t* missingBones = nullptr);
}
