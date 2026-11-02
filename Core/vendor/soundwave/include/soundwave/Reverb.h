// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "soundwave/Solve.h"

namespace Lux::Soundwave
{
	// The space around the listener, measured by bouncing a fixed set of rays through it.
	struct ReverbResult
	{
		// Seconds for the diffuse field to fall 60 dB. Escaping rays count as fully absorbed
		// (Sabine's open window), so open air decays at once and a closed stone room rings.
		float DecaySeconds = 0;
		float Send = 0;           // 0 dry, 1 fully reverberant; what drives the ReverbSend bus
		float EscapedFraction = 0; // rays that left the geometry before their bounce limit
		float MeanFreePath = 0;    // metres between surface hits
		std::uint32_t Traces = 0;
	};

	// One ray's contribution. The GPU kernel writes exactly this per ray, so CPU and GPU share
	// FinishReverb and its fixed ray-order reduction.
	struct ReverbRay
	{
		float Absorbed = 0;  // sum of applied absorption over the ray's hits
		float Travelled = 0; // metres to the last hit
		std::uint32_t Hits = 0;
		std::uint32_t Traces = 0;
		std::uint32_t Escaped = 0; // 1 when the ray left the geometry
	};

	// Deterministic: the ray directions, bounce order and sums are fixed for given inputs. Portal
	// triangles reflect like any surface. Failed evaluation clears output and returns an error.
	[[nodiscard]] EvaluationError EvaluateReverb(const GeometryView& geometry, const MaterialTable& materials,
		const Settings& settings, Position listener, std::vector<RayHit>& hits, ReverbResult& output);
	[[nodiscard]] EvaluationError TraceReverbRay(const GeometryView& geometry, const MaterialTable& materials,
		const Settings& settings, Position listener, std::uint32_t ray, std::vector<RayHit>& hits, ReverbRay& output);
	// Sums rays in index order (doubles), then derives the decay and send.
	[[nodiscard]] EvaluationError FinishReverb(std::span<const ReverbRay> rays, const Settings& settings, ReverbResult& output);
}
