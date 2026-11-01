// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include <soundwave/Solve.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cfenv>
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
	void Box(std::vector<Triangle>& triangles, float x = 0)
	{
		constexpr int indices[] = { 0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2,
			1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3 };
		const auto point = [x](int i) { return Position{ x + (i & 1 ? 1.0f : -1.0f), i & 2 ? 1.0f : -1.0f, i & 4 ? 1.0f : -1.0f }; };
		for (int i = 0; i < 36; i += 3)
			triangles.push_back({ point(indices[i]),point(indices[i + 1]),point(indices[i + 2]),1,static_cast<std::uint32_t>(i / 6),0,false });
	}
	Position Transform(const std::array<float,16>& m, Position p)
	{
		return { (m[0]*p.X + m[4]*p.Y) + (m[8]*p.Z + m[12]),
			(m[1]*p.X + m[5]*p.Y) + (m[9]*p.Z + m[13]),
			(m[2]*p.X + m[6]*p.Y) + (m[10]*p.Z + m[14]) };
	}
	bool Same(Position a, Position b)
	{
		return a.X == b.X && a.Y == b.Y && a.Z == b.Z;
	}
	GeometryInstance Placed(const Grid& mesh, std::uint64_t id, Position low, Position high)
	{
		GeometryInstance value;
		value.Geometry = &mesh;
		value.Instance = id;
		value.LocalToWorld[0] = (high.X - low.X) * 0.5f;
		value.LocalToWorld[5] = (high.Y - low.Y) * 0.5f;
		value.LocalToWorld[10] = (high.Z - low.Z) * 0.5f;
		value.LocalToWorld[12] = (high.X + low.X) * 0.5f;
		value.LocalToWorld[13] = (high.Y + low.Y) * 0.5f;
		value.LocalToWorld[14] = (high.Z + low.Z) * 0.5f;
		return value;
	}
	void CheckHierarchy(const GeometryView& view)
	{
		std::vector<std::uint32_t> pending, covered;
		for (auto root : view.GetRoots())
			if (root != kInvalidGeometryIndex)
				pending.push_back(root);
		while (!pending.empty())
		{
			const auto index = pending.back();
			pending.pop_back();
			const auto node = view.GetNode(index);
			if (!node.Count)
			{
				pending.push_back(node.Left);
				if (node.Right != kInvalidGeometryIndex)
					pending.push_back(node.Right);
				continue;
			}
			for (auto i = node.First; i < node.First + node.Count; ++i)
			{
				const auto triangle = view.TriangleAtOrder(i);
				covered.push_back(triangle);
				const auto value = view.GetTriangle(triangle);
				for (auto point : {value.A,value.B,value.C})
					Check(point.X >= node.Box.Min.X && point.X <= node.Box.Max.X &&
						point.Y >= node.Box.Min.Y && point.Y <= node.Box.Max.Y && point.Z >= node.Box.Min.Z && point.Z <= node.Box.Max.Z,
						"transformed hierarchy bounds do not contain their vertices");
				const auto edges = view.GetTriangleEdges(triangle);
				const std::array<Position,3> vertices{value.A,value.B,value.C};
				for (std::uint32_t side = 0; side < 3; ++side)
				{
					if (edges[side] == kInvalidGeometryIndex)
						continue;
					const auto edge = view.GetEdge(edges[side]);
					Check(edge.Triangle0 == triangle || edge.Triangle1 == triangle, "edge adjacency lost instance offset");
					Check((Same(edge.A, vertices[side]) && Same(edge.B, vertices[(side+1)%3])) ||
						(Same(edge.B, vertices[side]) && Same(edge.A, vertices[(side+1)%3])), "mirrored edge order incorrect");
				}
			}
		}
		std::sort(covered.begin(), covered.end());
		Check(covered.size() == view.GetTriangleCount(), "virtual hierarchy lost triangle coverage");
		for (std::uint32_t i = 0; i < covered.size(); ++i)
			Check(covered[i] == i, "virtual hierarchy duplicated a triangle");
	}
}

int main()
{
	using namespace Lux::Soundwave;
	std::vector<Triangle> triangles;
	Box(triangles);
	Grid mesh;
	Check(mesh.Build(triangles) == GeometryError::None, "unit mesh build failed");
	GeometryInstance preserved;
	preserved.Geometry = &mesh;
	InstancedGrid original;
	Check(original.Build({&preserved,1}) == InstanceError::None, "identity passthrough rejected");
	for (std::uint32_t i = 0; i < triangles.size(); ++i)
	{
		const auto triangle = original.GetTriangle(i);
		Check(triangle.Instance == triangles[i].Instance && triangle.Material == triangles[i].Material &&
			triangle.Portal == triangles[i].Portal && Same(triangle.A,triangles[i].A), "identity passthrough changed original data");
	}
	std::vector<GeometryInstance> instances;
	std::vector<Triangle> flattened;
	for (std::uint32_t i = 0; i < 9; ++i)
	{
		auto value = Placed(mesh, 100+i, {static_cast<float>(i)*5-1,-1,-1}, {static_cast<float>(i)*5+1,1,1});
		// Rotation, nonuniform scale, shear and mirrored scale; the winding swap is
		// independently applied to the expanded oracle below.
		value.LocalToWorld[0] = i % 2 ? -0.7f : 0.7f;
		value.LocalToWorld[1] = 0.3f;
		value.LocalToWorld[4] = 0.2f;
		value.LocalToWorld[5] = 1.3f;
		value.LocalToWorld[8] = 0.1f;
		value.LocalToWorld[10] = 0.9f;
		value.Materials = {{0,2},{2,3}};
		for (auto triangle : triangles)
		{
			triangle.A = Transform(value.LocalToWorld, triangle.A);
			triangle.B = Transform(value.LocalToWorld, triangle.B);
			triangle.C = Transform(value.LocalToWorld, triangle.C);
			if (i % 2)
				std::swap(triangle.B, triangle.C);
			triangle.Instance = value.Instance;
			triangle.Material = triangle.Surface == 0 ? 2 : (triangle.Surface == 2 ? 3 : 0);
			flattened.push_back(triangle);
		}
		instances.push_back(std::move(value));
	}
	InstancedGrid fixed, moving;
	Check(fixed.Build({instances.data(),4}) == InstanceError::None &&
		moving.Build({instances.data()+4,instances.size()-4}) == InstanceError::None, "transformed instance build failed");
	GeometryView view(fixed, &moving);
	Grid expanded;
	Check(expanded.Build(flattened) == GeometryError::None, "expanded oracle failed");
	CheckHierarchy(view);
	std::vector<RayHit> hits, expected;
	DirectScratch scratch;
	Path actualPath, expectedPath;
	Settings settings;
	settings.Strength = 0.003f;
	for (float y : {-0.75f,0.0f,0.31f,0.9f})
		for (float z : {-0.7f,0.0f,0.2f})
		{
			const Position from{-3,y,z}, to{44,y,z};
			Check(view.Trace(from,to,0,hits) && expanded.Trace(from,to,0,expected) && hits.size() == expected.size(), "instance trace differs from expanded oracle");
			for (std::size_t i = 0; i < hits.size(); ++i)
				Check(hits[i].Distance == expected[i].Distance && hits[i].TriangleIndex == expected[i].TriangleIndex &&
					hits[i].Exit == expected[i].Exit, "instance world-space intersection changed");
			Check(EvaluateDirect(view,{},DefaultMaterials(),settings,from,to,0,scratch,actualPath) == EvaluationError::None &&
				EvaluateDirect(expanded,{},DefaultMaterials(),settings,from,to,0,scratch,expectedPath) == EvaluationError::None &&
				std::bit_cast<std::uint32_t>(actualPath.GainLF()) == std::bit_cast<std::uint32_t>(expectedPath.GainLF()) &&
				std::bit_cast<std::uint32_t>(actualPath.GainHF()) == std::bit_cast<std::uint32_t>(expectedPath.GainHF()), "instance direct loss is not bitwise equal to expanded geometry");
		}
	for (std::uint32_t i = 0; i < 36; ++i)
	{
		const auto point = Transform(instances[i/4].LocalToWorld, {1, static_cast<float>(i%4)*0.2f,0});
		bool clear = true, expectedClear = true;
		Check(view.HasSurfaceClearance(point,0.001f,0,clear) && expanded.HasSurfaceClearance(point,0.001f,0,expectedClear) && clear == expectedClear && !clear,
			"nonuniform surface clearance differs from expanded geometry");
	}
	const std::array<std::uint64_t,2> exclusions{100,104};
	GeometryView masked(fixed,&moving,exclusions);
	Check(masked.Trace({-3,0,0},{44,0,0},101,hits), "masked instance trace failed");
	for (const auto& hit : hits)
		Check(masked.GetTriangle(hit.TriangleIndex).Instance != 100 && masked.GetTriangle(hit.TriangleIndex).Instance != 101 &&
			masked.GetTriangle(hit.TriangleIndex).Instance != 104, "instance deletion/source mask failed");

	// Malformed replacements cannot discard a previously valid world.
	GeometryInstance invalid = instances[0];
	invalid.LocalToWorld[0] = invalid.LocalToWorld[1] = 0;
	Check(fixed.Build({&invalid,1}) == InstanceError::InvalidTransform && fixed.GetInstanceCount() == 4, "singular replacement published");
	invalid = instances[0];
	invalid.Materials = {{2,0},{1,0}};
	Check(fixed.Build({&invalid,1}) == InstanceError::InvalidMaterials, "unsorted material map accepted");
	invalid.Materials = {{1,0},{1,2}};
	Check(fixed.Build({&invalid,1}) == InstanceError::InvalidMaterials, "duplicate surface map accepted");
	invalid = instances[0];
	invalid.LocalToWorld[12] = 1e20f;
	Check(fixed.Build({&invalid,1}) == InstanceError::Degenerate, "float-collapsed world surfaces accepted");
	invalid.LocalToWorld[12] = std::numeric_limits<float>::infinity();
	Check(fixed.Build({&invalid,1}) == InstanceError::NonFinite, "infinite transform accepted");
	invalid = instances[0];
	invalid.LocalToWorld[3] = 0.001f;
	Check(fixed.Build({&invalid,1}) == InstanceError::InvalidTransform, "projective transform accepted");
	invalid.Geometry = nullptr;
	Check(fixed.Build({&invalid,1}) == InstanceError::InvalidGeometry, "null mesh accepted");

	// A million logical triangles share one 6,144-triangle mesh and 359 stored nodes.
	triangles.clear();
	for (std::uint32_t i = 0; i < 512; ++i)
		Box(triangles, static_cast<float>(i)*4);
	Grid repeated;
	Check(repeated.Build(triangles) == GeometryError::None, "repeated mesh build failed");
	instances.clear();
	for (std::uint32_t i = 0; i < 180; ++i)
	{
		GeometryInstance value;
		value.Geometry = &repeated;
		value.Instance = 1000+i;
		value.LocalToWorld[13] = static_cast<float>(i)*4;
		instances.push_back(std::move(value));
	}
	InstancedGrid large;
	Check(large.Build(instances) == InstanceError::None && large.GetTriangleCount() > Grid::MaximumTriangles &&
		large.GetStoredNodeCount() == 359 && repeated.GetTriangles().size() == 6144, "shared instances still hit aggregate triangle cap");
	GeometryView largeView(large);
	std::atomic<bool> readersOK{true};
	std::vector<std::thread> readers;
	for (std::uint32_t worker = 0; worker < 4; ++worker)
		readers.emplace_back([&,worker]
		{
			for (std::uint32_t sample = 0; sample < 32; ++sample)
			{
				std::vector<RayHit> local;
				const auto owner = worker*40+sample;
				const float y = static_cast<float>(owner)*4;
				if (!largeView.Trace({-3,y,0},{3,y,0},0,local) || local.size() != 4)
					readersOK = false;
				for (const auto& hit : local)
					if (largeView.GetTriangle(hit.TriangleIndex).Instance != 1000+owner)
						readersOK = false;
			}
		});
	for (auto& reader : readers)
		reader.join();
	Check(readersOK, "concurrent readers crossed instance transforms/identity");

	// Independent mesh instances form the enclosure, wall sections and moving door.
	// An off-axis opening must help; closing it must not leave seam diffraction.
	instances = { Placed(mesh,1,{-3,-2,-3},{3,2,3}), Placed(mesh,2,{-0.1f,-2,-3},{0.1f,2,-0.5f}),
		Placed(mesh,3,{-0.1f,-2,0.5f},{0.1f,2,3}), Placed(mesh,4,{-0.1f,1,-0.5f},{0.1f,2,0.5f}) };
	InstancedGrid room, door;
	Check(room.Build(instances) == InstanceError::None, "instanced room invalid");
	GeometryInstance shutter = Placed(mesh,99,{-0.1f,-2,-0.5f},{0.1f,1,0.5f});
	shutter.Portal = true;
	Check(door.Build({&shutter,1}) == InstanceError::None, "instanced door invalid");
	settings = {};
	settings.ReflectionDepth = 0;
	auto materials = DefaultMaterials();
	materials[0].TransmissionLF = materials[0].TransmissionHF = 0.001f;
	materials[0].FlatTransmissionLF = materials[0].FlatTransmissionHF = 1;
	SolveScratch solveScratch;
	SolveResult result;
	Check(Solve(GeometryView(room),{},materials,settings,{1,0,-1},{-1,0,-1},0,solveScratch,result) == EvaluationError::None &&
		result.HasAudibleOpening && result.OcclusionGainLF > 5*result.Direct.GainLF(), "shared-mesh doorway remained inaudible");
	PortalBox portal;
	portal.Instance = 99;
	portal.WorldToBox = {10,0,0,0, 0,2.0f/3.0f,0,0, 0,0,2,0, 0,1.0f/3.0f,0,1};
	Check(Solve(GeometryView(room,&door),{&portal,1},materials,settings,{1,0,-1},{-1,0,-1},0,solveScratch,result) == EvaluationError::None &&
		!result.HasAudibleOpening && result.Diagnostics.OpeningPaths == 0, "closed instanced door leaked a diffraction path");
	const std::array<std::uint64_t,1> removedDoor{99};
	Check(Solve(GeometryView(room,&door,removedDoor),{&portal,1},materials,settings,{1,0,-1},{-1,0,-1},0,solveScratch,result) == EvaluationError::None &&
		result.HasAudibleOpening, "removed instanced door still blocks opening");
	instances = {Placed(mesh,2,{-0.1f,-2,-2},{0.1f,2,0})};
	Check(room.Build(instances) == InstanceError::None &&
		Solve(GeometryView(room),{},materials,settings,{1,0,-0.5f},{-1,0,-0.5f},0,solveScratch,result) == EvaluationError::None &&
		result.HasAudibleOpening && result.AudibleOpening.Diffractions == 2 && result.OcclusionGainLF > 0.01f,
		"instanced thick corner did not reach around both edges");
	Grid floor;
	const std::array<Triangle,2> floorTriangles{{{{-5,0,-5},{5,0,5},{5,0,-5},1,0,0,false},
		{{-5,0,-5},{-5,0,5},{5,0,5},1,0,0,false}}};
	Check(floor.Build(floorTriangles) == GeometryError::None, "local reflection floor invalid");
	preserved.Geometry = &floor;
	preserved.Instance = 500;
	preserved.LocalToWorld[13] = 0.7f;
	Check(room.Build({&preserved,1}) == InstanceError::None, "translated reflection floor invalid");
	settings.ReflectionDepth = settings.MaxBounces = 1;
	settings.OpeningGridSize = 1;
	materials[0].Scattering = 0;
	materials[0].AbsorptionLF = 0.2f;
	Check(Solve(GeometryView(room),{},materials,settings,{-1,1.7f,0},{1,1.7f,0},0,solveScratch,result) == EvaluationError::None &&
		result.Diagnostics.ReflectionPaths == 1 && std::abs(result.Indirect[0].Points[1].Y - 0.7f) < 1e-5f &&
		std::abs(result.Indirect[0].GainLF - std::sqrt(0.4f)) < 1e-5f, "translated instance reflection changed analytic solution");
	// Render-asset pole primitives retain their original triangle IDs. Exact zero
	// area is inert; a valid small face is kept until world rounding really collapses it.
	{
		const std::array<Triangle,3> primitives{{
			{{0,0,0},{1,0,0},{0,1e-14f,0},1,0,0,false},
			{{0,0,0},{0,0,0},{0,0,0},1,0,0,false},
			{{0,-2,-2},{0,2,-2},{0,0,2},1,0,0,false}}};
		Grid rendered;
		Check(rendered.Build(primitives) == GeometryError::Degenerate, "strict reference minimum-area policy changed");
		Check(rendered.Build(primitives,DegeneratePolicy::Retain) == GeometryError::None &&
			rendered.GetTriangles().size() == 3 && rendered.GetTopologyStats().InertTriangles == 1,
			"render primitives were dropped or zero-area count is wrong");
		Check(rendered.Trace({0.25f,2.5e-15f,1},{0.25f,2.5e-15f,-1},0,hits) &&
			hits.size() == 1 && hits[0].TriangleIndex == 0 && hits[0].Distance == 1,
			"small valid render face became transparent");
		GeometryInstance translated;
		translated.Geometry = &rendered;
		translated.Instance = 700;
		translated.LocalToWorld[13] = 1;
		InstancedGrid renderWorld;
		Check(renderWorld.Build({&translated,1}) == InstanceError::None && renderWorld.GetTriangleCount() == 3 &&
			renderWorld.GetInertTriangleCount() == 2, "world-collapsed primitive count wrong");
		GeometryView renderView(renderWorld);
		std::feclearexcept(FE_ALL_EXCEPT);
		bool clear = false;
		Check(renderView.Trace({-2,1.2f,0},{2,1.2f,0},0,hits) && hits.size() == 1 && hits[0].TriangleIndex == 2,
			"valid render surface lost its ID or became transparent");
		Check(renderView.HasSurfaceClearance({0.25f,1,0},0.001f,0,clear) && clear,
			"collapsed render primitive obstructed clearance");
		Check(std::fetestexcept(FE_INVALID|FE_DIVBYZERO) == 0, "inert primitive raised an invalid floating operation");
		settings.ReflectionDepth = 1;
		settings.MaxBounces = 1;
		settings.OpeningGridSize = 1;
		Check(Solve(renderView,{},materials,settings,{-1,1.2f,0},{1,1.2f,0},0,solveScratch,result) == EvaluationError::None &&
			result.Indirect.empty(), "inert primitive fabricated an indirect path");
		GeometryInstance huge;
		huge.Geometry = &mesh;
		huge.Instance = 780;
		huge.LocalToWorld[0] = std::numeric_limits<float>::max();
		std::feclearexcept(FE_ALL_EXCEPT);
		Check(renderWorld.Build({&huge,1}) == InstanceError::NonFinite && renderWorld.GetTriangleCount() == 3 &&
			std::fetestexcept(FE_OVERFLOW|FE_INVALID|FE_DIVBYZERO) == 0,
			"invalid finite transform trapped before transactional rejection");
		Check(rendered.Build(primitives,static_cast<DegeneratePolicy>(255)) == GeometryError::InvalidPolicy &&
			rendered.GetTriangles().size() == 3, "invalid render primitive policy replaced the previous grid");
	}
	Check(large.Build({}) == InstanceError::None && large.GetNodeCount() == 0 && large.GetTriangleCount() == 0, "empty instance replacement retained data");
	std::cout << "Soundwave instance checks passed (1,105,920 logical triangles, shared storage).\n";
}
