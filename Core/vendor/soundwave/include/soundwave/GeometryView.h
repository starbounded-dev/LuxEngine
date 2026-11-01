// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "soundwave/Grid.h"

namespace Lux::Soundwave
{
	// Allocation-free view of immutable static/moving snapshots. The caller owns both
	// grids and the sorted exclusion list for the entire query/solve. Dynamic edits can
	// replace only the moving grid; removed instances are masked during async rebuilds.
	class GeometryView
	{
	public:
		explicit GeometryView(const Grid& fixed, const Grid* moving = nullptr,
			std::span<const std::uint64_t> excludedInstances = {});
		bool IsValid() const { return m_Valid; }
		bool IsInstanceActive(std::uint64_t instance) const;
		std::uint32_t GetTriangleCount() const { return m_TriangleCount; }
		std::uint32_t GetNodeCount() const { return m_NodeCount; }
		std::uint32_t GetEdgeCount() const { return m_EdgeCount; }
		std::array<std::uint32_t, 2> GetRoots() const;
		const Triangle& GetTriangle(std::uint32_t index) const;
		Grid::Node GetNode(std::uint32_t index) const;
		std::uint32_t TriangleAtOrder(std::uint32_t index) const;
		Edge GetEdge(std::uint32_t index) const;
		std::array<std::uint32_t, 3> GetTriangleEdges(std::uint32_t triangle) const;
		[[nodiscard]] bool Trace(Position from, Position to, std::uint64_t ignoredInstance,
			std::vector<RayHit>& hits, float endpointMargin = 0) const;
		[[nodiscard]] bool HasSurfaceClearance(Position point, float radius, std::uint64_t ignoredInstance, bool& clear) const;

	private:
		const Grid& m_Fixed;
		const Grid* m_Moving;
		std::span<const std::uint64_t> m_Excluded;
		std::uint32_t m_FixedTriangles = 0, m_FixedNodes = 0, m_FixedEdges = 0;
		std::uint32_t m_TriangleCount = 0, m_NodeCount = 0, m_EdgeCount = 0;
		bool m_Valid = false;
	};
}
