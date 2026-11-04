// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "AcousticMaterial.h"
#include "AudioGeometrySettings.h"
#include "Lux/Core/UUID.h"
#include <glm/glm.hpp>
#include <vector>

namespace Lux
{
	struct AcousticGeometry
	{
		AcousticMaterial Material = AcousticMaterial::Default;
		std::vector<glm::vec3> Triangles;
	};

	struct AudioGeometryInput
	{
		UUID Entity = 0;
		bool Portal = false;
		AcousticGeometryMode Mode = AcousticGeometryMode::Static;
		AcousticMaterial Material = AcousticMaterial::Default;
		glm::mat4 Transform{ 1.0f };
		uint64_t Mesh = 0;
		uint32_t Submesh = 0;
		glm::vec3 HalfExtents{ 0.5f, 1.0f, 0.05f };
		float Open = 0.0f;
		bool operator==(const AudioGeometryInput& other) const;
	};

	// Shared validation and shutter maths for acoustic captures and the raycast comparison.
	class AudioGeometrySystem
	{
	public:
		static bool Validate(const AudioGeometryInput& input);
		static glm::mat4 PortalTransform(const AudioGeometryInput& input);
		static void BuildPortal(AcousticGeometry& geometry);
	};
}
