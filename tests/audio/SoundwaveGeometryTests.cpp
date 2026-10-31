// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include <soundwave/Grid.h>
#include <soundwave/Settings.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
	using namespace Lux::Soundwave;

	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	void Box(std::vector<Triangle>& triangles, std::uint64_t instance, float x)
	{
		// Outward-wound closed cube. Each ray through its face diagonal reports two
		// triangles per face; deduplication belongs to the crossing walk, not this trace.
		constexpr int indices[] = { 0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2,
			1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3 };
		const auto position = [x](int i)
		{
			return Position{ x + (i & 1 ? 1.0f : -1.0f), i & 2 ? 1.0f : -1.0f, i & 4 ? 1.0f : -1.0f };
		};
		for (int i = 0; i < 36; i += 3)
			triangles.push_back({ position(indices[i]), position(indices[i + 1]), position(indices[i + 2]), instance, 7, 1, true });
	}
}

int main()
{
	using namespace Lux::Soundwave;
	Settings settings;
	Check(settings.Validate(), "default solve settings are invalid");
	settings.ReflectionDepth = 0;
	settings.OpeningGridSize = 1;
	settings.MaxBounces = 1;
	Check(settings.Validate(), "direct-only comparison settings are invalid");
	settings.ReflectionDepth = 2;
	Check(!settings.Validate(), "reflection depth exceeds bounce work limit");
	settings = {};
	settings.CandidateBudget = 0;
	Check(!settings.Validate(), "zero candidate budget accepted");
	settings = {};
	settings.OpeningGridSize = 33;
	Check(!settings.Validate(), "unbounded opening sampling accepted");
	settings = {};
	settings.Absorption = std::numeric_limits<float>::quiet_NaN();
	Check(!settings.Validate(), "non-finite absorption accepted");
	settings.Absorption = 1.01f;
	Check(!settings.Validate(), "invalid absorption fraction accepted");

	Grid grid;
	std::vector<Triangle> triangles;
	std::vector<RayHit> hits;
	Check(grid.Build(triangles) == GeometryError::None && grid.Trace({ -2,0,0 }, { 2,0,0 }, 0, hits) && hits.empty(),
		"empty geometry does not trace cleanly");
	Box(triangles, 42, 0);
	Check(grid.Build(triangles) == GeometryError::None, "closed cube rejected");
	Check(grid.Trace({ -2,0,0 }, { 2,0,0 }, 0, hits) && hits.size() == 4, "shared-edge triangle crossings missing");
	for (std::size_t i = 0; i < hits.size(); ++i)
	{
		Check(hits[i].Distance == (i < 2 ? 1.0f : 3.0f), "analytic cube hit distance differs");
		Check(hits[i].Exit == (i >= 2), "outward-wound solid entry/exit reversed");
		const auto& triangle = grid.GetTriangles()[hits[i].TriangleIndex];
		Check(triangle.Instance == 42 && triangle.Surface == 7 && triangle.Material == 1 && triangle.Portal,
			"geometry identity or authored data lost");
	}
	const auto capacity = hits.capacity();
	Check(grid.Trace({ 2,0,0 }, { -2,0,0 }, 0, hits) && hits.size() == 4 && !hits[0].Exit && hits.back().Exit,
		"reverse ray does not report solid entries/exits");
	Check(grid.Trace({ -2,0,0 }, { 2,0,0 }, 42, hits) && hits.empty() && hits.capacity() >= capacity,
		"own-instance exclusion or hit storage reuse failed");
	Check(grid.Trace({ 0,0,0 }, { 2,0,0 }, 0, hits) && hits.size() == 2 && hits.front().Exit && hits.front().Distance == 1,
		"inside-solid ray lost its unmatched exit");
	Check(grid.Trace({ -1,0,0 }, { 2,0,0 }, 0, hits) && hits.size() == 2 && hits.front().Distance == 2,
		"segment start was incorrectly counted as a crossing");
	Check(grid.Trace({ -1,0,0 }, { 2,0,0 }, 0, hits, 1) && hits.empty(), "endpoint margin was not applied in metres");
	Check(grid.Trace({ -2,2,0 }, { 2,2,0 }, 0, hits) && hits.empty(), "parallel ray outside box hit a surface");
	Check(grid.Trace({ 0,0,0 }, { 0,0,0 }, 0, hits) && hits.empty(), "zero-length ray failed");
	Check(!grid.Trace({ std::numeric_limits<float>::infinity(),0,0 }, {}, 0, hits) && hits.empty(),
		"invalid ray reported an unobstructed success");
	Check(!grid.Trace({}, { 2,0,0 }, 0, hits, -1), "negative endpoint margin accepted");
	bool clear = true;
	Check(grid.HasSurfaceClearance({1,0,0}, 0.001f, 0, clear) && !clear,
		"opening candidate on another solid's face was treated as free space");
	Check(grid.HasSurfaceClearance({1.01f,0,0}, 0.001f, 0, clear) && clear, "off-surface clearance query failed");
	Check(grid.HasSurfaceClearance({1.0005f,1.0005f,0}, 0.001f, 0, clear) && !clear, "near-edge surface distance ignored");
	Check(grid.HasSurfaceClearance({1,0,0}, 0.001f, 42, clear) && clear, "surface clearance ignored source exclusion");
	Check(!grid.HasSurfaceClearance({}, -1, 0, clear) && !clear, "invalid clearance query returned free space");

	// Failed builds retain a usable previous snapshot, including when the input aliases it.
	Check(grid.Build(grid.GetTriangles()) == GeometryError::None, "self-snapshot rebuild invalidated its own input");
	const auto original = triangles[0];
	triangles[0].Instance = 0;
	Check(grid.Build(triangles) == GeometryError::InvalidIdentity, "missing stable identity accepted");
	triangles[0] = original;
	triangles[0].Material = 255;
	Check(grid.Build(triangles) == GeometryError::InvalidMaterial, "invalid material accepted");
	triangles[0] = original;
	triangles[0].A.X = std::numeric_limits<float>::quiet_NaN();
	Check(grid.Build(triangles) == GeometryError::NonFinite, "non-finite geometry accepted");
	triangles[0] = original;
	triangles[0].B = triangles[0].A;
	Check(grid.Build(triangles) == GeometryError::Degenerate, "zero-area triangle accepted");
	Check(grid.Trace({ -2,0,0 }, { 2,0,0 }, 0, hits) && hits.size() == 4, "failed replacement discarded the last geometry");

	// Hundreds of analytically positioned solids exercise internal nodes, stack traversal,
	// sorting and axis-parallel slab tests. This does not duplicate the triangle intersector.
	triangles.clear();
	for (std::uint32_t i = 0; i < 256; ++i)
		Box(triangles, i + 1, static_cast<float>(i * 4));
	Check(grid.Build(triangles) == GeometryError::None && grid.GetNodes().size() > 100, "hierarchy not constructed");
	Check(grid.Trace({ -2,0,0 }, { 1023,0,0 }, 0, hits) && hits.size() == 1024, "hierarchy missed a solid");
	for (std::size_t i = 0; i < hits.size(); ++i)
		Check(hits[i].Distance == static_cast<float>((i / 4) * 4 + (i % 4 < 2 ? 1 : 3)), "hierarchy changed analytic hit order");
	std::vector<std::uint32_t> order(grid.GetTriangleOrder().begin(), grid.GetTriangleOrder().end());
	std::sort(order.begin(), order.end());
	for (std::size_t i = 0; i < order.size(); ++i)
		Check(order[i] == i, "hierarchy duplicated or lost a triangle index");
	Grid repeat;
	Check(repeat.Build(triangles) == GeometryError::None &&
		std::equal(repeat.GetTriangleOrder().begin(), repeat.GetTriangleOrder().end(), grid.GetTriangleOrder().begin()),
		"identical snapshots produced different hierarchy order");

	std::atomic<bool> readersOK{ true };
	std::vector<std::thread> readers;
	for (int worker = 0; worker < 4; ++worker)
	{
		readers.emplace_back([&, worker]
		{
			std::vector<RayHit> local;
			for (int sample = 0; sample < 64; ++sample)
			{
				const float y = 0.1f + static_cast<float>(worker) * 0.1f;
				if (!grid.Trace({ -2,y,0 }, { 1023,y,0 }, 0, local) || local.size() != 512 ||
					local.front().Distance != 1 || local.back().Distance != 1023)
					readersOK.store(false);
			}
		});
	}
	for (auto& reader : readers)
		reader.join();
	Check(readersOK.load(), "concurrent immutable geometry readers disagreed");
	// Finite float inputs can overflow a float-only intersection numerator even
	// though the hit distance is representable. Such geometry must not leak sound.
	triangles = { { { 1e15f,-1e15f,-1e15f }, { 1e15f,1e15f,-1e15f }, { 1e15f,0,1e15f }, 1,0,0,false } };
	Check(grid.Build(triangles) == GeometryError::None && grid.Trace({}, { 2e15f,0,0 }, 0, hits) && hits.size() == 1 &&
		hits[0].Distance == 1e15f, "large finite triangle silently became transparent");
	Check(grid.Build({}) == GeometryError::None && grid.GetNodes().empty() && grid.GetTriangles().empty(),
		"empty replacement retained stale geometry");
	std::cout << "PASS: deterministic hierarchy, analytic triangle hits, solid winding, source exclusion, transactional geometry, settings and concurrent readers\n";
}
