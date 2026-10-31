// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "soundwave/Grid.h"
#include "GeometryMath.h"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <numeric>
#include <tuple>
#include <utility>

namespace Lux::Soundwave
{
	namespace
	{
		constexpr std::uint32_t kLeafTriangles = 4;
		constexpr float kBarycentricEpsilon = 1e-6f;
		constexpr float kCoplanarCosine = 1.0f - 1e-5f;
		using EdgeKey = std::array<float, 6>;
		EdgeKey Key(Position a, Position b)
		{
			if (std::tie(b.X, b.Y, b.Z) < std::tie(a.X, a.Y, a.Z))
				std::swap(a, b);
			return { a.X,a.Y,a.Z,b.X,b.Y,b.Z };
		}
		struct PrecisePosition { double X, Y, Z; };
		PrecisePosition Difference(Position a, Position b)
		{
			return { static_cast<double>(a.X) - b.X, static_cast<double>(a.Y) - b.Y, static_cast<double>(a.Z) - b.Z };
		}
		PrecisePosition Cross(PrecisePosition a, PrecisePosition b)
		{
			return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X };
		}
		double Dot(PrecisePosition a, PrecisePosition b)
		{
			return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
		}
		double SegmentDistanceSquared(Position point, Position a, Position b)
		{
			const auto edge = Difference(b, a), offset = Difference(point, a);
			const double lengthSquared = Dot(edge, edge);
			const double t = lengthSquared == 0 ? 0 : std::clamp(Dot(offset, edge) / lengthSquared, 0.0, 1.0);
			const PrecisePosition rest{ offset.X - t * edge.X, offset.Y - t * edge.Y, offset.Z - t * edge.Z };
			return Dot(rest, rest);
		}
		double SurfaceDistanceSquared(Position point, const Triangle& triangle)
		{
			const auto normal = Cross(Difference(triangle.B, triangle.A), Difference(triangle.C, triangle.A));
			if (Dot(Cross(Difference(triangle.B, triangle.A), Difference(point, triangle.A)), normal) >= 0 &&
				Dot(Cross(Difference(triangle.C, triangle.B), Difference(point, triangle.B)), normal) >= 0 &&
				Dot(Cross(Difference(triangle.A, triangle.C), Difference(point, triangle.C)), normal) >= 0)
			{
				const double distance = Dot(Difference(point, triangle.A), normal);
				return distance * distance / Dot(normal, normal);
			}
			return std::min({ SegmentDistanceSquared(point, triangle.A, triangle.B),
				SegmentDistanceSquared(point, triangle.B, triangle.C), SegmentDistanceSquared(point, triangle.C, triangle.A) });
		}

		Bounds TriangleBounds(const Triangle& triangle)
		{
			return { Math::Min(triangle.A, Math::Min(triangle.B, triangle.C)),
				Math::Max(triangle.A, Math::Max(triangle.B, triangle.C)) };
		}

		bool Intersects(const Bounds& box, Position from, Position direction, float length)
		{
			float start = 0.0f, end = length;
			for (std::uint32_t axis = 0; axis < 3; ++axis)
			{
				const float origin = Math::Component(from, axis);
				const float delta = Math::Component(direction, axis);
				const float low = Math::Component(box.Min, axis), high = Math::Component(box.Max, axis);
				if (delta == 0.0f)
				{
					if (origin < low || origin > high)
						return false;
					continue;
				}
				const float a = (low - origin) / delta, b = (high - origin) / delta;
				start = std::max(start, std::min(a, b));
				end = std::min(end, std::max(a, b));
				if (start > end)
					return false;
			}
			return true;
		}

		bool IntersectTriangle(const Triangle& triangle, Position from, Position direction, float& distance, bool& exit)
		{
			// Wide intermediates prevent a valid large triangle's determinant numerator
			// overflowing into a silent miss. Geometry and returned distances stay floats.
			const PrecisePosition edge1 = Difference(triangle.B, triangle.A);
			const PrecisePosition edge2 = Difference(triangle.C, triangle.A);
			const PrecisePosition ray{ direction.X, direction.Y, direction.Z };
			const PrecisePosition p = Cross(ray, edge2);
			const double determinant = Dot(edge1, p);
			if (determinant == 0.0)
				return false;
			const double inverse = 1.0 / determinant;
			const PrecisePosition offset = Difference(from, triangle.A);
			const double u = Dot(offset, p) * inverse;
			if (u < -kBarycentricEpsilon || u > 1.0f + kBarycentricEpsilon)
				return false;
			const PrecisePosition q = Cross(offset, edge1);
			const double v = Dot(ray, q) * inverse;
			if (v < -kBarycentricEpsilon || u + v > 1.0f + kBarycentricEpsilon)
				return false;
			const double hitDistance = Dot(edge2, q) * inverse;
			if (!std::isfinite(hitDistance) || hitDistance > std::numeric_limits<float>::max() ||
				hitDistance < std::numeric_limits<float>::lowest())
				return false;
			distance = static_cast<float>(hitDistance);
			exit = determinant < 0.0f;
			return std::isfinite(distance);
		}
	}

	GeometryError Grid::Build(std::span<const Triangle> triangles)
	{
		if (triangles.size() > MaximumTriangles)
			return GeometryError::TooManyTriangles;
		for (const Triangle& triangle : triangles)
		{
			if (!triangle.Instance)
				return GeometryError::InvalidIdentity;
			if (triangle.Material >= kMaterialTagCount)
				return GeometryError::InvalidMaterial;
			if (!Math::Finite(triangle.A) || !Math::Finite(triangle.B) || !Math::Finite(triangle.C))
				return GeometryError::NonFinite;
			const Position normal = Math::Cross(Math::Subtract(triangle.B, triangle.A), Math::Subtract(triangle.C, triangle.A));
			const double areaSquared = static_cast<double>(normal.X) * normal.X +
				static_cast<double>(normal.Y) * normal.Y + static_cast<double>(normal.Z) * normal.Z;
			if (!Math::Finite(normal) || areaSquared <= 1e-20)
				return GeometryError::Degenerate;
		}
		Grid built;
		built.m_Triangles.assign(triangles.begin(), triangles.end());
		built.m_Order.resize(triangles.size());
		std::iota(built.m_Order.begin(), built.m_Order.end(), 0u);
		if (!triangles.empty())
		{
			built.m_Nodes.reserve(triangles.size() * 2 - 1);
			built.BuildNode(0, static_cast<std::uint32_t>(triangles.size()));
			built.BuildEdges();
		}
		*this = std::move(built);
		return GeometryError::None;
	}

	void Grid::BuildEdges()
	{
		struct Adjacency
		{
			std::uint32_t First = 0, Second = kInvalidGeometryIndex, Count = 0, Index = kInvalidGeometryIndex;
			Position Normal;
			bool Coplanar = false;
		};
		std::map<EdgeKey, Adjacency> edges;
		for (std::uint32_t i = 0; i < m_Triangles.size(); ++i)
		{
			const auto& triangle = m_Triangles[i];
			const Position normal = Math::Unit(Math::Cross(Math::Subtract(triangle.B, triangle.A), Math::Subtract(triangle.C, triangle.A)));
			for (const auto& key : { Key(triangle.A, triangle.B), Key(triangle.B, triangle.C), Key(triangle.C, triangle.A) })
			{
				auto& adjacent = edges[key];
				if (!adjacent.Count)
				{
					adjacent.First = i;
					adjacent.Normal = normal;
				}
				else if (adjacent.Count == 1)
				{
					adjacent.Second = i;
					adjacent.Coplanar = std::abs(Math::Dot(adjacent.Normal, normal)) >= kCoplanarCosine;
				}
				++adjacent.Count;
			}
		}
		for (auto& [key, adjacent] : edges)
		{
			if (adjacent.Count > 2)
			{
				++m_Topology.NonManifoldEdges;
				continue;
			}
			if (adjacent.Count == 2 && adjacent.Coplanar)
				continue; // a material seam or triangle diagonal cannot diffract sound
			adjacent.Index = static_cast<std::uint32_t>(m_Edges.size());
			m_Edges.push_back({ {key[0],key[1],key[2]}, {key[3],key[4],key[5]}, adjacent.First, adjacent.Second });
			if (adjacent.Count == 1)
				++m_Topology.BoundaryEdges;
			else
				++m_Topology.CreaseEdges;
		}
		m_TriangleEdges.reserve(m_Triangles.size());
		for (const auto& triangle : m_Triangles)
			m_TriangleEdges.push_back({ edges.at(Key(triangle.A, triangle.B)).Index,
				edges.at(Key(triangle.B, triangle.C)).Index, edges.at(Key(triangle.C, triangle.A)).Index });
	}

	std::uint32_t Grid::BuildNode(std::uint32_t first, std::uint32_t count)
	{
		const std::uint32_t index = static_cast<std::uint32_t>(m_Nodes.size());
		Bounds box = TriangleBounds(m_Triangles[m_Order[first]]);
		for (std::uint32_t i = first + 1; i < first + count; ++i)
		{
			const Bounds next = TriangleBounds(m_Triangles[m_Order[i]]);
			box.Min = Math::Min(box.Min, next.Min);
			box.Max = Math::Max(box.Max, next.Max);
		}
		m_Nodes.push_back({ box, first, count, 0 });
		if (count <= kLeafTriangles)
			return index;
		const Position extent = Math::Subtract(box.Max, box.Min);
		std::uint32_t axis = extent.Y > extent.X ? 1u : 0u;
		if (extent.Z > Math::Component(extent, axis))
			axis = 2;
		// Compare half-sums of bounds rather than summing vertices (which can overflow).
		const auto center = [this, axis](std::uint32_t triangleIndex)
		{
			const Bounds bounds = TriangleBounds(m_Triangles[triangleIndex]);
			return Math::Component(bounds.Min, axis) * 0.5f + Math::Component(bounds.Max, axis) * 0.5f;
		};
		std::stable_sort(m_Order.begin() + first, m_Order.begin() + first + count,
			[&](std::uint32_t a, std::uint32_t b) { return center(a) < center(b); });
		const std::uint32_t leftCount = count / 2;
		BuildNode(first, leftCount);
		const std::uint32_t right = BuildNode(first + leftCount, count - leftCount);
		m_Nodes[index].Count = 0;
		m_Nodes[index].Right = right;
		return index;
	}

	bool Grid::HasSurfaceClearance(Position point, float radius, std::uint64_t ignoredInstance, bool& clear) const
	{
		clear = false;
		if (!Math::Finite(point) || !std::isfinite(radius) || radius < 0)
			return false;
		const double radiusSquared = static_cast<double>(radius) * radius;
		std::array<std::uint32_t, 32> stack{};
		std::uint32_t pending = m_Nodes.empty() ? 0u : 1u;
		while (pending)
		{
			const auto index = stack[--pending];
			const auto& node = m_Nodes[index];
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
				stack[pending++] = node.Right;
				stack[pending++] = index + 1;
				continue;
			}
			for (auto i = node.First; i < node.First + node.Count; ++i)
			{
				const auto& triangle = m_Triangles[m_Order[i]];
				if (triangle.Instance != ignoredInstance && SurfaceDistanceSquared(point, triangle) <= radiusSquared)
					return true;
			}
		}
		clear = true;
		return true;
	}

	bool Grid::Trace(const Position& from, const Position& to, std::uint64_t ignoredInstance,
		std::vector<RayHit>& hits, float endpointMargin) const
	{
		hits.clear();
		if (!Math::Finite(from) || !Math::Finite(to) || !std::isfinite(endpointMargin) || endpointMargin < 0.0f)
			return false;
		const Position delta = Math::Subtract(to, from);
		const float length = Math::Length(delta);
		if (!std::isfinite(length))
			return false;
		if (length <= endpointMargin * 2.0f || length == 0.0f || m_Nodes.empty())
			return true;
		const Position direction = Math::Scale(delta, 1.0f / length);
		// Median subdivision bounds depth to ceil(log2(MaximumTriangles)); no per-ray
		// traversal allocation or unbounded recursion. Readers share no mutable scratch.
		std::array<std::uint32_t, 32> stack{};
		std::uint32_t pending = 1;
		while (pending)
		{
			const std::uint32_t index = stack[--pending];
			const Node& node = m_Nodes[index];
			if (!Intersects(node.Box, from, direction, length))
				continue;
			if (!node.Count)
			{
				stack[pending++] = node.Right;
				stack[pending++] = index + 1;
				continue;
			}
			for (std::uint32_t i = node.First; i < node.First + node.Count; ++i)
			{
				const std::uint32_t triangleIndex = m_Order[i];
				const Triangle& triangle = m_Triangles[triangleIndex];
				if (triangle.Instance == ignoredInstance)
					continue;
				float distance = 0.0f;
				bool exit = false;
				if (IntersectTriangle(triangle, from, direction, distance, exit) && distance > endpointMargin && distance < length - endpointMargin)
					hits.push_back({ distance, triangleIndex, exit });
			}
		}
		std::sort(hits.begin(), hits.end(), [this](const RayHit& a, const RayHit& b)
		{
			if (a.Distance != b.Distance)
				return a.Distance < b.Distance;
			const auto& ta = m_Triangles[a.TriangleIndex];
			const auto& tb = m_Triangles[b.TriangleIndex];
			if (ta.Instance != tb.Instance)
				return ta.Instance < tb.Instance;
			if (a.Exit != b.Exit)
				return a.Exit < b.Exit;
			return a.TriangleIndex < b.TriangleIndex;
		});
		return true;
	}

}
