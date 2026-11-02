// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <glm/glm.hpp>
#include "Sphere.h"

namespace Lux {

	struct AABB
	{
		glm::vec3 Min, Max;

		AABB()
			: Min(0.0f), Max(0.0f) {
		}

		AABB(const glm::vec3& min, const glm::vec3& max)
			: Min(min), Max(max) {
		}

		glm::vec3 Size() const { return Max - Min; }
		glm::vec3 Center() const { return Min + Size() * 0.5f; }

		//converts to sphere bounds from center + diagonal
		BoundingSphere ToBoundingSphere() const
		{
			glm::vec3 center = Center();
			float radius = glm::length(Size()) * 0.5f;
			return BoundingSphere(center, radius);
		}

		// The smallest axis-aligned box enclosing this box after an affine `transform`.
		AABB Transformed(const glm::mat4& transform) const
		{
			const glm::vec3 center = glm::vec3(transform * glm::vec4(Center(), 1.0f));
			const glm::vec3 halfExtent = Size() * 0.5f;
			glm::vec3 transformedHalfExtent;
			for (int axis = 0; axis < 3; axis++)
			{
				transformedHalfExtent[axis] = glm::abs(transform[0][axis]) * halfExtent.x
					+ glm::abs(transform[1][axis]) * halfExtent.y
					+ glm::abs(transform[2][axis]) * halfExtent.z;
			}
			return AABB(center - transformedHalfExtent, center + transformedHalfExtent);
		}
	};
}
