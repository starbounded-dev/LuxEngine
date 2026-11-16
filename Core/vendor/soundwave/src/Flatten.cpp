// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "soundwave/Flatten.h"

namespace Lux::Soundwave
{
	bool Flatten(const GeometryView& geometry, FlatGeometry& output)
	{
		output = {};
		if (!geometry.IsValid())
			return false;
		output.Roots = geometry.GetRoots();
		output.Nodes.resize(geometry.GetNodeCount());
		for (std::uint32_t i = 0; i < output.Nodes.size(); ++i)
		{
			const auto node = geometry.GetNode(i);
			output.Nodes[i] = { { node.Box.Min.X, node.Box.Min.Y, node.Box.Min.Z }, node.First,
				{ node.Box.Max.X, node.Box.Max.Y, node.Box.Max.Z }, node.Count, node.Left, node.Right, 0, 0 };
		}
		output.Order.resize(geometry.GetTriangleCount());
		output.Triangles.resize(geometry.GetTriangleCount());
		for (std::uint32_t i = 0; i < output.Triangles.size(); ++i)
		{
			output.Order[i] = geometry.TriangleAtOrder(i);
			const auto t = geometry.GetTriangle(i);
			const std::uint32_t flags = (geometry.IsInstanceActive(t.Instance) ? kFlatActive : 0) | (t.Portal ? kFlatPortal : 0);
			output.Triangles[i] = { { t.A.X, t.A.Y, t.A.Z }, t.Material, { t.B.X, t.B.Y, t.B.Z }, flags, { t.C.X, t.C.Y, t.C.Z }, 0,
				static_cast<std::uint32_t>(t.Instance), static_cast<std::uint32_t>(t.Instance >> 32), 0, 0 };
		}
		return true;
	}
}
