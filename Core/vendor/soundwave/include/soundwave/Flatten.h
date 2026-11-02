// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "soundwave/GeometryView.h"
#include <array>
#include <vector>

namespace Lux::Soundwave
{
	// std430 records the compute kernel reads unchanged (Soundwave.glslh mirrors them).
	struct alignas(16) FlatNode
	{
		float Min[3]; std::uint32_t First;
		float Max[3]; std::uint32_t Count;
		std::uint32_t Left, Right, Pad0, Pad1;
	};
	struct alignas(16) FlatTriangle
	{
		float A[3]; std::uint32_t Material;
		float B[3]; std::uint32_t Flags; // bit 0 active (not excluded), bit 1 portal
		float C[3]; std::uint32_t Pad;
		std::uint32_t InstanceLow, InstanceHigh, Pad0, Pad1;
	};
	static_assert(sizeof(FlatNode) == 48 && sizeof(FlatTriangle) == 64);

	inline constexpr std::uint32_t kFlatActive = 1, kFlatPortal = 2;

	// One world-space copy of a view: both layers' nodes, triangle order and transformed
	// triangles at the view's own virtual indices, with exclusions baked into Flags.
	// shortcut: instanced meshes are expanded (about 64 B per logical triangle); move to a
	// two-level GPU traversal when a scene's flattened size exceeds its upload budget.
	struct FlatGeometry
	{
		std::vector<FlatNode> Nodes;
		std::vector<std::uint32_t> Order;
		std::vector<FlatTriangle> Triangles;
		std::array<std::uint32_t, 2> Roots{ kInvalidGeometryIndex, kInvalidGeometryIndex };
	};
	[[nodiscard]] bool Flatten(const GeometryView& geometry, FlatGeometry& output);
}
