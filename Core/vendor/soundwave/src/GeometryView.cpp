// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "soundwave/GeometryView.h"
#include "GeometryQueries.h"
#include <algorithm>
#include <stdexcept>

namespace Lux::Soundwave
{
	GeometryView::Layer::Layer(const Grid* grid) : Mesh(grid)
	{
		if (grid)
		{
			Triangles = static_cast<std::uint32_t>(grid->GetTriangles().size());
			Nodes = static_cast<std::uint32_t>(grid->GetNodes().size());
			Edges = static_cast<std::uint32_t>(grid->GetEdges().size());
		}
	}
	GeometryView::Layer::Layer(const InstancedGrid* grid) : Instances(grid)
	{
		if (grid)
		{
			Triangles = grid->GetTriangleCount();
			Nodes = grid->GetNodeCount();
			Edges = grid->GetEdgeCount();
		}
	}
	Triangle GeometryView::Layer::GetTriangle(std::uint32_t index) const
	{
		return Mesh ? Mesh->GetTriangles()[index] : Instances->GetTriangle(index);
	}
	Grid::Node GeometryView::Layer::GetNode(std::uint32_t index) const
	{
		return Mesh ? Mesh->GetNodes()[index] : Instances->GetNode(index);
	}
	std::uint32_t GeometryView::Layer::TriangleAtOrder(std::uint32_t index) const
	{
		return Mesh ? Mesh->GetTriangleOrder()[index] : Instances->TriangleAtOrder(index);
	}
	Edge GeometryView::Layer::GetEdge(std::uint32_t index) const
	{
		return Mesh ? Mesh->GetEdges()[index] : Instances->GetEdge(index);
	}
	std::array<std::uint32_t, 3> GeometryView::Layer::GetTriangleEdges(std::uint32_t index) const
	{
		return Mesh ? Mesh->GetTriangleEdges()[index] : Instances->GetTriangleEdges(index);
	}
	GeometryView::GeometryView(const Grid& fixed, const Grid* moving, std::span<const std::uint64_t> excluded)
		: GeometryView(Layer(&fixed), Layer(moving), excluded) {}
	GeometryView::GeometryView(const InstancedGrid& fixed, const InstancedGrid* moving, std::span<const std::uint64_t> excluded)
		: GeometryView(Layer(&fixed), Layer(moving), excluded) {}
	GeometryView::GeometryView(Layer fixed, Layer moving, std::span<const std::uint64_t> excluded)
		: m_Fixed(fixed), m_Moving(moving), m_Excluded(excluded)
	{
		m_Valid = !(fixed.Mesh && fixed.Mesh == moving.Mesh) && !(fixed.Instances && fixed.Instances == moving.Instances) &&
			std::is_sorted(excluded.begin(), excluded.end()) && std::adjacent_find(excluded.begin(), excluded.end()) == excluded.end();
		const auto sum = [&](auto a, auto b, auto& output)
		{
			const auto count = static_cast<std::uint64_t>(a) + b;
			if (count >= kInvalidGeometryIndex)
				m_Valid = false;
			else
				output = static_cast<std::uint32_t>(count);
		};
		sum(fixed.Triangles, moving.Triangles, m_TriangleCount);
		sum(fixed.Nodes, moving.Nodes, m_NodeCount);
		sum(fixed.Edges, moving.Edges, m_EdgeCount);
	}
	bool GeometryView::IsInstanceActive(std::uint64_t instance) const
	{
		return m_Valid && !std::binary_search(m_Excluded.begin(), m_Excluded.end(), instance);
	}
	std::array<std::uint32_t, 2> GeometryView::GetRoots() const
	{
		return { m_Fixed.Nodes ? 0u : kInvalidGeometryIndex, m_Moving.Nodes ? m_Fixed.Nodes : kInvalidGeometryIndex };
	}
	Triangle GeometryView::GetTriangle(std::uint32_t index) const
	{
		if (!m_Valid || index >= m_TriangleCount)
			throw std::out_of_range("Invalid Soundwave triangle index");
		return index < m_Fixed.Triangles ? m_Fixed.GetTriangle(index) : m_Moving.GetTriangle(index - m_Fixed.Triangles);
	}
	Grid::Node GeometryView::GetNode(std::uint32_t index) const
	{
		if (!m_Valid || index >= m_NodeCount)
			throw std::out_of_range("Invalid Soundwave hierarchy index");
		if (index < m_Fixed.Nodes)
			return m_Fixed.GetNode(index);
		auto node = m_Moving.GetNode(index - m_Fixed.Nodes);
		if (node.Count)
			node.First += m_Fixed.Triangles;
		else
		{
			node.Left += m_Fixed.Nodes;
			if (node.Right != kInvalidGeometryIndex)
				node.Right += m_Fixed.Nodes;
		}
		return node;
	}
	std::uint32_t GeometryView::TriangleAtOrder(std::uint32_t index) const
	{
		if (!m_Valid || index >= m_TriangleCount)
			throw std::out_of_range("Invalid Soundwave triangle-order index");
		return index < m_Fixed.Triangles ? m_Fixed.TriangleAtOrder(index) :
			m_Moving.TriangleAtOrder(index - m_Fixed.Triangles) + m_Fixed.Triangles;
	}
	Edge GeometryView::GetEdge(std::uint32_t index) const
	{
		if (!m_Valid || index >= m_EdgeCount)
			throw std::out_of_range("Invalid Soundwave edge index");
		if (index < m_Fixed.Edges)
			return m_Fixed.GetEdge(index);
		auto edge = m_Moving.GetEdge(index - m_Fixed.Edges);
		edge.Triangle0 += m_Fixed.Triangles;
		if (edge.Triangle1 != kInvalidGeometryIndex)
			edge.Triangle1 += m_Fixed.Triangles;
		return edge;
	}
	std::array<std::uint32_t, 3> GeometryView::GetTriangleEdges(std::uint32_t triangle) const
	{
		if (!m_Valid || triangle >= m_TriangleCount)
			throw std::out_of_range("Invalid Soundwave triangle-edge index");
		if (triangle < m_Fixed.Triangles)
			return m_Fixed.GetTriangleEdges(triangle);
		auto edges = m_Moving.GetTriangleEdges(triangle - m_Fixed.Triangles);
		for (auto& index : edges)
			if (index != kInvalidGeometryIndex)
				index += m_Fixed.Edges;
		return edges;
	}
	bool GeometryView::Trace(Position from, Position to, std::uint64_t ignoredInstance, std::vector<RayHit>& hits, float endpointMargin) const
	{
		hits.clear();
		if (!m_Valid || !Math::Finite(from) || !Math::Finite(to) || !std::isfinite(endpointMargin) || endpointMargin < 0)
			return false;
		const auto delta = Math::Subtract(to, from);
		const float length = Math::Length(delta);
		if (!std::isfinite(length))
			return false;
		if (length == 0 || length <= 2 * endpointMargin)
			return true;
		const auto direction = Math::Scale(delta, 1.0f / length);
		// Median instance + mesh BVHs: depth <= 20 + 20 + two roots.
		std::array<std::uint32_t, 64> stack{};
		std::uint32_t pending = 0;
		for (auto root : GetRoots())
			if (root != kInvalidGeometryIndex)
				stack[pending++] = root;
		while (pending)
		{
			const auto node = GetNode(stack[--pending]);
			if (!Queries::Intersects(node.Box, from, direction, length))
				continue;
			if (!node.Count)
			{
				if (node.Right != kInvalidGeometryIndex)
					stack[pending++] = node.Right;
				stack[pending++] = node.Left;
				continue;
			}
			for (auto i = node.First; i < node.First + node.Count; ++i)
			{
				const auto index = TriangleAtOrder(i);
				const auto triangle = GetTriangle(index);
				if (triangle.Instance == ignoredInstance || !IsInstanceActive(triangle.Instance))
					continue;
				float distance = 0;
				bool exit = false;
				if (Queries::IntersectTriangle(triangle, from, direction, distance, exit) &&
					distance > endpointMargin && distance < length - endpointMargin)
					hits.push_back({ distance, index, exit });
			}
		}
		std::sort(hits.begin(), hits.end(), [this](const auto& a, const auto& b)
		{
			if (a.Distance != b.Distance)
				return a.Distance < b.Distance;
			const auto x = GetTriangle(a.TriangleIndex), y = GetTriangle(b.TriangleIndex);
			if (x.Instance != y.Instance)
				return x.Instance < y.Instance;
			if (a.Exit != b.Exit)
				return a.Exit < b.Exit;
			return a.TriangleIndex < b.TriangleIndex;
		});
		return true;
	}
	bool GeometryView::HasSurfaceClearance(Position point, float radius, std::uint64_t ignoredInstance, bool& clear) const
	{
		clear = false;
		if (!m_Valid || !Math::Finite(point) || !std::isfinite(radius) || radius < 0)
			return false;
		const double radiusSquared = static_cast<double>(radius) * radius;
		std::array<std::uint32_t, 64> stack{};
		std::uint32_t pending = 0;
		for (auto root : GetRoots())
			if (root != kInvalidGeometryIndex)
				stack[pending++] = root;
		while (pending)
		{
			const auto node = GetNode(stack[--pending]);
			double boxDistance = 0;
			for (std::uint32_t axis = 0; axis < 3; ++axis)
			{
				const double p = Math::Component(point, axis), low = Math::Component(node.Box.Min, axis), high = Math::Component(node.Box.Max, axis);
				const double delta = p < low ? low - p : (p > high ? p - high : 0);
				boxDistance += delta * delta;
			}
			if (boxDistance > radiusSquared)
				continue;
			if (!node.Count)
			{
				if (node.Right != kInvalidGeometryIndex)
					stack[pending++] = node.Right;
				stack[pending++] = node.Left;
				continue;
			}
			for (auto i = node.First; i < node.First + node.Count; ++i)
			{
				const auto triangle = GetTriangle(TriangleAtOrder(i));
				if (triangle.Instance != ignoredInstance && IsInstanceActive(triangle.Instance) &&
					Queries::SurfaceDistanceSquared(point, triangle) <= radiusSquared)
					return true;
			}
		}
		clear = true;
		return true;
	}
}
