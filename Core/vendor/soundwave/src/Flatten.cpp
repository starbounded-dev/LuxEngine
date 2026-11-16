// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "soundwave/Flatten.h"
#include <new>
#include <stdexcept>
#include <utility>

namespace Lux::Soundwave
{
	FlatError Flatten(const GeometryView& geometry, FlatGeometry& output, std::size_t maxBytes)
	{
		if (!geometry.IsValid())
			return FlatError::InvalidGeometry;
		const auto nodes = static_cast<std::size_t>(geometry.GetNodeCount());
		const auto triangles = static_cast<std::size_t>(geometry.GetTriangleCount());
		// Counts are uint32_t, so this calculation fits uint64_t on every supported target.
		const auto bytes = static_cast<std::uint64_t>(nodes) * sizeof(FlatNode) +
			static_cast<std::uint64_t>(triangles) * (sizeof(std::uint32_t) + sizeof(FlatTriangle));
		if (bytes > maxBytes)
			return FlatError::BudgetExceeded;
		FlatGeometry candidate;
		try
		{
			candidate.Roots = geometry.GetRoots();
			candidate.Nodes.resize(nodes);
			candidate.Order.resize(triangles);
			candidate.Triangles.resize(triangles);
		}
		catch (const std::bad_alloc&)
		{
			return FlatError::AllocationFailed;
		}
		catch (const std::length_error&)
		{
			return FlatError::BudgetExceeded;
		}
		for (std::uint32_t i = 0; i < nodes; ++i)
		{
			const auto node = geometry.GetNode(i);
			candidate.Nodes[i] = { { node.Box.Min.X, node.Box.Min.Y, node.Box.Min.Z }, node.First,
				{ node.Box.Max.X, node.Box.Max.Y, node.Box.Max.Z }, node.Count, node.Left, node.Right, 0, 0 };
		}
		for (std::uint32_t i = 0; i < triangles; ++i)
		{
			candidate.Order[i] = geometry.TriangleAtOrder(i);
			const auto t = geometry.GetTriangle(i);
			const std::uint32_t flags = (geometry.IsInstanceActive(t.Instance) ? kFlatActive : 0) | (t.Portal ? kFlatPortal : 0);
			candidate.Triangles[i] = { { t.A.X, t.A.Y, t.A.Z }, t.Material, { t.B.X, t.B.Y, t.B.Z }, flags, { t.C.X, t.C.Y, t.C.Z }, 0,
				static_cast<std::uint32_t>(t.Instance), static_cast<std::uint32_t>(t.Instance >> 32), 0, 0 };
		}
		output = std::move(candidate);
		return FlatError::None;
	}
}
