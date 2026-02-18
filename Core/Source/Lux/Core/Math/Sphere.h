#pragma once

#include <glm/glm.hpp>

namespace Lux {

	struct BoundingSphere
	{
		glm::vec3 Center;
		float Radius;

		BoundingSphere()
			: Center(0.0f), Radius(0.0f) {
		}

		BoundingSphere(const glm::vec3& center, float radius)
			: Center(center), Radius(radius) {
		}

		static BoundingSphere Transform(const BoundingSphere& sphere, const glm::mat4& transform)
		{
			// Transform center
			glm::vec4 transformedCenter = transform * glm::vec4(sphere.Center, 1.0f);

			// Extract scale from the matrix (use max scale for safety with non-uniform scaling)
			float scaleX = glm::length(glm::vec3(transform[0]));
			float scaleY = glm::length(glm::vec3(transform[1]));
			float scaleZ = glm::length(glm::vec3(transform[2]));
			float maxScale = glm::max(scaleX, glm::max(scaleY, scaleZ));

			return BoundingSphere(glm::vec3(transformedCenter), sphere.Radius * maxScale);
		}
	};
}
