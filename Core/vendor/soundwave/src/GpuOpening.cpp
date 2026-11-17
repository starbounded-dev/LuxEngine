// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#include "soundwave/GpuOpening.h"
#include <algorithm>
#include <cmath>

namespace Lux::Soundwave
{
	EvaluationError PrepareGpuOpening(const Settings& settings, const PreparedCandidates& prepared,
		const SolveResult& seed, std::array<std::uint32_t, 2> roots, std::uint64_t ignoredInstance,
		std::uint32_t portals, GpuOpeningRequest& output)
	{
		if (!settings.Validate())
			return EvaluationError::InvalidSettings;
		if (prepared.Openings.size() > settings.OpeningNodeBudget || seed.Diagnostics.Work > settings.CandidateBudget ||
			seed.Indirect.size() > settings.PathBudget || seed.Diagnostics.OpeningPaths || seed.HasAudibleOpening ||
			!std::isfinite(seed.OcclusionGainLF) || !std::isfinite(seed.OcclusionGainHF))
			return EvaluationError::InvalidGeometry;
		const auto finite = [](Position p) { return std::isfinite(p.X) && std::isfinite(p.Y) && std::isfinite(p.Z); };
		if (!finite(prepared.Direct.From) || !finite(prepared.Direct.To) || !std::isfinite(prepared.Direct.Distance) ||
			prepared.Direct.Distance < 0 || seed.OcclusionGainLF < 0 || seed.OcclusionGainLF > 1 ||
			seed.OcclusionGainHF < 0 || seed.OcclusionGainHF > 1 ||
			seed.Diagnostics.VisibilityRays > seed.Diagnostics.Work ||
			std::any_of(prepared.Openings.begin(), prepared.Openings.end(),
				[&](const auto& node) { return !finite(node.Point); }))
			return EvaluationError::InvalidGeometry;
		GpuOpeningRequest request{};
		request.Config[0] = static_cast<std::uint32_t>(prepared.Openings.size());
		request.Config[1] = settings.MaxBounces;
		request.Config[2] = settings.CandidateBudget;
		request.Config[3] = settings.PathBudget - static_cast<std::uint32_t>(seed.Indirect.size());
		request.Geometry[0] = roots[0];
		request.Geometry[1] = roots[1];
		request.Geometry[2] = static_cast<std::uint32_t>(ignoredInstance);
		request.Geometry[3] = static_cast<std::uint32_t>(ignoredInstance >> 32);
		const auto& from = prepared.Direct.From;
		const auto& to = prepared.Direct.To;
		request.Listener[0] = from.X;
		request.Listener[1] = from.Y;
		request.Listener[2] = from.Z;
		request.Source[0] = to.X;
		request.Source[1] = to.Y;
		request.Source[2] = to.Z;
		request.Listener[3] = seed.OcclusionGainLF;
		request.Source[3] = seed.OcclusionGainHF;
		request.Air[0] = settings.AirAbsorptionLF;
		request.Air[1] = settings.AirAbsorptionHF;
		request.Initial[0] = seed.Diagnostics.Work;
		request.Initial[1] = seed.Diagnostics.VisibilityRays;
		request.Initial[2] = (seed.Diagnostics.WorkLimited ? kGpuOpeningWorkLimited : 0u) |
			(seed.Diagnostics.PathsLimited ? kGpuOpeningPathsLimited : 0u);
		request.Initial[3] = portals;
		output = request;
		return EvaluationError::None;
	}

	std::size_t GpuOpeningScratchBytes(const GpuOpeningRequest& request)
	{
		return sizeof(std::uint32_t) *
			((static_cast<std::size_t>(request.Config[1]) + 1) * request.Config[0] +
				6 * static_cast<std::size_t>(request.Config[2]));
	}

	std::size_t GpuOpeningResultBytes(const GpuOpeningRequest& request)
	{
		return sizeof(GpuOpeningResult) + (static_cast<std::size_t>(request.Config[3]) + 1) * sizeof(GpuOpeningPath);
	}

	EvaluationError PrepareGpuSource(const Settings& settings, const PreparedCandidates& prepared,
		const PreparedReflections& reflections, std::array<std::uint32_t, 2> roots, std::uint64_t ignoredInstance,
		std::uint32_t portals, GpuOpeningRequest& output, std::vector<GpuReflection>& paths)
	{
		SolveResult seed;
		seed.Direct = prepared.Direct;
		seed.OcclusionGainLF = seed.Direct.GainLF();
		seed.OcclusionGainHF = seed.Direct.GainHF();
		seed.Diagnostics = prepared.Diagnostics;
		GpuOpeningRequest request;
		const auto error = PrepareGpuOpening(settings, prepared, seed, roots, ignoredInstance, portals, request);
		if (error != EvaluationError::None)
			return error;
		const auto limit =
			settings.OpeningGridSize > 1 ? std::max(1u, settings.CandidateBudget / 2) : settings.CandidateBudget;
		if (reflections.CandidateCount > limit)
			return EvaluationError::InvalidGeometry;
		std::vector<GpuReflection> next;
		next.reserve(reflections.Paths.size());
		std::uint32_t previous = 0;
		for (const auto& candidate : reflections.Paths)
		{
			const auto& path = candidate.Path;
			if (candidate.Candidate <= previous || candidate.Candidate > reflections.CandidateCount ||
				path.Kind != PathKind::Reflection || path.Reflections == 0 ||
				path.Reflections > settings.ReflectionDepth || path.PointCount != path.Reflections + 2 ||
				!std::isfinite(path.AddedFocusEnergy) || path.AddedFocusEnergy < 0 || !std::isfinite(path.Distance) ||
				!std::isfinite(path.GainLF) || !std::isfinite(path.GainHF))
				return EvaluationError::InvalidGeometry;
			previous = candidate.Candidate;
			GpuReflection gpu{};
			gpu.Metadata[0] = candidate.Candidate;
			gpu.Metadata[1] = path.PointCount;
			gpu.Energy[0] = path.AddedFocusEnergy;
			for (std::uint32_t i = 0; i < path.PointCount; ++i)
			{
				const auto point = path.Points[i];
				if (!std::isfinite(point.X) || !std::isfinite(point.Y) || !std::isfinite(point.Z))
					return EvaluationError::InvalidGeometry;
				gpu.Points[i][0] = point.X;
				gpu.Points[i][1] = point.Y;
				gpu.Points[i][2] = point.Z;
			}
			next.push_back(gpu);
		}
		request.Reflections[0] = static_cast<std::uint32_t>(next.size());
		request.Reflections[1] = reflections.CandidateCount;
		request.Reflections[2] = limit;
		request.Reflections[3] = 1u | (reflections.WorkLimited ? 2u : 0u);
		output = request;
		paths = std::move(next);
		return EvaluationError::None;
	}

	EvaluationError FinishGpuSource(const Settings& settings, const PreparedCandidates& prepared,
		const PreparedReflections& reflections, const GpuOpeningResult& result, std::span<const GpuOpeningPath> paths,
		std::span<const std::uint32_t> retainedReflections, SolveResult& output)
	{
		if (!settings.Validate())
			return EvaluationError::InvalidSettings;
		if (result.Reflections > settings.PathBudget || retainedReflections.size() != result.Reflections ||
			!std::isfinite(result.FocusEnergy) || result.FocusEnergy < 0)
			return EvaluationError::NumericalFailure;
		SolveResult seed;
		seed.Direct = prepared.Direct;
		seed.OcclusionGainLF = seed.Direct.GainLF();
		seed.OcclusionGainHF = seed.Direct.GainHF();
		seed.Diagnostics = prepared.Diagnostics;
		// Reflection visibility can consume the sample budget; final work is supplied by the GPU.
		seed.Diagnostics.Work = 0;
		std::uint32_t previous = 0;
		for (std::size_t i = 0; i < retainedReflections.size(); ++i)
		{
			const auto index = retainedReflections[i];
			if (index >= reflections.Paths.size() || (i != 0 && index <= previous))
				return EvaluationError::NumericalFailure;
			previous = index;
			const auto& path = reflections.Paths[index].Path;
			if (path.Reflections == 0 || path.Reflections > settings.ReflectionDepth)
				return EvaluationError::NumericalFailure;
			seed.Indirect.push_back(path);
			++seed.Diagnostics.PathsByInteractions[path.Reflections];
			seed.AddedFocusEnergy += path.AddedFocusEnergy;
		}
		if (std::abs(seed.AddedFocusEnergy - result.FocusEnergy) > 1e-5f * std::max(1.0f, seed.AddedFocusEnergy))
			return EvaluationError::NumericalFailure;
		seed.Diagnostics.ReflectionPaths = result.Reflections;
		return FinishGpuOpening(settings, prepared, seed, result, paths, output);
	}

	EvaluationError FinishGpuOpening(const Settings& settings, const PreparedCandidates& prepared,
		const SolveResult& seed, const GpuOpeningResult& result, std::span<const GpuOpeningPath> paths,
		SolveResult& output)
	{
		if (!settings.Validate())
			return EvaluationError::InvalidSettings;
		const auto gainValid = [](float gain) { return std::isfinite(gain) && gain >= 0 && gain <= 1; };
		GpuOpeningRequest validated;
		if (PrepareGpuOpening(settings, prepared, seed, {}, 0, 0, validated) != EvaluationError::None)
			return EvaluationError::InvalidGeometry;
		if (seed.Indirect.size() > settings.PathBudget || result.Paths > settings.PathBudget - seed.Indirect.size() ||
			paths.size() != static_cast<std::size_t>(result.Paths) + 1 || result.Work < seed.Diagnostics.Work ||
			result.Work > settings.CandidateBudget || result.VisibilityRays < seed.Diagnostics.VisibilityRays ||
			result.VisibilityRays > result.Work ||
			(result.Flags & ~(kGpuOpeningWorkLimited | kGpuOpeningPathsLimited)) || !gainValid(result.GainLF) ||
			!gainValid(result.GainHF) || result.GainLF < seed.OcclusionGainLF || result.GainHF < seed.OcclusionGainHF)
			return EvaluationError::NumericalFailure;
		for (std::size_t i = 0; i < paths.size(); ++i)
		{
			const auto& path = paths[i];
			if (i == 0 && path.Depth == 0)
				continue;
			if (path.Depth == 0 || path.Depth > settings.MaxBounces || !std::isfinite(path.Length) || path.Length < 0 ||
				!gainValid(path.GainLF) || !gainValid(path.GainHF))
				return EvaluationError::NumericalFailure;
			for (std::uint32_t j = 0; j < path.Depth; ++j)
				if (path.Chain[j] >= prepared.Openings.size())
					return EvaluationError::NumericalFailure;
		}
		if ((paths[0].Depth != 0) != (result.GainLF > seed.OcclusionGainLF))
			return EvaluationError::NumericalFailure;
		const auto expand = [&](const GpuOpeningPath& path) {
			PropagationPath expanded;
			expanded.Kind = PathKind::Opening;
			expanded.PointCount = path.Depth + 2;
			expanded.Diffractions = path.Depth;
			expanded.Distance = path.Length;
			expanded.GainLF = path.GainLF;
			expanded.GainHF = path.GainHF;
			expanded.SpreadGain =
				std::min(1.0f, std::max(1.0f, prepared.Direct.Distance) / std::max(1.0f, path.Length));
			expanded.Points[0] = prepared.Direct.From;
			for (std::uint32_t j = 0; j < path.Depth; ++j)
				expanded.Points[j + 1] = prepared.Openings[path.Chain[j]].Point;
			expanded.Points[path.Depth + 1] = prepared.Direct.To;
			return expanded;
		};
		SolveResult next = seed;
		for (std::size_t i = 1; i < paths.size(); ++i)
		{
			next.Indirect.push_back(expand(paths[i]));
			++next.Diagnostics.PathsByInteractions[paths[i].Depth];
		}
		if (paths[0].Depth)
		{
			next.AudibleOpening = expand(paths[0]);
			next.HasAudibleOpening = true;
		}
		next.OcclusionGainLF = result.GainLF;
		next.OcclusionGainHF = result.GainHF;
		next.Diagnostics.Work = result.Work;
		next.Diagnostics.VisibilityRays = result.VisibilityRays;
		next.Diagnostics.OpeningPaths = result.Paths;
		next.Diagnostics.WorkLimited = (result.Flags & kGpuOpeningWorkLimited) != 0;
		next.Diagnostics.PathsLimited = (result.Flags & kGpuOpeningPathsLimited) != 0;
		output = std::move(next);
		return EvaluationError::None;
	}
} // namespace Lux::Soundwave
