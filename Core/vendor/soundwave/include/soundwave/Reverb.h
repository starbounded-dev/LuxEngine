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

	// Deterministic: the ray directions, bounce order and sums are fixed for given inputs,
	// so a later GPU port can be compared against this result. Portal triangles reflect like
	// any surface. Failed evaluation clears output and returns an error.
	[[nodiscard]] EvaluationError EvaluateReverb(const GeometryView& geometry, const MaterialTable& materials,
		const Settings& settings, Position listener, std::vector<RayHit>& hits, ReverbResult& output);
}
