// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "soundwave/GeometryView.h"
#include <array>
#include <cstddef>
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
	// SoundwaveReverb.glsl's request block and per-ray output (std430).
	struct alignas(16) FlatReverbRequest
	{
		float Listener[4];
		std::uint32_t Config[4]; // rays, bounces, root 0, root 1
		float Params[4];         // x = absorption fraction
		float Materials[kMaterialTagCount][4]; // absorption LF, absorption HF, scattering
	};
	struct FlatReverbRay
	{
		float Absorbed, Travelled;
		std::uint32_t Hits, Traces; // Traces bit 31 = escaped
	};
	// SoundwaveVisibility.glsl's independent segment tests. The request points at the same
	// flattened geometry as the reverb shader. Result values: 0 clear, 1 blocked, 2 invalid.
	struct alignas(16) FlatVisibilityRequest
	{
		std::uint32_t Roots[2], SegmentCount, PortalCount;
	};
	struct alignas(16) FlatVisibilitySegment
	{
		float From[4], To[4];
		std::uint32_t IgnoredInstance[2], MarginBits, Pad;
	};
	struct alignas(16) FlatPortal
	{
		float WorldToBox[16];
		std::uint32_t Instance[2], Active, Pad;
	};
	static_assert(sizeof(FlatNode) == 48 && sizeof(FlatTriangle) == 64);
	static_assert(sizeof(FlatReverbRequest) == 48 + 16 * kMaterialTagCount && sizeof(FlatReverbRay) == 16);
	static_assert(sizeof(FlatVisibilityRequest) == 16 && sizeof(FlatVisibilitySegment) == 48 && sizeof(FlatPortal) == 80);

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
	enum class FlatError { None, InvalidGeometry, BudgetExceeded, AllocationFailed };
	// The byte budget covers all three upload arrays. On failure output retains its last valid value.
	[[nodiscard]] FlatError Flatten(const GeometryView& geometry, FlatGeometry& output, std::size_t maxBytes);
}
