// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include <soundwave/Solve.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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
	void Same(float actual, float expected, const char* message)
	{
		Check(std::bit_cast<std::uint32_t>(actual) == std::bit_cast<std::uint32_t>(expected), message);
	}
	void Box(std::vector<Triangle>& triangles, std::uint64_t instance, MaterialTag material, float halfX, bool portal = false)
	{
		constexpr int indices[] = { 0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2,
			1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3 };
		const auto position = [halfX](int i)
		{
			return Position{ i & 1 ? halfX : -halfX, i & 2 ? 1.0f : -1.0f, i & 4 ? 1.0f : -1.0f };
		};
		for (int i = 0; i < 36; i += 3)
			triangles.push_back({ position(indices[i]), position(indices[i + 1]), position(indices[i + 2]), instance,0,material,portal });
	}
	void Front(std::vector<Triangle>& triangles, std::uint64_t instance, MaterialTag material, float x)
	{
		triangles.push_back({ {x,-1,-1}, {x,-1,1}, {x,1,1}, instance,0,material,false });
		triangles.push_back({ {x,-1,-1}, {x,1,1}, {x,1,-1}, instance,0,material,false });
	}
}

int main()
{
	using namespace Lux::Soundwave;
	Grid grid;
	Settings settings;
	settings.ReflectionDepth = 0;
	settings.OpeningGridSize = 1;
	settings.MaxBounces = 1;
	MaterialTable materials = DefaultMaterials();
	DirectScratch scratch;
	Path path;
	std::vector<Triangle> triangles;
	const Position listener{ -1,0,0 }, source{ 1,0,0 };
	const auto evaluate = [&]() { return EvaluateDirect(grid, {}, materials, settings, listener, source, 0, scratch, path); };

	// Binary-exact geometry keeps this comparison about the shipped loss arithmetic,
	// not about tolerances in a different ray intersector. Every material and strength.
	for (std::size_t tag = 0; tag < kMaterialTagCount; ++tag)
	{
		triangles.clear();
		Box(triangles, 7, static_cast<MaterialTag>(tag), 0.125f);
		Check(grid.Build(triangles) == GeometryError::None, "solid fixture invalid");
		for (float strength : { 0.0f, 0.125f, 1.0f, 2.75f, 4.0f })
		{
			settings.Strength = strength;
			Check(evaluate() == EvaluationError::None && path.Crossings.size() == 1, "solid crossing not paired/deduplicated");
			const auto& material = materials[tag];
			const float lossLF = (strength * 30.0f) * 0.25f / std::max(material.TransmissionLF, 0.001f);
			const float lossHF = (strength * 30.0f) * 0.25f / std::max(material.TransmissionHF, 0.001f);
			Same(path.Crossings[0].Start, 0.875f, "solid entry distance changed");
			Same(path.Crossings[0].End, 1.125f, "solid exit distance changed");
			Same(path.Crossings[0].LossLF, lossLF, "solid LF arithmetic changed bits");
			Same(path.Crossings[0].LossHF, lossHF, "solid HF arithmetic changed bits");
			Same(path.LossLF, std::min(lossLF, 60.0f), "solid LF cap changed bits");
			Same(path.LossHF, std::min(lossHF, 60.0f), "solid HF cap changed bits");
			Check(path.Crossings[0].Material == tag && !path.Crossings[0].OneSided, "solid material/semantics lost");
		}
	}
	settings.Strength = 1;
	Check(EvaluateDirect(grid, {}, materials, settings, listener, source, 7, scratch, path) == EvaluationError::None &&
		path.Crossings.empty() && path.GainLF() == 1, "source's own solid occludes it");

	triangles.clear();
	Front(triangles, 8, 1, 0);
	Check(grid.Build(triangles) == GeometryError::None && evaluate() == EvaluationError::None && path.Crossings.size() == 1,
		"shared-edge thin wall was counted twice");
	Check(path.Crossings[0].OneSided && path.Crossings[0].Start == path.Crossings[0].End, "unmatched entry is not thin");
	Same(path.LossLF, -10.0f * std::log10(std::max(1.0f - materials[1].FlatTransmissionLF, 1e-6f)), "thin LF formula differs");
	Same(path.LossHF, -10.0f * std::log10(std::max(1.0f - materials[1].FlatTransmissionHF, 1e-6f)), "thin HF formula differs");
	const float flatLF = path.LossLF;
	Check(EvaluateDirect(grid, {}, materials, settings, source, listener, 0, scratch, path) == EvaluationError::None &&
		path.Crossings.size() == 1 && path.Crossings[0].OneSided, "unmatched exit lost thin semantics");
	Same(path.LossLF, flatLF, "thin wall differs from reverse side");
	Front(triangles, 8, 1, 0.5f);
	Check(grid.Build(triangles) == GeometryError::None && evaluate() == EvaluationError::None && path.Crossings.size() == 2,
		"separate unmatched faces collapsed into a solid");
	Same(path.LossLF, flatLF + flatLF, "unmatched face losses were not added");

	triangles.clear();
	Box(triangles, 10, 1, 0.5f);
	Box(triangles, 20, 2, 0.25f);
	Check(grid.Build(triangles) == GeometryError::None && evaluate() == EvaluationError::None && path.Crossings.size() == 2,
		"overlapping owners lost a crossing");
	Check(path.Crossings[0].Start == 0.5f && path.Crossings[0].End == 1.5f &&
		path.Crossings[1].Start == 0.75f && path.Crossings[1].End == 1.25f, "solids paired across owners or reduced in exit order");
	Check(EvaluateDirect(grid, {}, materials, settings, {}, source, 0, scratch, path) == EvaluationError::None &&
		path.Crossings.size() == 2 && path.Crossings[0].OneSided && path.Crossings[1].OneSided,
		"inside-solid exits no longer match the comparison model");

	Check(grid.Build({}) == GeometryError::None, "empty geometry reset failed");
	PortalBox portal;
	portal.Instance = 42;
	materials[0].TransmissionLF = 4;
	materials[0].TransmissionHF = 1;
	settings.Strength = 0.25f;
	const auto throughPortal = [&](Position from, Position to)
	{
		return EvaluateDirect(grid, { &portal,1 }, materials, settings, from, to, 0, scratch, path);
	};
	Check(throughPortal({ -2,0,0 }, { 2,0,0 }) == EvaluationError::None && path.Crossings.size() == 1 && path.Crossings[0].Portal,
		"portal-only world did not clip a shutter");
	Same(path.LossLF, 3.75f, "portal full thickness wrong");
	Check(throughPortal({ -0.5f,0,0 }, { 0.5f,0,0 }) == EvaluationError::None && path.Crossings.size() == 1,
		"segment wholly inside shutter was treated as clear");
	Same(path.LossLF, 1.875f, "inside portal segment thickness wrong");
	Check(throughPortal({}, { 2,0,0 }) == EvaluationError::None && path.Crossings.size() == 1, "portal exit-only clipping lost");
	Same(path.LossLF, 1.875f, "portal listener-inside thickness wrong");
	Check(throughPortal({ -2,0,0 }, {}) == EvaluationError::None && path.Crossings.size() == 1, "portal entry-only clipping lost");
	Same(path.LossLF, 1.875f, "portal source-inside thickness wrong");
	Check(throughPortal({ -2,2,0 }, { 2,2,0 }) == EvaluationError::None && path.Crossings.empty(), "parallel miss hit shutter");
	Check(throughPortal({ -2,0,0 }, { 0,2,0 }) == EvaluationError::None && path.Crossings.empty(), "tangent point acquired wall thickness");

	triangles.clear();
	Box(triangles, 42, 0, 1, true);
	Check(grid.Build(triangles) == GeometryError::None && throughPortal({ -2,0,0 }, { 2,0,0 }) == EvaluationError::None &&
		path.Crossings.size() == 1, "portal triangles charged in addition to analytic shutter");
	Check(EvaluateDirect(grid, { &portal,1 }, materials, settings, { -2,0,0 }, { 2,0,0 }, 42, scratch, path) == EvaluationError::None &&
		path.Crossings.size() == 1, "source-instance exclusion incorrectly bypassed analytic shutter");
	Check(grid.Build({}) == GeometryError::None, "portal geometry reset failed");
	portal.WorldToBox = { 0,0.5f,0,0, -2,0,0,0, 0,0,1,0, 8,-1.5f,0,1 };
	Check(throughPortal({ 0,4,0 }, { 6,4,0 }) == EvaluationError::None && path.Crossings.size() == 1, "affine rotated/scaled/translated portal failed");
	Same(path.LossLF, 7.5f, "affine portal lost world-space thickness");
	const auto pathCapacity = path.Crossings.capacity();
	const auto wallCapacity = scratch.Walls.capacity();
	Check(throughPortal({ 0,4,0 }, { 6,4,0 }) == EvaluationError::None && path.Crossings.capacity() >= pathCapacity &&
		scratch.Walls.capacity() >= wallCapacity, "steady-state direct evaluation discarded scratch storage");
	Check(EvaluateDirect(grid, { &portal,1 }, materials, settings, path.From, path.To, 0, scratch, path) == EvaluationError::None,
		"saved path endpoints could not be reused as inputs");
	Same(path.LossLF, 7.5f, "reset erased aliased input endpoints before evaluation");

	portal = {};
	portal.Instance = 42;
	const std::array<PortalBox, 2> duplicates{ portal, portal };
	Check(EvaluateDirect(grid, duplicates, materials, settings, listener, source, 0, scratch, path) == EvaluationError::InvalidPortal &&
		path.Crossings.empty() && path.Distance == 0, "duplicate portal identity was charged twice or stale output retained");
	portal.WorldToBox[0] = 0;
	Check(throughPortal(listener, source) == EvaluationError::InvalidPortal, "singular portal transform accepted");
	portal = {};
	portal.Instance = 42;
	portal.WorldToBox[3] = 0.1f;
	Check(throughPortal(listener, source) == EvaluationError::InvalidPortal, "projective portal transform accepted");
	portal = {};
	portal.Instance = 42;
	portal.WorldToBox[15] = std::nextafter(1.0f, 0.0f);
	Check(throughPortal({ -2,0,0 }, { 2,0,0 }) == EvaluationError::None, "rounded affine inverse W rejected");
	Same(path.LossLF, 3.75f, "homogeneous rounding changed the XYZ portal transform");
	portal.WorldToBox[15] = 1.01f;
	Check(throughPortal(listener, source) == EvaluationError::InvalidPortal, "non-affine homogeneous scale accepted");
	portal = {};
	portal.Instance = 42;
	portal.WorldToBox[0] = std::numeric_limits<float>::max();
	Check(throughPortal({ -2,0,0 }, { 2,0,0 }) == EvaluationError::InvalidPortal, "overflowing portal coordinates accepted");
	settings.CandidateBudget = 0;
	Check(evaluate() == EvaluationError::InvalidSettings && path.Crossings.empty(), "invalid solve settings silently used defaults");
	settings = {};
	materials[2].AbsorptionLF = std::numeric_limits<float>::quiet_NaN();
	Check(evaluate() == EvaluationError::InvalidMaterials, "invalid material table accepted");
	materials = DefaultMaterials();
	Check(EvaluateDirect(grid, {}, materials, settings, { std::numeric_limits<float>::quiet_NaN(),0,0 }, source, 0, scratch, path) ==
		EvaluationError::InvalidRay && path.Distance == 0, "invalid source/listener retained a stale measurement");
	Check(EvaluateDirect(grid, {}, materials, settings, source, source, 0, scratch, path) == EvaluationError::None &&
		path.Crossings.empty() && path.GainLF() == 1 && path.Distance == 0, "coincident source/listener attenuates sound");
	std::cout << "PASS: CPU direct crossings, bitwise material/strength arithmetic, overlaps, thin walls, inside solids, source exclusion, analytic shutters, affine transforms, validation and scratch reuse\n";
}
