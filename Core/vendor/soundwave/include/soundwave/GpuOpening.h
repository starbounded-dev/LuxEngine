// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "soundwave/Flatten.h"
#include "soundwave/Solve.h"

namespace Lux::Soundwave
{
	// std430 interface for the endpoint, adjacency and ordered-search compute passes.
	// Geometry and candidates are immutable until all three passes and readback complete.
	struct alignas(16) GpuOpeningRequest
	{
		std::uint32_t Config[4]{};		  // nodes, max bounces, work limit, remaining path slots
		std::uint32_t Geometry[4]{};	  // roots, ignored instance low/high
		float Listener[4]{}, Source[4]{}; // xyz positions, w direct LF/HF
		float Air[4]{};					  // LF/HF absorption in dB per 100 metres
		std::uint32_t Initial[4]{};		  // work, visibility rays, flags, portal count
		std::uint32_t Reflections[4]{};	  // paths, candidate count, work limit, enabled/work-limited flags
	};
	struct alignas(16) GpuOpeningNode
	{
		Position Point;
		std::uint32_t Edge;
	};
	struct GpuOpeningPath
	{
		std::uint32_t Chain[kMaximumInteractions]{};
		float Length = 0, GainLF = 0, GainHF = 0;
		std::uint32_t Depth = 0;
	};
	struct alignas(16) GpuOpeningResult
	{
		std::uint32_t Work = 0, VisibilityRays = 0, Paths = 0, Flags = 0;
		float GainLF = 1, GainHF = 1, FocusEnergy = 0;
		std::uint32_t Reflections = 0;
	};
	struct alignas(16) GpuReflection
	{
		float Points[kMaximumInteractions + 2][4]{};
		std::uint32_t Metadata[4]{}; // candidate ordinal, point count
		float Energy[4]{};			 // added focus energy
	};
	inline constexpr std::uint32_t kGpuOpeningWorkLimited = 1, kGpuOpeningPathsLimited = 2, kGpuOpeningInvalid = 4;
	static_assert(sizeof(GpuOpeningRequest) == 112 && sizeof(GpuOpeningNode) == 16 && sizeof(GpuReflection) == 320);
	static_assert(sizeof(GpuOpeningPath) == 80 && sizeof(GpuOpeningResult) == 32);

	// The seed contains direct measurement and already reduced reflections. The GPU continues
	// that same work/path budget; it never grants the opening search a second budget.
	[[nodiscard]] EvaluationError PrepareGpuOpening(const Settings& settings, const PreparedCandidates& prepared,
		const SolveResult& seed, std::array<std::uint32_t, 2> roots, std::uint64_t ignoredInstance,
		std::uint32_t portals, GpuOpeningRequest& output);
	std::size_t GpuOpeningScratchBytes(const GpuOpeningRequest& request);
	std::size_t GpuOpeningResultBytes(const GpuOpeningRequest& request);

	// Full source continuation: GPU reflection visibility/reduction runs before opening search
	// in the same submission. CPU preparation retains image-source paths and material factors.
	[[nodiscard]] EvaluationError PrepareGpuSource(const Settings& settings, const PreparedCandidates& prepared,
		const PreparedReflections& reflections, std::array<std::uint32_t, 2> roots, std::uint64_t ignoredInstance,
		std::uint32_t portals, GpuOpeningRequest& output, std::vector<GpuReflection>& paths);
	[[nodiscard]] EvaluationError FinishGpuSource(const Settings& settings, const PreparedCandidates& prepared,
		const PreparedReflections& reflections, const GpuOpeningResult& result, std::span<const GpuOpeningPath> paths,
		std::span<const std::uint32_t> retainedReflections, SolveResult& output);

	// Readback contains a header, the strongest LF route, then retained routes in CPU search
	// order. Validation is transactional: malformed/incomplete readback leaves output unchanged.
	[[nodiscard]] EvaluationError FinishGpuOpening(const Settings& settings, const PreparedCandidates& prepared,
		const SolveResult& seed, const GpuOpeningResult& result, std::span<const GpuOpeningPath> paths,
		SolveResult& output);
} // namespace Lux::Soundwave
