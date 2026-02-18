#pragma once

#include <glm/glm.hpp>
#include "Sphere.h"
#include <array>

namespace Lux {

	// Potential Shadow Caster (PSC) Volume
	// Takes the 8 points of a shadow frustrum from cascade, extrudes them along the light direction, and builds a final volume for culling
	// Technique adapted from:
	// https://gamedev.net/articles/programming/general-and-gameplay-programming/shadow-caster-volumes-for-the-culling-of-potential-shadow-casters-r2330/ 
	// https://github.com/bevyengine/bevy/issues/10397
	struct PSCVolume
	{
		static constexpr int MaxPlanes = 10;  // 6 original + up to 4 extruded
		std::array<glm::vec4, MaxPlanes> Planes = {};
		int PlaneCount = 0;

		PSCVolume() = default;

		// Construct PSC volume from cascade frustum corners and light direction
		// frustumCorners: 8 world-space corners of the cascade frustum slice
		//   [0-3]: near plane (TL, TR, BR, BL)
		//   [4-7]: far plane (TL, TR, BR, BL)
		// lightDirection: normalized direction the light is pointing (toward scene)
		static PSCVolume FromCascadeFrustum(const glm::vec3 frustumCorners[8], const glm::vec3& lightDirection)
		{
			PSCVolume psc;

			// Calculate frustum center for orientation checks
			glm::vec3 frustumCenter(0.0f);
			for (int i = 0; i < 8; i++)
				frustumCenter += frustumCorners[i];
			frustumCenter /= 8.0f;

			// Define the 6 frustum planes with their corner indices (CCW winding when viewed from inside)
			// Format: {corner indices for plane, forming CCW quad}
			struct PlaneCorners { int indices[4]; };
			constexpr PlaneCorners planeDefs[6] = {
				{{0, 4, 7, 3}},  // Left plane
				{{1, 2, 6, 5}},  // Right plane
				{{3, 7, 6, 2}},  // Bottom plane
				{{0, 1, 5, 4}},  // Top plane
				{{0, 3, 2, 1}},  // Near plane
				{{4, 5, 6, 7}}   // Far plane
			};

			// Edge definitions: pairs of corner indices forming the 12 frustum edges
			constexpr int edges[12][2] = {
				{0, 1}, {1, 2}, {2, 3}, {3, 0},  // Near face
				{4, 5}, {5, 6}, {6, 7}, {7, 4},  // Far face
				{0, 4}, {1, 5}, {2, 6}, {3, 7}   // Connecting edges
			};

			// Which planes share each edge (2 planes per edge)
			// Plane indices: 0=Left, 1=Right, 2=Bottom, 3=Top, 4=Near, 5=Far
			constexpr int edgePlanes[12][2] = {
				{3, 4}, {1, 4}, {2, 4}, {0, 4},  // Near face edges
				{3, 5}, {1, 5}, {2, 5}, {0, 5},  // Far face edges
				{0, 3}, {1, 3}, {1, 2}, {0, 2}   // Connecting edges
			};

			// Compute plane equations and classify them
			glm::vec4 planeEquations[6];
			bool planeFacesLight[6];

			for (int i = 0; i < 6; i++)
			{
				const auto& def = planeDefs[i];
				glm::vec3 v0 = frustumCorners[def.indices[0]];
				glm::vec3 v1 = frustumCorners[def.indices[1]];
				glm::vec3 v2 = frustumCorners[def.indices[2]];

				glm::vec3 edge1 = v1 - v0;
				glm::vec3 edge2 = v2 - v0;
				glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

				// Ensure normal points inward (toward frustum center)
				if (glm::dot(normal, frustumCenter - v0) < 0.0f)
					normal = -normal;

				float d = -glm::dot(normal, v0);
				planeEquations[i] = glm::vec4(normal, d);

				// Plane faces light if its normal opposes the light direction
				// (light shines into the plane, so shadows can be cast from behind it)
				planeFacesLight[i] = glm::dot(normal, -lightDirection) > 0.0f;
			}

			// Add planes that face the light (these bound the shadow receiver region)
			for (int i = 0; i < 6; i++)
			{
				if (planeFacesLight[i])
				{
					psc.Planes[psc.PlaneCount++] = planeEquations[i];
				}
			}

			// Find silhouette edges (boundary between light-facing and non-light-facing planes)
			// and create extruded planes along the light direction
			for (int e = 0; e < 12; e++)
			{
				int plane1 = edgePlanes[e][0];
				int plane2 = edgePlanes[e][1];

				// Silhouette edge: one adjacent plane faces light, the other doesn't
				if (planeFacesLight[plane1] != planeFacesLight[plane2])
				{
					glm::vec3 v0 = frustumCorners[edges[e][0]];
					glm::vec3 v1 = frustumCorners[edges[e][1]];
					glm::vec3 edgeDir = v1 - v0;

					// Create plane from edge extruded along light direction
					// Normal = cross(lightDirection, edgeDirection)
					glm::vec3 normal = glm::normalize(glm::cross(lightDirection, edgeDir));

					// Ensure normal points inward (toward frustum center)
					if (glm::dot(normal, frustumCenter - v0) < 0.0f)
						normal = -normal;

					float d = -glm::dot(normal, v0);

					if (psc.PlaneCount < MaxPlanes)
						psc.Planes[psc.PlaneCount++] = glm::vec4(normal, d);
				}
			}

			return psc;
		}

		// Test if a bounding sphere is inside the PSC volume
		bool IsSphereVisible(const BoundingSphere& sphere) const
		{
			for (int i = 0; i < PlaneCount; i++)
			{
				glm::vec3 normal(Planes[i].x, Planes[i].y, Planes[i].z);
				float distance = glm::dot(normal, sphere.Center) + Planes[i].w;

				if (distance < -sphere.Radius)
					return false;
			}
			return true;
		}

		// Test if an AABB is inside the PSC volume
		bool IsBoxVisible(const AABB& box) const
		{
			for (int i = 0; i < PlaneCount; i++)
			{
				const glm::vec4& plane = Planes[i];
				glm::vec3 normal(plane.x, plane.y, plane.z);

				glm::vec3 positiveVertex(
					plane.x >= 0 ? box.Max.x : box.Min.x,
					plane.y >= 0 ? box.Max.y : box.Min.y,
					plane.z >= 0 ? box.Max.z : box.Min.z
				);

				if (glm::dot(normal, positiveVertex) + plane.w < 0)
					return false;
			}
			return true;
		}
	};

	struct Frustum
	{
		// Planes: Left, Right, Bottom, Top, Near, Far
		// Each plane stored as (nx, ny, nz, d) where nx*x + ny*y + nz*z + d = 0
		glm::vec4 Planes[6];

		Frustum() = default;

		// Extract frustum planes from a view-projection matrix
		// Planes are normalized and point inward
		static Frustum FromViewProjection(const glm::mat4& viewProj)
		{
			Frustum frustum;

			// Left plane
			frustum.Planes[0] = glm::vec4(
				viewProj[0][3] + viewProj[0][0],
				viewProj[1][3] + viewProj[1][0],
				viewProj[2][3] + viewProj[2][0],
				viewProj[3][3] + viewProj[3][0]
			);

			// Right plane
			frustum.Planes[1] = glm::vec4(
				viewProj[0][3] - viewProj[0][0],
				viewProj[1][3] - viewProj[1][0],
				viewProj[2][3] - viewProj[2][0],
				viewProj[3][3] - viewProj[3][0]
			);

			// Bottom plane
			frustum.Planes[2] = glm::vec4(
				viewProj[0][3] + viewProj[0][1],
				viewProj[1][3] + viewProj[1][1],
				viewProj[2][3] + viewProj[2][1],
				viewProj[3][3] + viewProj[3][1]
			);

			// Top plane
			frustum.Planes[3] = glm::vec4(
				viewProj[0][3] - viewProj[0][1],
				viewProj[1][3] - viewProj[1][1],
				viewProj[2][3] - viewProj[2][1],
				viewProj[3][3] - viewProj[3][1]
			);

			// Near plane (for Z: 0 to 1, z >= 0)
			// Formula: row2
			frustum.Planes[4] = glm::vec4(
				viewProj[0][2],
				viewProj[1][2],
				viewProj[2][2],
				viewProj[3][2]
			);

			// Far plane (for Z: 0 to 1, z <= w)
			// Formula: row3 - row2
			frustum.Planes[5] = glm::vec4(
				viewProj[0][3] - viewProj[0][2],
				viewProj[1][3] - viewProj[1][2],
				viewProj[2][3] - viewProj[2][2],
				viewProj[3][3] - viewProj[3][2]
			);

			// Normalize all planes
			for (int i = 0; i < 6; i++)
			{
				float length = glm::length(glm::vec3(frustum.Planes[i]));
				frustum.Planes[i] /= length;
			}

			return frustum;
		}

		// Test if an AABB is visible (intersects or is inside the frustum)
		// Returns true if the box should be rendered
		bool IsBoxVisible(const AABB& box) const
		{
			for (int i = 0; i < 6; i++)
			{
				const glm::vec4& plane = Planes[i];
				glm::vec3 normal(plane.x, plane.y, plane.z);

				// Find the positive vertex (furthest along the plane normal)
				glm::vec3 positiveVertex(
					plane.x >= 0 ? box.Max.x : box.Min.x,
					plane.y >= 0 ? box.Max.y : box.Min.y,
					plane.z >= 0 ? box.Max.z : box.Min.z
				);

				// If the positive vertex is behind the plane, the box is completely outside
				if (glm::dot(normal, positiveVertex) + plane.w < 0)
					return false;
			}
			return true;
		}

		// Test if a bounding sphere is visible (intersects or is inside the frustum)
		// Returns true if the sphere should be rendered
		bool IsSphereVisible(const BoundingSphere& sphere) const
		{
			for (int i = 0; i < 6; i++)
			{
				glm::vec3 normal(Planes[i].x, Planes[i].y, Planes[i].z);

				float distance = glm::dot(normal, sphere.Center) + Planes[i].w;

				if (distance < -sphere.Radius)
					return false;
			}
			return true;
		}
	};
}
