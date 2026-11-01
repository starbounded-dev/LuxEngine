// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "soundwave/Grid.h"

namespace Lux::Soundwave
{
	struct SurfaceMaterial
	{
		std::uint32_t Surface = 0;
		MaterialTag Material = 0;
	};
	struct GeometryInstance
	{
		// Borrowed immutable mesh; its owner must outlive this snapshot and all readers.
		const Grid* Geometry = nullptr;
		// Zero preserves each triangle's original identity, material and portal flag.
		// Nonzero replaces identity and portal; sparse sorted overrides replace material.
		std::uint64_t Instance = 0;
		std::array<float, 16> LocalToWorld{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		std::vector<SurfaceMaterial> Materials;
		bool Portal = false;
	};
	enum class InstanceError : std::uint8_t
	{
		None, TooManyInstances, TooManyIndices, InvalidGeometry, InvalidTransform,
		InvalidMaterials, NonFinite, Degenerate
	};

	// Shares local triangles, topology and mesh hierarchies; owns only copied instance
	// descriptors and a top-level hierarchy. Virtual indices expose transformed mesh nodes
	// on demand without allocating per-instance triangle/node/edge arrays. Build validates
	// transformed surfaces on the preparation worker, never during a query or on main.
	class InstancedGrid
	{
	public:
		static constexpr std::uint32_t MaximumInstances = 1u << 20;
		[[nodiscard]] InstanceError Build(std::span<const GeometryInstance> instances);
		std::uint32_t GetInstanceCount() const { return static_cast<std::uint32_t>(m_Instances.size()); }
		std::uint32_t GetStoredNodeCount() const { return static_cast<std::uint32_t>(m_Nodes.size()); }
		std::uint32_t GetTriangleCount() const { return m_TriangleCount; }
		std::uint32_t GetNodeCount() const { return m_NodeCount; }
		std::uint32_t GetEdgeCount() const { return m_EdgeCount; }
		Triangle GetTriangle(std::uint32_t index) const;
		Grid::Node GetNode(std::uint32_t index) const;
		std::uint32_t TriangleAtOrder(std::uint32_t index) const;
		Edge GetEdge(std::uint32_t index) const;
		std::array<std::uint32_t, 3> GetTriangleEdges(std::uint32_t index) const;

	private:
		struct InstanceData
		{
			GeometryInstance Value;
			Bounds Box;
			std::uint32_t TriangleBegin = 0, NodeBegin = 0, EdgeBegin = 0;
			bool Identity = false, Mirrored = false;
		};
		const InstanceData& FindTriangle(std::uint32_t index) const;
		std::uint32_t BuildNode(std::uint32_t first, std::uint32_t count);
		static Position Transform(const InstanceData& instance, Position point);
		static Bounds TransformBounds(const InstanceData& instance, Bounds bounds);
		std::vector<InstanceData> m_Instances;
		std::vector<Grid::Node> m_Nodes;
		std::vector<std::uint32_t> m_Order;
		std::uint32_t m_TriangleCount = 0, m_NodeCount = 0, m_EdgeCount = 0;
	};
}
