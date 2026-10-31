// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "soundwave/AcousticPath.h"
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

	enum class GeometryError : std::uint8_t
	{
		None, TooManyTriangles, InvalidIdentity, InvalidMaterial, NonFinite, Degenerate
	};

	// Immutable between Build calls. Build on an owned worker snapshot, then publish the
	// complete value. Concurrent readers use their own hit/scratch storage. The hierarchy is
	// deterministic and uses the same triangle indices that later GPU stages can upload.
	class Grid
	{
	public:
		static constexpr std::uint32_t MaximumTriangles = 1u << 20;
		struct Node
		{
			Bounds Box;
			std::uint32_t First = 0;
			std::uint32_t Count = 0; // nonzero: leaf range in GetTriangleOrder()
			std::uint32_t Right = 0; // internal: left is the next node
		};

		// Transactional: an invalid replacement leaves the previous geometry intact.
		GeometryError Build(std::span<const Triangle> triangles);
		// Endpoint margin is in metres. A reflection leg excludes its touching endpoints;
		// direct paths use zero. False means invalid input, never "an unobstructed path".
		bool Trace(const Position& from, const Position& to, std::uint64_t ignoredInstance,
			std::vector<RayHit>& hits, float endpointMargin = 0.0f) const;
		std::span<const Triangle> GetTriangles() const { return m_Triangles; }
		std::span<const Node> GetNodes() const { return m_Nodes; }
		std::span<const std::uint32_t> GetTriangleOrder() const { return m_Order; }

	private:
		std::uint32_t BuildNode(std::uint32_t first, std::uint32_t count);
		std::vector<Triangle> m_Triangles;
		std::vector<Node> m_Nodes;
		std::vector<std::uint32_t> m_Order;
	};

}
