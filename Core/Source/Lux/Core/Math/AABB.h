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
	};
}
