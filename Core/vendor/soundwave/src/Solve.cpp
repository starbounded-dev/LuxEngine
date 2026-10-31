// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "soundwave/Solve.h"
#include "GeometryMath.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Lux::Soundwave
{
	namespace
	{
		// The engine comparison merges shared-edge hits at this distance in metres.
		constexpr float kHitMergeDistance = 0.001f;
		constexpr float kParallelBoxAxis = 1e-8f;
		constexpr float kAffineWTolerance = 8.0f * std::numeric_limits<float>::epsilon();

		bool ValidPortal(const PortalBox& portal)
		{
			if (!portal.Instance || portal.Material >= kMaterialTagCount)
				return false;
			const auto& m = portal.WorldToBox;
			for (float value : m)
				if (!std::isfinite(value))
					return false;
			// The engine's general inverse can round the affine W=1 by an ULP. W is
			// discarded by its vec3 transform; retain the XYZ rows exactly for parity.
			if (m[3] != 0 || m[7] != 0 || m[11] != 0 || std::abs(m[15] - 1.0f) > kAffineWTolerance)
				return false;
			const double determinant = static_cast<double>(m[0]) * (static_cast<double>(m[5]) * m[10] - static_cast<double>(m[9]) * m[6]) -
				static_cast<double>(m[4]) * (static_cast<double>(m[1]) * m[10] - static_cast<double>(m[9]) * m[2]) +
				static_cast<double>(m[8]) * (static_cast<double>(m[1]) * m[6] - static_cast<double>(m[5]) * m[2]);
			return std::isfinite(determinant) && determinant != 0;
		}

		Position InBox(const PortalBox& portal, Position world)
		{
			const auto& m = portal.WorldToBox;
			// Pin the column-vector association used by the engine comparison path.
			return { (m[0] * world.X + m[4] * world.Y) + (m[8] * world.Z + m[12]),
				(m[1] * world.X + m[5] * world.Y) + (m[9] * world.Z + m[13]),
				(m[2] * world.X + m[6] * world.Y) + (m[10] * world.Z + m[14]) };
		}

		bool ClipBox(Position from, Position to, float& start, float& end)
		{
			const Position delta = Math::Subtract(to, from);
			start = 0;
			end = 1;
			for (std::uint32_t axis = 0; axis < 3; ++axis)
			{
				const float origin = Math::Component(from, axis), direction = Math::Component(delta, axis);
				if (std::abs(direction) < kParallelBoxAxis)
				{
					if (origin < -1 || origin > 1)
						return false;
					continue;
				}
				float a = (-1 - origin) / direction, b = (1 - origin) / direction;
				if (a > b)
					std::swap(a, b);
				start = std::max(start, a);
				end = std::min(end, b);
				if (start >= end)
					return false;
			}
			return true;
		}

		void AddWall(DirectScratch& scratch, const MaterialTable& materials, float strength,
			std::uint64_t instance, MaterialTag material, float start, float end, bool flat, bool portal)
		{
			const auto& properties = materials[material];
			Crossing crossing{ material, start, end, 0, 0, flat, portal };
			crossing.LossLF = flat ? strength * FlatLossDb(properties.FlatTransmissionLF) :
				SolidLossDb(end - start, properties.TransmissionLF, strength);
			crossing.LossHF = flat ? strength * FlatLossDb(properties.FlatTransmissionHF) :
				SolidLossDb(end - start, properties.TransmissionHF, strength);
			scratch.Walls.push_back({ crossing, instance });
		}
	}

	MaterialTable DefaultMaterials()
	{
		MaterialTable materials;
		for (std::size_t i = 0; i < materials.size(); ++i)
			materials[i] = GetDefaultMaterialProperties(static_cast<MaterialTag>(i));
		return materials;
	}

	EvaluationError EvaluateDirect(const Grid& grid, std::span<const PortalBox> portals,
		const MaterialTable& materials, const Settings& settings, Position listener,
		Position source, std::uint64_t sourceInstance, DirectScratch& scratch, Path& output)
	{
		output.Reset({}, {}, 0);
		scratch.Hits.clear();
		scratch.OpenSolids.clear();
		scratch.Walls.clear();
		scratch.PortalInstances.clear();
		if (!settings.Validate())
			return EvaluationError::InvalidSettings;
		for (const auto& material : materials)
			if (!IsValidMaterialProperties(material))
				return EvaluationError::InvalidMaterials;
		if (!Math::Finite(listener) || !Math::Finite(source))
			return EvaluationError::InvalidRay;
		const float length = Math::Length(Math::Subtract(source, listener));
		if (!std::isfinite(length))
			return EvaluationError::InvalidRay;
		for (const auto& portal : portals)
		{
			if (!ValidPortal(portal))
				return EvaluationError::InvalidPortal;
			const Position from = InBox(portal, listener), to = InBox(portal, source);
			if (!Math::Finite(from) || !Math::Finite(to) || !Math::Finite(Math::Subtract(to, from)))
				return EvaluationError::InvalidPortal;
			scratch.PortalInstances.push_back(portal.Instance);
		}
		std::sort(scratch.PortalInstances.begin(), scratch.PortalInstances.end());
		if (std::adjacent_find(scratch.PortalInstances.begin(), scratch.PortalInstances.end()) != scratch.PortalInstances.end())
			return EvaluationError::InvalidPortal;
		output.Reset(listener, source, length);
		if (length <= kHitMergeDistance)
			return EvaluationError::None;
		if (!grid.Trace(listener, source, sourceInstance, scratch.Hits))
			return EvaluationError::InvalidRay;
		for (const RayHit& hit : scratch.Hits)
		{
			const Triangle& triangle = grid.GetTriangles()[hit.TriangleIndex];
			if (triangle.Portal && std::binary_search(scratch.PortalInstances.begin(), scratch.PortalInstances.end(), triangle.Instance))
				continue;
			auto open = std::find_if(scratch.OpenSolids.begin(), scratch.OpenSolids.end(),
				[&](const DirectScratch::Entry& entry) { return entry.Instance == triangle.Instance; });
			if (!hit.Exit)
			{
				if (open == scratch.OpenSolids.end())
					scratch.OpenSolids.push_back({ triangle.Instance, triangle.Material, hit.Distance, triangle.Portal });
				else if (hit.Distance - open->Start > kHitMergeDistance)
				{
					AddWall(scratch, materials, settings.Strength, open->Instance, open->Material, open->Start, open->Start, true, open->Portal);
					*open = { triangle.Instance, triangle.Material, hit.Distance, triangle.Portal };
				}
				continue;
			}
			if (open != scratch.OpenSolids.end())
			{
				AddWall(scratch, materials, settings.Strength, open->Instance, open->Material, open->Start, hit.Distance, false, open->Portal);
				scratch.OpenSolids.erase(open);
				continue;
			}
			const bool duplicateExit = !scratch.Walls.empty() && scratch.Walls.back().Instance == triangle.Instance &&
				hit.Distance - scratch.Walls.back().Value.End <= kHitMergeDistance;
			if (!duplicateExit)
				AddWall(scratch, materials, settings.Strength, triangle.Instance, triangle.Material, hit.Distance, hit.Distance, true, triangle.Portal);
		}
		for (const auto& entry : scratch.OpenSolids)
			AddWall(scratch, materials, settings.Strength, entry.Instance, entry.Material, entry.Start, entry.Start, true, entry.Portal);
		for (const PortalBox& portal : portals)
		{
			float start = 0, end = 0;
			if (ClipBox(InBox(portal, listener), InBox(portal, source), start, end))
				AddWall(scratch, materials, settings.Strength, portal.Instance, portal.Material, start * length, end * length, false, true);
		}
		// A total ordering also pins coincident owners/materials for future GPU validation.
		std::sort(scratch.Walls.begin(), scratch.Walls.end(), [](const auto& a, const auto& b)
		{
			if (a.Value.Start != b.Value.Start)
				return a.Value.Start < b.Value.Start;
			if (a.Instance != b.Instance)
				return a.Instance < b.Instance;
			if (a.Value.End != b.Value.End)
				return a.Value.End < b.Value.End;
			return a.Value.Material < b.Value.Material;
		});
		for (const auto& wall : scratch.Walls)
			output.AddCrossing(wall.Value);
		output.Finalize();
		return EvaluationError::None;
	}
}
