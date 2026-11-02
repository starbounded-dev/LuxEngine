// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioGeometrySystem.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace Lux
{
	bool AudioGeometryInput::operator==(const AudioGeometryInput& other) const
	{
		// Stable invalid authoring values must not flood the queue and log every frame.
		const auto equal = [](float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); };
		if (Entity != other.Entity || Portal != other.Portal || Mode != other.Mode || Material != other.Material ||
			Mesh != other.Mesh || Submesh != other.Submesh || !equal(Open, other.Open))
			return false;
		for (int axis = 0; axis < 3; ++axis)
			if (!equal(HalfExtents[axis], other.HalfExtents[axis]))
				return false;
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				if (!equal(Transform[c][r], other.Transform[c][r]))
					return false;
		return true;
	}

	bool AudioGeometrySystem::Validate(const AudioGeometryInput& input)
	{
		if (input.Entity == 0 || input.Mode > AcousticGeometryMode::Disabled || !IsValidAcousticMaterial(input.Material))
			return false;
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				if (!std::isfinite(input.Transform[c][r]))
					return false;
		const double determinant = glm::determinant(glm::dmat3(input.Transform));
		if (input.Transform[0].w != 0 || input.Transform[1].w != 0 || input.Transform[2].w != 0 || input.Transform[3].w != 1 ||
			!std::isfinite(determinant) || determinant == 0)
			return false;
		return !input.Portal || (std::isfinite(input.Open) && input.Open >= 0 && input.Open <= 1 &&
			std::isfinite(input.HalfExtents.x) && std::isfinite(input.HalfExtents.y) && std::isfinite(input.HalfExtents.z) &&
			glm::all(glm::greaterThan(input.HalfExtents, glm::vec3(0))));
	}

	glm::mat4 AudioGeometrySystem::PortalTransform(const AudioGeometryInput& input)
	{
		// A shutter retracts toward local -X; Open=1 removes the primitive entirely.
		return glm::scale(glm::translate(input.Transform, glm::vec3(-input.HalfExtents.x * input.Open, 0, 0)),
			input.HalfExtents * glm::vec3(1.0f - input.Open, 1, 1));
	}

	void AudioGeometrySystem::BuildPortal(AcousticGeometry& geometry)
	{
		constexpr int indices[] = { 0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2,
			1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3 };
		geometry.Triangles.reserve(36);
		for (int i : indices)
			geometry.Triangles.emplace_back(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1);
	}
}
