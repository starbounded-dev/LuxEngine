// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "soundwave/AcousticPath.h"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Lux::Soundwave
{

	struct Triangle
	{
		Position A, B, C;
		// The library treats identity as opaque. The engine maps an entity UUID here.
		std::uint64_t Instance = 0;
		std::uint32_t Surface = 0;
		MaterialTag Material = 0;
		bool Portal = false;
	};

	struct Bounds
	{
		Position Min, Max;
	};

	struct RayHit
	{
		float Distance = 0.0f;
		std::uint32_t TriangleIndex = 0;
		bool Exit = false;
	};

	inline constexpr std::uint32_t kInvalidGeometryIndex = UINT32_MAX;
	struct Edge
	{
		Position A, B;
		std::uint32_t Triangle0 = kInvalidGeometryIndex;
		std::uint32_t Triangle1 = kInvalidGeometryIndex; // absent on a boundary
	};
	struct TopologyStats
	{
		std::uint32_t BoundaryEdges = 0;
		std::uint32_t CreaseEdges = 0;
		std::uint32_t NonManifoldEdges = 0, InertTriangles = 0;
	};

	enum class GeometryError : std::uint8_t
	{
		None, TooManyTriangles, InvalidIdentity, InvalidMaterial, NonFinite, Degenerate, InvalidPolicy
	};

	// Render assets can contain zero-area pole/seam primitives. Retain keeps their IDs,
	// counts exact zero-area triangles and preserves every nonzero surface, however small.
	// Reject preserves the original strict minimum-area validation for reference inputs.
	enum class DegeneratePolicy : std::uint8_t { Reject, Retain };

	// Immutable between Build calls. Build on an owned worker snapshot, then publish the
	// complete value. Concurrent readers use their own hit/scratch storage. The hierarchy is
	// deterministic and uses the same triangle indices that later GPU stages can upload.
	class Grid
	{
		friend class GeometryView;
	public:
		static constexpr std::uint32_t MaximumTriangles = 1u << 20;
		struct Node
		{
			Bounds Box;
			std::uint32_t First = 0;
			std::uint32_t Count = 0; // nonzero: leaf range in GetTriangleOrder()
			std::uint32_t Left = kInvalidGeometryIndex, Right = kInvalidGeometryIndex; // internal children
		};

		// Transactional: an invalid replacement leaves the previous geometry intact.
		GeometryError Build(std::span<const Triangle> triangles, DegeneratePolicy policy = DegeneratePolicy::Reject);
		DegeneratePolicy GetDegeneratePolicy() const { return m_DegeneratePolicy; }
		// Endpoint margin is in metres. A reflection leg excludes its touching endpoints;
		// direct paths use zero. False means invalid input, never "an unobstructed path".
		bool Trace(const Position& from, const Position& to, std::uint64_t ignoredInstance,
			std::vector<RayHit>& hits, float endpointMargin = 0.0f) const;
		// Distance from triangle surfaces, not a point-in-solid classifier. Used for
		// off-surface diffraction candidates before testing their free-space legs.
		[[nodiscard]] bool HasSurfaceClearance(Position point, float radius, std::uint64_t ignoredInstance, bool& clear) const;
		std::span<const Triangle> GetTriangles() const { return m_Triangles; }
		std::span<const Node> GetNodes() const { return m_Nodes; }
		std::span<const std::uint32_t> GetTriangleOrder() const { return m_Order; }
		std::span<const Edge> GetEdges() const { return m_Edges; }
		std::span<const std::array<std::uint32_t, 3>> GetTriangleEdges() const { return m_TriangleEdges; }
		const TopologyStats& GetTopologyStats() const { return m_Topology; }

	private:
		bool AppendTrace(Position from, Position to, std::uint64_t ignoredInstance, std::vector<RayHit>& hits,
			float endpointMargin, std::uint32_t triangleOffset, std::span<const std::uint64_t> excluded) const;
		bool ClearanceInternal(Position point, float radius, std::uint64_t ignoredInstance, bool& clear,
			std::span<const std::uint64_t> excluded) const;
		std::uint32_t BuildNode(std::uint32_t first, std::uint32_t count);
		void BuildEdges();
		std::vector<Triangle> m_Triangles;
		std::vector<Node> m_Nodes;
		std::vector<std::uint32_t> m_Order;
		std::vector<Edge> m_Edges;
		std::vector<std::array<std::uint32_t, 3>> m_TriangleEdges;
		TopologyStats m_Topology;
		DegeneratePolicy m_DegeneratePolicy = DegeneratePolicy::Reject;
	};

}
