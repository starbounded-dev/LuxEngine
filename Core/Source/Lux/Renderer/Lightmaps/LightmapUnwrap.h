// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Lux {

	// A non-overlapping lightmap parameterisation of one submesh, from xatlas (MIT, Core/vendor/xatlas). Charts are cut where the surface
	// needs it, so vertices are split: SourceVertex[i] is the submesh-local vertex the i-th output vertex copies, UV[i] its lightmap UV in
	// 0..1 over the atlas, and Indices the triangles over the output vertices (same triangle order as the input).
	struct LightmapUnwrapResult
	{
		bool Success = false;
		std::vector<uint32_t> SourceVertex;
		std::vector<glm::vec2> UV;
		std::vector<uint32_t> Indices;
		uint32_t ChartCount = 0;
		uint32_t AtlasSize = 0;
		float Utilization = 0.0f; // covered fraction of the atlas
	};

	// resolution: the atlas side in texels the charts are packed into; padding: gutter between charts in texels.
	LightmapUnwrapResult UnwrapSubmeshForLightmap(const MeshSource& source, const Submesh& submesh, uint32_t resolution = 256, uint32_t padding = 2);

}
