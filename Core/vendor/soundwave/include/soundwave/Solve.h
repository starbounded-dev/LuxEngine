// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "soundwave/Grid.h"
#include "soundwave/Settings.h"
#include <array>

namespace Lux::Soundwave
{
	using MaterialTable = std::array<MaterialProperties, kMaterialTagCount>;
	MaterialTable DefaultMaterials();

	// A shutter's applied world-to-unit-box transform, column major. Retain the exact
	// affine inverse supplied by the engine, including its authored opening fraction.
	// Its homogeneous W may differ from 1 by float roundoff; XYZ is not divided by W.
	// This clips inside endpoints correctly, even when the segment hits no triangles.
	struct PortalBox
	{
		std::uint64_t Instance = 0;
		MaterialTag Material = 0;
		std::array<float, 16> WorldToBox{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
	};

	enum class EvaluationError : std::uint8_t
	{
		None, InvalidSettings, InvalidMaterials, InvalidRay, InvalidPortal
	};

	// Caller-owned reusable scratch. One instance per concurrent solve. World data,
	// settings and material tables remain immutable while a reader uses them.
	struct DirectScratch
	{
		struct Wall
		{
			Crossing Value;
			std::uint64_t Instance = 0;
		};
		struct Entry
		{
			std::uint64_t Instance = 0;
			MaterialTag Material = 0;
			float Start = 0;
			bool Portal = false;
		};
		std::vector<RayHit> Hits;
		std::vector<Entry> OpenSolids;
		std::vector<Wall> Walls;
		std::vector<std::uint64_t> PortalInstances;
	};

	// The direct measurement shared by the propagation solver and its comparison path.
	// Reflection/opening controls do not alter this measurement. Failed evaluation clears
	// output and returns a required error; callers must retain their last valid result.
	// A matching PortalBox replaces that portal's triangle crossings with analytic clipping.
	[[nodiscard]] EvaluationError EvaluateDirect(const Grid& grid, std::span<const PortalBox> portals,
		const MaterialTable& materials, const Settings& settings, Position listener,
		Position source, std::uint64_t sourceInstance, DirectScratch& scratch, Path& output);
}
