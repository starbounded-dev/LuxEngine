// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include <soundwave/Solve.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>

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
	void Near(float actual, float expected, const char* message)
	{
		if (std::abs(actual - expected) > 1e-4f)
		{
			std::cerr << "actual=" << actual << " expected=" << expected << '\n';
			Check(false, message);
		}
	}
	void Box(std::vector<Triangle>& triangles, Position low, Position high, std::uint64_t instance, bool portal = false)
	{
		constexpr int indices[] = { 0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2,
			1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3 };
		const auto point = [&](int i) { return Position{ i & 1 ? high.X : low.X, i & 2 ? high.Y : low.Y, i & 4 ? high.Z : low.Z }; };
		for (int i = 0; i < 36; i += 3)
			triangles.push_back({ point(indices[i]),point(indices[i + 1]),point(indices[i + 2]),instance,0,0,portal });
	}
	void Floor(std::vector<Triangle>& triangles)
	{
		triangles.push_back({ {-5,0,-5},{5,0,5},{5,0,-5},1,0,0,false });
		triangles.push_back({ {-5,0,-5},{-5,0,5},{5,0,5},1,0,0,false });
	}
}

int main()
{
	using namespace Lux::Soundwave;
	Grid grid;
	SolveScratch scratch;
	SolveResult result;
	Settings settings;
	settings.ReflectionDepth = 1;
	settings.MaxBounces = 1;
	settings.OpeningGridSize = 1;
	MaterialTable materials = DefaultMaterials();
	materials[0].Scattering = 0;
	materials[0].AbsorptionLF = 0.2f;
	materials[0].AbsorptionHF = 0.75f;
	materials[0].TransmissionLF = materials[0].TransmissionHF = 0.001f;
	materials[0].FlatTransmissionLF = materials[0].FlatTransmissionHF = 1;
	std::vector<Triangle> geometry;
	Floor(geometry);
	Check(grid.Build(geometry) == GeometryError::None, "floor geometry invalid");
	Check(grid.GetEdges().size() == 4 && grid.GetTopologyStats().BoundaryEdges == 4 && grid.GetTopologyStats().CreaseEdges == 0,
		"coplanar floor diagonal was cached as a diffracting edge");
	Check(Solve(grid, {}, materials, settings, {-1,1,0}, {1,1,0}, 0, scratch, result) == EvaluationError::None &&
		result.Diagnostics.ReflectionPaths == 1, "analytic first reflection missing or counted twice on shared diagonal");
	const auto reflection = result.Indirect[0];
	Check(reflection.Reflections == 1 && reflection.Diffractions == 0 && reflection.PointCount == 3, "reflection path topology wrong");
	Near(reflection.Points[1].X, 0, "reflection point X differs from image source");
	Near(reflection.Points[1].Y, 0, "reflection point is not on floor");
	Near(reflection.Distance, std::sqrt(8.0f), "reflection length wrong");
	Near(reflection.GainLF, std::sqrt(0.4f), "reflection LF energy/relative spreading wrong");
	Near(reflection.GainHF, std::sqrt(0.125f), "reflection HF absorption wrong");
	Check(result.OcclusionGainLF == 1 && result.OcclusionGainHF == 1, "reflection multiplied the direct dry signal");
	settings.Absorption = 0;
	Check(Solve(grid, {}, materials, settings, {-1,1,0}, {1,1,0}, 0, scratch, result) == EvaluationError::None, "zero absorption solve failed");
	Near(result.Indirect[0].GainLF, std::sqrt(0.5f), "authored absorption control ignored");
	settings.Absorption = 1;
	materials[0].Scattering = 0.75f;
	Check(Solve(grid, {}, materials, settings, {-1,1,0}, {1,1,0}, 0, scratch, result) == EvaluationError::None, "scattering solve failed");
	Near(result.Indirect[0].GainLF, reflection.GainLF * 0.5f, "specular energy was not split by scattering");
	materials[0].Scattering = 0;

	PortalBox legBlocker;
	legBlocker.Instance = 9;
	legBlocker.WorldToBox = { 10,0,0,0, 0,10,0,0, 0,0,2,0, 5,-5,0,1 };
	Check(Solve(grid, {&legBlocker,1}, materials, settings, {-1,1,0}, {1,1,0}, 0, scratch, result) == EvaluationError::None &&
		result.Diagnostics.ReflectionPaths == 0, "a blocked reflection leg reached the listener");
	Check(Solve(grid, {}, materials, settings, {-1,1,0}, {1,-1,0}, 0, scratch, result) == EvaluationError::None &&
		result.Diagnostics.ReflectionPaths == 0 && result.OcclusionGainLF <= 0.001001f, "mirror path transmitted through its own plane");

	geometry.clear();
	Box(geometry, {-2,-2,-2}, {2,2,2}, 1);
	Check(grid.Build(geometry) == GeometryError::None && grid.GetEdges().size() == 12 &&
		grid.GetTopologyStats().CreaseEdges == 12, "cube crease topology wrong");
	settings.ReflectionDepth = settings.MaxBounces = 3;
	settings.CandidateBudget = 65536;
	settings.PathBudget = 4096;
	Check(Solve(grid, {}, materials, settings, {-0.5f,0,0}, {0.5f,0,0}, 0, scratch, result) == EvaluationError::None,
		"multi-bounce image-source solve failed");
	Check(std::any_of(result.Indirect.begin(), result.Indirect.end(), [](const auto& path) { return path.Reflections == 3; }),
		"reflection traversal silently capped depth at two");
	settings.PathBudget = 1;
	Check(Solve(grid, {}, materials, settings, {-0.5f,0,0}, {0.5f,0,0}, 0, scratch, result) == EvaluationError::None &&
		result.Indirect.size() == 1 && result.Diagnostics.PathsLimited, "path storage limit was silent or ignored");

	geometry.clear();
	Box(geometry, {-0.1f,-2,-2}, {0.1f,2,0}, 2);
	Check(grid.Build(geometry) == GeometryError::None, "thick corner geometry invalid");
	settings = {};
	settings.ReflectionDepth = 0;
	settings.MaxBounces = 2;
	Check(Solve(grid, {}, materials, settings, {1,0,-0.5f}, {-1,0,-0.5f}, 0, scratch, result) == EvaluationError::None,
		"corner solve failed");
	Check(result.Direct.GainLF() <= 0.001001f && result.OcclusionGainLF > 0.01f && result.HasAudibleOpening,
		"thick corner remained inaudible when the direct ray was blocked");
	Check(result.AudibleOpening.Diffractions == 2 && result.OcclusionGainHF < result.OcclusionGainLF,
		"corner route did not cross both edges or lacked frequency-dependent diffraction");
	for (const auto& path : result.Indirect)
	{
		Check(path.Diffractions <= settings.MaxBounces && path.PointCount == path.Diffractions + 2, "opening interaction limit ignored");
		for (std::uint32_t i = 1; i < path.PointCount; ++i)
		{
			std::vector<RayHit> hits;
			Check(grid.Trace(path.Points[i - 1], path.Points[i], 0, hits) && hits.empty(), "accepted corner path crossed a wall");
		}
	}
	const auto cornerResult = result.OcclusionGainLF;
	const auto points = result.AudibleOpening.Points;
	Check(Solve(grid, {}, materials, settings, {1,0,-0.5f}, {-1,0,-0.5f}, 0, scratch, result) == EvaluationError::None &&
		std::bit_cast<std::uint32_t>(result.OcclusionGainLF) == std::bit_cast<std::uint32_t>(cornerResult), "repeated solve changed its reduction/order");
	for (std::uint32_t i = 0; i < result.AudibleOpening.PointCount; ++i)
		Check(points[i].X == result.AudibleOpening.Points[i].X && points[i].Z == result.AudibleOpening.Points[i].Z,
			"repeated solve selected a different visible route");
	settings.OpeningGridSize = 1;
	settings.MaxBounces = 1;
	Check(Solve(grid, {}, materials, settings, {1,0,-0.5f}, {-1,0,-0.5f}, 0, scratch, result) == EvaluationError::None &&
		result.Indirect.empty() && result.OcclusionGainLF == result.Direct.GainLF() && result.Diagnostics.PreparationNodes == 0,
		"direct-only comparison mode changed gain or performed candidate preparation");

	// A sealed enclosure prevents a route around the ends of this internal wall. The
	// doorway is off the direct ray, so audibility must come from a genuine opening path.
	geometry.clear();
	Box(geometry, {-3,-2,-3}, {3,2,3}, 1);
	Box(geometry, {-0.1f,-2,-3}, {0.1f,2,-0.5f}, 2);
	Box(geometry, {-0.1f,-2,0.5f}, {0.1f,2,3}, 3);
	Box(geometry, {-0.1f,1,-0.5f}, {0.1f,2,0.5f}, 4);
	Check(grid.Build(geometry) == GeometryError::None, "doorway geometry invalid");
	settings = {};
	settings.ReflectionDepth = 0;
	Check(Solve(grid, {}, materials, settings, {1,0,-1}, {-1,0,-1}, 0, scratch, result) == EvaluationError::None &&
		result.OcclusionGainLF > result.Direct.GainLF() * 5 && result.HasAudibleOpening, "off-axis doorway did not become audible");
	Grid staticRoom;
	Check(staticRoom.Build(geometry) == GeometryError::None, "static room capture failed");
	Box(geometry, {-0.1f,-2,-0.5f}, {0.1f,1,0.5f}, 99, true);
	Check(grid.Build(geometry) == GeometryError::None, "closed door geometry invalid");
	PortalBox door;
	door.Instance = 99;
	door.WorldToBox = { 10,0,0,0, 0,2.0f/3.0f,0,0, 0,0,2,0, 0,1.0f/3.0f,0,1 };
	const auto closedError = Solve(grid, {&door,1}, materials, settings, {1,0,-1}, {-1,0,-1}, 0, scratch, result);
	if (result.HasAudibleOpening || result.Diagnostics.OpeningPaths)
	{
		std::cerr.precision(9);
		std::cerr << "closed direct=" << result.Direct.GainLF() << " audible=" << result.OcclusionGainLF << " paths=" << result.Diagnostics.OpeningPaths << '\n';
		for (std::uint32_t i = 0; i < result.AudibleOpening.PointCount; ++i)
		{
			const auto point = result.AudibleOpening.Points[i];
			std::cerr << "point " << i << ": " << point.X << ' ' << point.Y << ' ' << point.Z << '\n';
		}
	}
	Check(closedError == EvaluationError::None &&
		result.Diagnostics.OpeningPaths == 0 && !result.HasAudibleOpening && result.OcclusionGainLF <= 0.001001f,
		"sealed closed door leaked through an internal edge or seam");

	Grid movingDoor;
	std::vector<Triangle> doorTriangles;
	Box(doorTriangles, {-0.1f,-2,-0.5f}, {0.1f,1,0.5f}, 99, true);
	Check(movingDoor.Build(doorTriangles) == GeometryError::None, "moving door snapshot invalid");
	GeometryView closedRoom(staticRoom, &movingDoor);
	Check(Solve(closedRoom, {&door,1}, materials, settings, {1,0,-1}, {-1,0,-1}, 0, scratch, result) == EvaluationError::None &&
		!result.HasAudibleOpening && result.OcclusionGainLF <= 0.001001f,
		"separate moving door snapshot leaked through static frame edges");
	const std::array<std::uint64_t, 1> deletedDoor{99};
	GeometryView openRoom(staticRoom, &movingDoor, deletedDoor);
	Check(Solve(openRoom, {&door,1}, materials, settings, {1,0,-1}, {-1,0,-1}, 0, scratch, result) == EvaluationError::None &&
		result.HasAudibleOpening && result.OcclusionGainLF > 0.005f,
		"removing door did not restore audible route across both snapshots");
	Check(movingDoor.Build({}) == GeometryError::None &&
		Solve(GeometryView(staticRoom, &movingDoor), {}, materials, settings, {1,0,-1}, {-1,0,-1}, 0, scratch, result) == EvaluationError::None &&
		result.HasAudibleOpening, "replaced moving snapshot left a stale analytic shutter or topology");

	settings.CandidateBudget = 4;
	Check(Solve(grid, {&door,1}, materials, settings, {1,0,-1}, {-1,0,-1}, 0, scratch, result) == EvaluationError::None &&
		result.Diagnostics.Work <= 4 && result.Diagnostics.WorkLimited && result.OcclusionGainLF <= 0.001001f,
		"exhausted work budget returned open space or hid truncation");

	std::cout << "PASS: visible image-source reflections, absorption/scattering, third bounce, thick corner and off-axis doorway audibility, sealed closed-door rejection, deterministic work and explicit limits\n";
}
