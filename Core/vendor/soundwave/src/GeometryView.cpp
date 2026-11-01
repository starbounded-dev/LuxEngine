// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "soundwave/GeometryView.h"
#include <algorithm>
#include <stdexcept>

namespace Lux::Soundwave
{
	GeometryView::GeometryView(const Grid& fixed, const Grid* moving, std::span<const std::uint64_t> excludedInstances)
		: m_Fixed(fixed), m_Moving(moving), m_Excluded(excludedInstances)
	{
		m_Valid = moving != &fixed && std::is_sorted(m_Excluded.begin(), m_Excluded.end()) &&
			std::adjacent_find(m_Excluded.begin(), m_Excluded.end()) == m_Excluded.end();
		if (!m_Valid)
			return;
		m_FixedTriangles = static_cast<std::uint32_t>(fixed.GetTriangles().size());
		m_FixedNodes = static_cast<std::uint32_t>(fixed.GetNodes().size());
		m_FixedEdges = static_cast<std::uint32_t>(fixed.GetEdges().size());
		m_TriangleCount = m_FixedTriangles + (moving ? static_cast<std::uint32_t>(moving->GetTriangles().size()) : 0);
		m_NodeCount = m_FixedNodes + (moving ? static_cast<std::uint32_t>(moving->GetNodes().size()) : 0);
		m_EdgeCount = m_FixedEdges + (moving ? static_cast<std::uint32_t>(moving->GetEdges().size()) : 0);
	}
	bool GeometryView::IsInstanceActive(std::uint64_t instance) const
	{
		return m_Valid && !std::binary_search(m_Excluded.begin(), m_Excluded.end(), instance);
	}
	std::array<std::uint32_t, 2> GeometryView::GetRoots() const
	{
		return { m_FixedNodes ? 0u : kInvalidGeometryIndex,
			m_NodeCount > m_FixedNodes ? m_FixedNodes : kInvalidGeometryIndex };
	}
	const Triangle& GeometryView::GetTriangle(std::uint32_t index) const
	{
		if (!m_Valid || index >= m_TriangleCount)
			throw std::out_of_range("Invalid Soundwave triangle index");
		return index < m_FixedTriangles ? m_Fixed.GetTriangles()[index] : m_Moving->GetTriangles()[index - m_FixedTriangles];
	}
	Grid::Node GeometryView::GetNode(std::uint32_t index) const
	{
		if (!m_Valid || index >= m_NodeCount)
			throw std::out_of_range("Invalid Soundwave hierarchy index");
		if (index < m_FixedNodes)
			return m_Fixed.GetNodes()[index];
		auto node = m_Moving->GetNodes()[index - m_FixedNodes];
		node.First += m_FixedTriangles;
		if (!node.Count)
			node.Right += m_FixedNodes;
		return node;
	}
	std::uint32_t GeometryView::TriangleAtOrder(std::uint32_t index) const
	{
		if (!m_Valid || index >= m_TriangleCount)
			throw std::out_of_range("Invalid Soundwave triangle-order index");
		return index < m_FixedTriangles ? m_Fixed.GetTriangleOrder()[index] :
			m_Moving->GetTriangleOrder()[index - m_FixedTriangles] + m_FixedTriangles;
	}
	Edge GeometryView::GetEdge(std::uint32_t index) const
	{
		if (!m_Valid || index >= m_EdgeCount)
			throw std::out_of_range("Invalid Soundwave edge index");
		if (index < m_FixedEdges)
			return m_Fixed.GetEdges()[index];
		auto edge = m_Moving->GetEdges()[index - m_FixedEdges];
		edge.Triangle0 += m_FixedTriangles;
		if (edge.Triangle1 != kInvalidGeometryIndex)
			edge.Triangle1 += m_FixedTriangles;
		return edge;
	}
	std::array<std::uint32_t, 3> GeometryView::GetTriangleEdges(std::uint32_t triangle) const
	{
		if (!m_Valid || triangle >= m_TriangleCount)
			throw std::out_of_range("Invalid Soundwave triangle-edge index");
		if (triangle < m_FixedTriangles)
			return m_Fixed.GetTriangleEdges()[triangle];
		auto edges = m_Moving->GetTriangleEdges()[triangle - m_FixedTriangles];
		for (auto& index : edges)
			if (index != kInvalidGeometryIndex)
				index += m_FixedEdges;
		return edges;
	}
	bool GeometryView::Trace(Position from, Position to, std::uint64_t ignoredInstance, std::vector<RayHit>& hits, float endpointMargin) const
	{
		hits.clear();
		if (!m_Valid || !m_Fixed.AppendTrace(from, to, ignoredInstance, hits, endpointMargin, 0, m_Excluded) ||
			(m_Moving && !m_Moving->AppendTrace(from, to, ignoredInstance, hits, endpointMargin, m_FixedTriangles, m_Excluded)))
		{
			hits.clear();
			return false;
		}
		std::sort(hits.begin(), hits.end(), [this](const RayHit& a, const RayHit& b)
		{
			if (a.Distance != b.Distance)
				return a.Distance < b.Distance;
			const auto& x = GetTriangle(a.TriangleIndex);
			const auto& y = GetTriangle(b.TriangleIndex);
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
		if (!m_Valid || !m_Fixed.ClearanceInternal(point, radius, ignoredInstance, clear, m_Excluded))
			return false;
		if (!clear)
			return true;
		if (m_Moving && !m_Moving->ClearanceInternal(point, radius, ignoredInstance, clear, m_Excluded))
			return false;
		return true;
	}
}
