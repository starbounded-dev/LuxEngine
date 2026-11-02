// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "soundwave/InstancedGrid.h"
#include "GeometryMath.h"
#include "GeometryQueries.h"
#include <algorithm>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace Lux::Soundwave
{
	namespace
	{
		constexpr std::array<float, 16> kIdentity{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		// Bounds must enclose separately rounded world vertices, including cancellation.
		constexpr double kTransformRounding = 8 * std::numeric_limits<float>::epsilon();
		bool AddCount(std::uint32_t& value, std::size_t count)
		{
			if (count >= kInvalidGeometryIndex - value)
				return false;
			value += static_cast<std::uint32_t>(count);
			return true;
		}
	}
	Position InstancedGrid::Transform(const InstanceData& instance, Position point)
	{
		if (instance.Identity)
			return point;
		const auto& m = instance.Value.LocalToWorld;
		return { (m[0] * point.X + m[4] * point.Y) + (m[8] * point.Z + m[12]),
			(m[1] * point.X + m[5] * point.Y) + (m[9] * point.Z + m[13]),
			(m[2] * point.X + m[6] * point.Y) + (m[10] * point.Z + m[14]) };
	}
	Bounds InstancedGrid::TransformBounds(const InstanceData& instance, Bounds bounds)
	{
		if (instance.Identity)
			return bounds;
		const auto& m = instance.Value.LocalToWorld;
		std::array<float, 3> lower{}, upper{};
		for (std::uint32_t row = 0; row < 3; ++row)
		{
			double low = m[12 + row], high = low, magnitude = std::abs(low);
			for (std::uint32_t column = 0; column < 3; ++column)
			{
				const double a = static_cast<double>(m[column * 4 + row]) * Math::Component(bounds.Min, column);
				const double b = static_cast<double>(m[column * 4 + row]) * Math::Component(bounds.Max, column);
				low += std::min(a, b);
				high += std::max(a, b);
				magnitude += std::max(std::abs(a), std::abs(b));
			}
			const double error = magnitude * kTransformRounding + std::numeric_limits<float>::denorm_min() * 8.0;
			const double minimum = low - error, maximum = high + error;
			const double limit = std::numeric_limits<float>::max();
			const float infinity = std::numeric_limits<float>::infinity();
			// Reject before an overflowing conversion/nextafter can trip the engine's
			// floating exception policy. Build reports these invalid bounds explicitly.
			if (!(minimum > -limit && maximum < limit))
				return {{-infinity,-infinity,-infinity},{infinity,infinity,infinity}};
			lower[row] = static_cast<float>(minimum);
			upper[row] = static_cast<float>(maximum);
			if (lower[row] == -std::numeric_limits<float>::max() || upper[row] == std::numeric_limits<float>::max())
				return {{-infinity,-infinity,-infinity},{infinity,infinity,infinity}};
			lower[row] = std::nextafter(lower[row], -infinity);
			upper[row] = std::nextafter(upper[row], infinity);
		}
		return { {lower[0],lower[1],lower[2]}, {upper[0],upper[1],upper[2]} };
	}
	InstanceError InstancedGrid::Build(std::span<const GeometryInstance> instances)
	{
		if (instances.size() > MaximumInstances)
			return InstanceError::TooManyInstances;
		InstancedGrid built;
		built.m_Instances.reserve(instances.size());
		for (const auto& value : instances)
		{
			if (!value.Geometry || value.Geometry->GetNodes().empty())
				return InstanceError::InvalidGeometry;
			const auto& m = value.LocalToWorld;
			for (float number : m)
				if (!std::isfinite(number))
					return InstanceError::NonFinite;
			if (m[3] != 0 || m[7] != 0 || m[11] != 0 || std::abs(m[15] - 1) > 8 * std::numeric_limits<float>::epsilon())
				return InstanceError::InvalidTransform;
			const double determinant = static_cast<double>(m[0]) * (static_cast<double>(m[5]) * m[10] - static_cast<double>(m[9]) * m[6]) -
				static_cast<double>(m[4]) * (static_cast<double>(m[1]) * m[10] - static_cast<double>(m[9]) * m[2]) +
				static_cast<double>(m[8]) * (static_cast<double>(m[1]) * m[6] - static_cast<double>(m[5]) * m[2]);
			if (!std::isfinite(determinant) || determinant == 0)
				return InstanceError::InvalidTransform;
			for (std::size_t i = 0; i < value.Materials.size(); ++i)
				if (value.Materials[i].Material >= kMaterialTagCount ||
					(i && value.Materials[i - 1].Surface >= value.Materials[i].Surface) || !value.Instance)
					return InstanceError::InvalidMaterials;
			if (!value.Instance && value.Portal)
				return InstanceError::InvalidMaterials;
			InstanceData data{ value, {}, built.m_TriangleCount, built.m_NodeCount, built.m_EdgeCount,
				m == kIdentity, determinant < 0 };
			if (!AddCount(built.m_TriangleCount, value.Geometry->GetTriangles().size()) ||
				!AddCount(built.m_NodeCount, value.Geometry->GetNodes().size()) ||
				!AddCount(built.m_EdgeCount, value.Geometry->GetEdges().size()))
				return InstanceError::TooManyIndices;
			data.Box = TransformBounds(data, value.Geometry->GetNodes()[0].Box);
			if (!Math::Finite(data.Box.Min) || !Math::Finite(data.Box.Max))
				return InstanceError::NonFinite;
			// Validate rounding at the authored world position too. A finite nonsingular
			// matrix can still collapse a small triangle after float translation.
			for (const auto& triangle : value.Geometry->GetTriangles())
			{
				const auto a = Transform(data, triangle.A), b = Transform(data, triangle.B), c = Transform(data, triangle.C);
				if (!Math::Finite(a) || !Math::Finite(b) || !Math::Finite(c))
					return InstanceError::NonFinite;
				if (value.Geometry->GetDegeneratePolicy() == DegeneratePolicy::Reject)
				{
					const auto normal = Math::Cross(Math::Subtract(b, a), Math::Subtract(c, a));
					const double squared = static_cast<double>(normal.X) * normal.X + static_cast<double>(normal.Y) * normal.Y + static_cast<double>(normal.Z) * normal.Z;
					if (!Math::Finite(normal) || squared <= 1e-20)
						return InstanceError::Degenerate;
				}
				if (!Queries::HasArea({a,b,c}))
					++built.m_InertTriangles;
			}
			built.m_Instances.push_back(std::move(data));
		}
		built.m_Order.resize(instances.size());
		std::iota(built.m_Order.begin(), built.m_Order.end(), 0u);
		if (!instances.empty())
		{
			built.m_Nodes.reserve(instances.size() * 2 - 1);
			built.BuildNode(0, static_cast<std::uint32_t>(instances.size()));
		}
		if (!AddCount(built.m_NodeCount, built.m_Nodes.size()))
			return InstanceError::TooManyIndices;
		*this = std::move(built);
		return InstanceError::None;
	}
	std::uint32_t InstancedGrid::BuildNode(std::uint32_t first, std::uint32_t count)
	{
		const auto index = static_cast<std::uint32_t>(m_Nodes.size());
		Bounds box = m_Instances[m_Order[first]].Box;
		for (auto i = first + 1; i < first + count; ++i)
		{
			box.Min = Math::Min(box.Min, m_Instances[m_Order[i]].Box.Min);
			box.Max = Math::Max(box.Max, m_Instances[m_Order[i]].Box.Max);
		}
		m_Nodes.push_back({ box, m_Order[first], 0 });
		if (count == 1)
			return index;
		const auto extent = Math::Subtract(box.Max, box.Min);
		std::uint32_t axis = extent.Y > extent.X ? 1u : 0u;
		if (extent.Z > Math::Component(extent, axis))
			axis = 2;
		std::stable_sort(m_Order.begin() + first, m_Order.begin() + first + count, [&](auto a, auto b)
		{
			const auto& x = m_Instances[a].Box;
			const auto& y = m_Instances[b].Box;
			return Math::Component(x.Min, axis) * 0.5f + Math::Component(x.Max, axis) * 0.5f <
				Math::Component(y.Min, axis) * 0.5f + Math::Component(y.Max, axis) * 0.5f;
		});
		const auto left = BuildNode(first, count / 2);
		const auto right = BuildNode(first + count / 2, count - count / 2);
		m_Nodes[index].Left = left;
		m_Nodes[index].Right = right;
		return index;
	}
	const InstancedGrid::InstanceData& InstancedGrid::FindTriangle(std::uint32_t index) const
	{
		if (index >= m_TriangleCount)
			throw std::out_of_range("Invalid Soundwave instance triangle index");
		return *std::prev(std::upper_bound(m_Instances.begin(), m_Instances.end(), index,
			[](auto value, const auto& instance) { return value < instance.TriangleBegin; }));
	}
	Triangle InstancedGrid::GetTriangle(std::uint32_t index) const
	{
		const auto& instance = FindTriangle(index);
		auto triangle = instance.Value.Geometry->GetTriangles()[index - instance.TriangleBegin];
		triangle.A = Transform(instance, triangle.A);
		triangle.B = Transform(instance, triangle.B);
		triangle.C = Transform(instance, triangle.C);
		if (instance.Mirrored)
			std::swap(triangle.B, triangle.C);
		if (instance.Value.Instance)
		{
			triangle.Instance = instance.Value.Instance;
			triangle.Portal = instance.Value.Portal;
			const auto& overrides = instance.Value.Materials;
			const auto material = std::lower_bound(overrides.begin(), overrides.end(), triangle.Surface,
				[](const auto& entry, auto surface) { return entry.Surface < surface; });
			if (material != overrides.end() && material->Surface == triangle.Surface)
				triangle.Material = material->Material;
		}
		return triangle;
	}
	Grid::Node InstancedGrid::GetNode(std::uint32_t index) const
	{
		if (index >= m_NodeCount)
			throw std::out_of_range("Invalid Soundwave instance node index");
		const auto stored = GetStoredNodeCount();
		if (index < stored)
		{
			auto node = m_Nodes[index];
			if (node.Left == kInvalidGeometryIndex)
				node.Left = stored + m_Instances[node.First].NodeBegin;
			return node;
		}
		const auto local = index - stored;
		const auto& instance = *std::prev(std::upper_bound(m_Instances.begin(), m_Instances.end(), local,
			[](auto value, const auto& entry) { return value < entry.NodeBegin; }));
		auto node = instance.Value.Geometry->GetNodes()[local - instance.NodeBegin];
		node.Box = TransformBounds(instance, node.Box);
		if (node.Count)
			node.First += instance.TriangleBegin;
		else
		{
			node.Left += stored + instance.NodeBegin;
			node.Right += stored + instance.NodeBegin;
		}
		return node;
	}
	std::uint32_t InstancedGrid::TriangleAtOrder(std::uint32_t index) const
	{
		const auto& instance = FindTriangle(index);
		return instance.Value.Geometry->GetTriangleOrder()[index - instance.TriangleBegin] + instance.TriangleBegin;
	}
	Edge InstancedGrid::GetEdge(std::uint32_t index) const
	{
		if (index >= m_EdgeCount)
			throw std::out_of_range("Invalid Soundwave instance edge index");
		const auto& instance = *std::prev(std::upper_bound(m_Instances.begin(), m_Instances.end(), index,
			[](auto value, const auto& entry) { return value < entry.EdgeBegin; }));
		auto edge = instance.Value.Geometry->GetEdges()[index - instance.EdgeBegin];
		edge.A = Transform(instance, edge.A);
		edge.B = Transform(instance, edge.B);
		edge.Triangle0 += instance.TriangleBegin;
		if (edge.Triangle1 != kInvalidGeometryIndex)
			edge.Triangle1 += instance.TriangleBegin;
		return edge;
	}
	std::array<std::uint32_t, 3> InstancedGrid::GetTriangleEdges(std::uint32_t index) const
	{
		const auto& instance = FindTriangle(index);
		auto edges = instance.Value.Geometry->GetTriangleEdges()[index - instance.TriangleBegin];
		for (auto& edge : edges)
			if (edge != kInvalidGeometryIndex)
				edge += instance.EdgeBegin;
		if (instance.Mirrored)
			std::swap(edges[0], edges[2]);
		return edges;
	}
}
