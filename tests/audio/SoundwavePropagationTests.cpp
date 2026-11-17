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
	// Pin the reflection arithmetic first; air absorption is checked on its own below.
	settings.AirAbsorptionLF = settings.AirAbsorptionHF = 0;
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
	PreparedCandidates prepared;
	Check(PrepareCandidates(GeometryView(grid), {}, materials, settings, {-1,1,0}, {1,1,0}, 0, scratch, prepared) == EvaluationError::None &&
		!prepared.ReflectionTriangles.empty() && prepared.Openings.empty() && prepared.Direct.GainLF() == result.Direct.GainLF(),
		"reflection candidate preparation lost its bounded pool or direct reference");
	// Reflecting on the shared diagonal produces two equivalent geometric candidates.
	// The staged path keeps the serial work order and accepts the same single path.
	PreparedReflections reflections;
	const GeometryView floorView(grid);
	Check(PrepareReflections(floorView, materials, settings, prepared, scratch, reflections) ==
		EvaluationError::None && reflections.Paths.size() == 2, "reflecting candidates were lost before visibility");
	const std::array<std::uint32_t, 4> emptyOpenings{};
	std::array<std::uint32_t, 4> reflectionVisibility{};
	SolveResult stagedReflection;
	Check(ReduceOpeningVisibility(floorView, {}, materials, settings, {-1,1,0}, {1,1,0}, 0, prepared,
		emptyOpenings, scratch, stagedReflection, &reflections, reflectionVisibility) == EvaluationError::None &&
		stagedReflection.Diagnostics.ReflectionPaths == result.Diagnostics.ReflectionPaths &&
		stagedReflection.Diagnostics.Work == result.Diagnostics.Work &&
		stagedReflection.Diagnostics.VisibilityRays == result.Diagnostics.VisibilityRays &&
		stagedReflection.Indirect.front().GainLF == result.Indirect.front().GainLF,
		"staged reflection reduction diverged from the serial solve");
	reflectionVisibility = { 1, 2, 1, 2 };
	Check(ReduceOpeningVisibility(floorView, {}, materials, settings, {-1,1,0}, {1,1,0}, 0, prepared,
		emptyOpenings, scratch, stagedReflection, &reflections, reflectionVisibility) == EvaluationError::None &&
		stagedReflection.Indirect.empty() && stagedReflection.Diagnostics.VisibilityRays == 2,
		"blocked reflection charged or consumed a subsequent leg");
	reflectionVisibility[0] = 2;
	Check(ReduceOpeningVisibility(floorView, {}, materials, settings, {-1,1,0}, {1,1,0}, 0, prepared,
		emptyOpenings, scratch, stagedReflection, &reflections, reflectionVisibility) == EvaluationError::NumericalFailure &&
		stagedReflection.Indirect.empty() && stagedReflection.Diagnostics.VisibilityRays == 2,
		"invalid reflection visibility replaced the last staged result");
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
	Near(result.Indirect[0].ScatteringGain, 0.5f, "scattering share is not observable");
	Near(result.Indirect[0].SpreadGain, 2.0f / std::sqrt(8.0f), "relative path spreading is not observable");
	materials[0].Scattering = 0;
	Check(result.Diagnostics.PathsByInteractions[1] == 1 && result.Diagnostics.PathsByInteractions[0] == 0,
		"single reflection not counted by its interaction count");

	// Air absorption charges only the reflection's length beyond the direct path; the direct
	// signal is the backend's distance fade and is never charged twice.
	settings.AirAbsorptionLF = 0.1f;
	settings.AirAbsorptionHF = 2.5f;
	Check(Solve(grid, {}, materials, settings, {-1,1,0}, {1,1,0}, 0, scratch, result) == EvaluationError::None, "air solve failed");
	const float excess = std::sqrt(8.0f) - 2.0f;
	Near(result.Indirect[0].GainHF, reflection.GainHF * GainFromLossDb(excess * 0.025f), "HF air absorption on the excess path wrong");
	Near(result.Indirect[0].GainLF, reflection.GainLF * GainFromLossDb(excess * 0.001f), "LF air absorption on the excess path wrong");
	Check(result.OcclusionGainLF == 1 && result.OcclusionGainHF == 1, "air absorption faded the clear direct path");
	settings.AirAbsorptionLF = settings.AirAbsorptionHF = 0;

	PortalBox legBlocker;
	legBlocker.Instance = 9;
	legBlocker.WorldToBox = { 10,0,0,0, 0,10,0,0, 0,0,2,0, 5,-5,0,1 };
	Check(Solve(grid, {&legBlocker,1}, materials, settings, {-1,1,0}, {1,1,0}, 0, scratch, result) == EvaluationError::None &&
		result.Diagnostics.ReflectionPaths == 0, "a blocked reflection leg reached the listener");
	Check(Solve(grid, {}, materials, settings, {-1,1,0}, {1,-1,0}, 0, scratch, result) == EvaluationError::None &&
		result.Diagnostics.ReflectionPaths == 0 && result.OcclusionGainLF <= 0.001001f, "mirror path transmitted through its own plane");

	// A shallow concave strip bends reflected rays together. The convex control
	// reverses the adjacent face normals, and the flat control has no curvature.
	const auto focus = [&](float rimHeight, float strength)
	{
		std::vector<Triangle> strip;
		const std::array<float, 4> boundaries{ -2, -1, 1, 2 };
		for (std::size_t i = 1; i < boundaries.size(); ++i)
		{
			const float left = boundaries[i - 1], right = boundaries[i];
			const float yLeft = i == 1 ? rimHeight : 0.0f;
			const float yRight = i + 1 == boundaries.size() ? rimHeight : 0.0f;
			strip.push_back({ {left,yLeft,-2},{right,yRight,2},{right,yRight,-2},1 });
			strip.push_back({ {left,yLeft,-2},{left,yLeft,2},{right,yRight,2},1 });
		}
		Grid reflector;
		Check(reflector.Build(strip) == GeometryError::None, "caustic geometry invalid");
		Settings focusSettings = settings;
		focusSettings.CausticFocus = strength;
		SolveResult focused;
		Check(Solve(reflector, {}, materials, focusSettings, {-0.5f,1,0}, {0.5f,1,0}, 0, scratch, focused) == EvaluationError::None &&
			focused.Diagnostics.ReflectionPaths == 1 && focused.OcclusionGainLF == 1 && focused.OcclusionGainHF == 1,
			"caustic control changed dry gain or lost the analytic reflection");
		return focused;
	};
	const auto planar = focus(0.0f, 1.0f);
	const auto disabled = focus(0.3f, 0.0f);
	const auto concave = focus(0.3f, 1.0f);
	const auto convex = focus(-0.3f, 1.0f);
	Check(planar.AddedFocusEnergy == 0 && planar.Indirect.front().FocusGainHF == 1 &&
		disabled.AddedFocusEnergy == 0 && planar.Indirect.front().GainHF == disabled.Indirect.front().GainHF,
		"planar or disabled focusing changed baseline spreading");
	Check(concave.AddedFocusEnergy > 0.01f && concave.Indirect.front().FocusGainHF > 1 &&
		concave.Indirect.front().GainHF > disabled.Indirect.front().GainHF && concave.Indirect.front().GainHF <= 1 &&
		convex.AddedFocusEnergy == 0 && convex.Indirect.front().FocusGainHF < 1,
		"concave and convex acoustic curvature did not change reflection energy in opposite directions");

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
	// Each bounce setting is observable: depth N stores paths with exactly N interactions and none deeper.
	for (std::uint32_t depth = 1; depth <= 4; ++depth)
	{
		settings.ReflectionDepth = settings.MaxBounces = depth;
		Check(Solve(grid, {}, materials, settings, {-0.5f,0,0}, {0.5f,0,0}, 0, scratch, result) == EvaluationError::None &&
			result.Diagnostics.PathsByInteractions[depth] > 0 && (depth == kMaximumInteractions || result.Diagnostics.PathsByInteractions[depth + 1] == 0),
			"bounce count is not measurable in diagnostics");
	}
	settings.ReflectionDepth = settings.MaxBounces = 3;
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
	Check(PrepareCandidates(GeometryView(grid), {}, materials, settings, {1,0,-0.5f}, {-1,0,-0.5f}, 0, scratch, prepared) == EvaluationError::None &&
		!prepared.Openings.empty() && prepared.Openings.size() <= settings.OpeningNodeBudget &&
		prepared.Diagnostics.Work <= settings.CandidateBudget,
		"occluded corner candidate preparation did not respect the authored bounds");
	std::vector<Position> visibilityPoints{{1,0,-0.5f}};
	for (const auto& opening : prepared.Openings)
		visibilityPoints.push_back(opening.Point);
	visibilityPoints.push_back({-1,0,-0.5f});
	std::vector<std::uint32_t> visibility(visibilityPoints.size() * visibilityPoints.size());
	for (std::size_t from = 0; from < visibilityPoints.size(); ++from)
		for (std::size_t to = 0; to < visibilityPoints.size(); ++to)
		{
			std::vector<RayHit> hits;
			Check(grid.Trace(visibilityPoints[from], visibilityPoints[to], 0, hits), "corner visibility reference trace failed");
			visibility[from * visibilityPoints.size() + to] = hits.empty() ? 0 : 1;
		}
	SolveResult reduced;
	Check(ReduceOpeningVisibility(GeometryView(grid), {}, materials, settings, {1,0,-0.5f}, {-1,0,-0.5f},
		0, prepared, visibility, scratch, reduced) == EvaluationError::None && reduced.HasAudibleOpening &&
		std::bit_cast<std::uint32_t>(reduced.OcclusionGainLF) == std::bit_cast<std::uint32_t>(result.OcclusionGainLF) &&
		std::bit_cast<std::uint32_t>(reduced.OcclusionGainHF) == std::bit_cast<std::uint32_t>(result.OcclusionGainHF),
		"visibility-matrix opening search diverged from the CPU reference");
	Settings reflectedSettings = settings;
	reflectedSettings.ReflectionDepth = 2;
	PreparedCandidates reflectedPrepared;
	SolveResult reflectedCpu, reflectedReduced;
	Check(PrepareCandidates(GeometryView(grid), {}, materials, reflectedSettings, {1,0,-0.5f}, {-1,0,-0.5f},
		0, scratch, reflectedPrepared) == EvaluationError::None &&
		reflectedPrepared.Openings.size() == prepared.Openings.size(), "reflection setting changed opening candidate indices");
	Check(Solve(grid, {}, materials, reflectedSettings, {1,0,-0.5f}, {-1,0,-0.5f}, 0, scratch, reflectedCpu) == EvaluationError::None &&
		ReduceOpeningVisibility(GeometryView(grid), {}, materials, reflectedSettings, {1,0,-0.5f}, {-1,0,-0.5f},
			0, reflectedPrepared, visibility, scratch, reflectedReduced) == EvaluationError::None,
		"default-depth source reduction failed");
	Check(std::bit_cast<std::uint32_t>(reflectedCpu.OcclusionGainLF) == std::bit_cast<std::uint32_t>(reflectedReduced.OcclusionGainLF) &&
		std::bit_cast<std::uint32_t>(reflectedCpu.OcclusionGainHF) == std::bit_cast<std::uint32_t>(reflectedReduced.OcclusionGainHF) &&
		reflectedCpu.Diagnostics.ReflectionPaths == reflectedReduced.Diagnostics.ReflectionPaths &&
		reflectedCpu.Diagnostics.OpeningPaths == reflectedReduced.Diagnostics.OpeningPaths,
		"default-depth visibility reduction changed the source gain");
	PreparedReflections hybridReflections;
	Check(PrepareReflections(GeometryView(grid), materials, reflectedSettings, reflectedPrepared, scratch, hybridReflections) ==
		EvaluationError::None, "hybrid reflection preparation failed");
	std::vector<std::uint32_t> hybridVisibility;
	for (const auto& candidate : hybridReflections.Paths)
	{
		const auto& path = candidate.Path;
		std::vector<RayHit> hits;
		for (std::uint32_t leg = 1; leg < path.PointCount; ++leg)
		{
			Check(grid.Trace(path.Points[leg - 1], path.Points[leg], 0, hits, kReflectionEndpointMargin),
				"hybrid reflection visibility trace failed");
			hybridVisibility.push_back(hits.empty() ? 0u : 1u);
		}
	}
	SolveResult hybrid;
	Check(ReduceOpeningVisibility(GeometryView(grid), {}, materials, reflectedSettings, {1,0,-0.5f}, {-1,0,-0.5f},
		0, reflectedPrepared, {}, scratch, hybrid, &hybridReflections, hybridVisibility) == EvaluationError::None &&
		hybrid.HasAudibleOpening && hybrid.OcclusionGainLF == reflectedCpu.OcclusionGainLF &&
		hybrid.OcclusionGainHF == reflectedCpu.OcclusionGainHF && hybrid.Diagnostics.Work == reflectedCpu.Diagnostics.Work &&
		hybrid.Diagnostics.ReflectionPaths == reflectedCpu.Diagnostics.ReflectionPaths &&
		hybrid.Diagnostics.OpeningPaths == reflectedCpu.Diagnostics.OpeningPaths,
		"CPU opening visibility with staged reflections changed path results");
	// Preparation speculates before reflections. The staged solve must keep the same
	// path results and exhaust the same work budget as the serial reference.
	Settings tightSettings = reflectedSettings;
	tightSettings.CandidateBudget = 32;
	PreparedCandidates tightPrepared;
	Check(PrepareCandidates(GeometryView(grid), {}, materials, tightSettings, {1,0,-0.5f}, {-1,0,-0.5f},
		0, scratch, tightPrepared) == EvaluationError::None &&
		tightPrepared.Diagnostics.Work == tightSettings.CandidateBudget && !tightPrepared.Openings.empty(),
		"tight-budget fixture did not fill the opening sample budget");
	const auto tightWidth = tightPrepared.Openings.size() + 2;
	std::vector<std::uint32_t> tightVisibility(tightWidth * tightWidth);
	SolveResult tightCpu, tightReduced;
	Check(Solve(grid, {}, materials, tightSettings, {1,0,-0.5f}, {-1,0,-0.5f}, 0, scratch, tightCpu) == EvaluationError::None &&
		ReduceOpeningVisibility(GeometryView(grid), {}, materials, tightSettings, {1,0,-0.5f}, {-1,0,-0.5f},
			0, tightPrepared, tightVisibility, scratch, tightReduced) == EvaluationError::None,
		"staged solve rejected a valid tight-budget job after reflections");
	Check(tightReduced.Diagnostics.Work == tightCpu.Diagnostics.Work && tightReduced.Diagnostics.WorkLimited &&
		tightReduced.Diagnostics.VisibilityRays == tightCpu.Diagnostics.VisibilityRays &&
		tightReduced.Diagnostics.ReflectionPaths == tightCpu.Diagnostics.ReflectionPaths &&
		tightReduced.OcclusionGainLF == tightCpu.OcclusionGainLF && tightReduced.OcclusionGainHF == tightCpu.OcclusionGainHF,
		"staged solve did not preserve the reference under a tight opening sample budget");
	const auto reducedLF = reduced.OcclusionGainLF;
	visibility[1] = 3;
	Check(ReduceOpeningVisibility(GeometryView(grid), {}, materials, settings, {1,0,-0.5f}, {-1,0,-0.5f},
		0, prepared, visibility, scratch, reduced) == EvaluationError::NumericalFailure && reduced.OcclusionGainLF == reducedLF,
		"invalid visibility matrix changed a published solve");
	Check(PrepareCandidates(GeometryView(grid), {}, materials, settings, {1,0,-0.5f}, {1,0,-1}, 0, scratch, prepared) == EvaluationError::None &&
		prepared.Openings.empty() && prepared.Direct.GainLF() == 1,
		"clear source inherited opening candidates from a previous solve");
	const auto preparedDirect = prepared.Direct;
	settings.CandidateBudget = 0;
	Check(PrepareCandidates(GeometryView(grid), {}, materials, settings, {1,0,-0.5f}, {-1,0,-0.5f}, 0, scratch, prepared) == EvaluationError::InvalidSettings &&
		prepared.Direct.Distance == preparedDirect.Distance && prepared.Openings.empty(),
		"invalid candidate preparation changed the last valid result");
	settings.CandidateBudget = 4096;
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

	// The same closed wall with a detailed render tessellation must not let early
	// blocked graph expansions starve reachable terminal candidates of their work.
	geometry.clear();
	Box(geometry, {-0.1f,-2,-2}, {0.1f,2,0}, 2);
	for (int level = 0; level < 3; ++level)
	{
		std::vector<Triangle> subdivided;
		subdivided.reserve(geometry.size() * 4);
		const auto midpoint = [](Position a, Position b)
		{
			return Position{(a.X+b.X)*0.5f,(a.Y+b.Y)*0.5f,(a.Z+b.Z)*0.5f};
		};
		for (const auto& triangle : geometry)
		{
			const auto ab = midpoint(triangle.A,triangle.B), bc = midpoint(triangle.B,triangle.C), ca = midpoint(triangle.C,triangle.A);
			for (const auto& childPoints : {std::array{triangle.A,ab,ca},std::array{ab,triangle.B,bc},
				std::array{ca,bc,triangle.C},std::array{ab,bc,ca}})
				subdivided.push_back({childPoints[0],childPoints[1],childPoints[2],triangle.Instance,0,0,false});
		}
		geometry.swap(subdivided);
	}
	settings = {};
	settings.ReflectionDepth = 0;
	settings.CandidateBudget = 65536;
	settings.OpeningNodeBudget = 2048;
	Check(grid.Build(geometry) == GeometryError::None && geometry.size() == 768, "detailed corner fixture invalid");
	Check(Solve(grid, {}, materials, settings, {1,0,-0.5f}, {-1,0,-0.5f}, 0, scratch, result) == EvaluationError::None &&
		result.HasAudibleOpening && result.OcclusionGainLF > 0.01f && result.Diagnostics.Work <= settings.CandidateBudget,
		"detailed corner graph starved a visible route within its authored work limit");

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
