// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "soundwave/InstancedGrid.h"

namespace Lux::Soundwave
{
	// Allocation-free borrowed view. Both snapshots, their local mesh grids and the
	// sorted exclusions remain immutable and alive throughout every query/solve.
	// Removed instances are masked while asynchronous replacements are prepared.
	class GeometryView
	{
	public:
		explicit GeometryView(const Grid& fixed, const Grid* moving = nullptr,
			std::span<const std::uint64_t> excludedInstances = {});
		explicit GeometryView(const InstancedGrid& fixed, const InstancedGrid* moving = nullptr,
			std::span<const std::uint64_t> excludedInstances = {});
		bool IsValid() const { return m_Valid; }
		bool IsInstanceActive(std::uint64_t instance) const;
		std::uint32_t GetTriangleCount() const { return m_TriangleCount; }
		std::uint32_t GetNodeCount() const { return m_NodeCount; }
		std::uint32_t GetEdgeCount() const { return m_EdgeCount; }
		std::array<std::uint32_t, 2> GetRoots() const;
		Triangle GetTriangle(std::uint32_t index) const;
		Grid::Node GetNode(std::uint32_t index) const;
		std::uint32_t TriangleAtOrder(std::uint32_t index) const;
		Edge GetEdge(std::uint32_t index) const;
		std::array<std::uint32_t, 3> GetTriangleEdges(std::uint32_t triangle) const;
		[[nodiscard]] bool Trace(Position from, Position to, std::uint64_t ignoredInstance,
			std::vector<RayHit>& hits, float endpointMargin = 0) const;
		[[nodiscard]] bool HasSurfaceClearance(Position point, float radius, std::uint64_t ignoredInstance, bool& clear) const;

	private:
		struct Layer
		{
			const Grid* Mesh = nullptr;
			const InstancedGrid* Instances = nullptr;
			std::uint32_t Triangles = 0, Nodes = 0, Edges = 0;
			explicit Layer(const Grid* grid);
			explicit Layer(const InstancedGrid* grid);
			Triangle GetTriangle(std::uint32_t index) const;
			Grid::Node GetNode(std::uint32_t index) const;
			std::uint32_t TriangleAtOrder(std::uint32_t index) const;
			Edge GetEdge(std::uint32_t index) const;
			std::array<std::uint32_t, 3> GetTriangleEdges(std::uint32_t index) const;
		};
		GeometryView(Layer fixed, Layer moving, std::span<const std::uint64_t> excluded);
		Layer m_Fixed, m_Moving;
		std::span<const std::uint64_t> m_Excluded;
		std::uint32_t m_TriangleCount = 0, m_NodeCount = 0, m_EdgeCount = 0;
		bool m_Valid = false;
	};
}
