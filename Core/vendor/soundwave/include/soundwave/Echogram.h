// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "soundwave/Solve.h"
#include <array>
#include <cstdint>
#include <span>

namespace Lux::Soundwave
{
	inline constexpr std::uint32_t kEchogramBins = 256;

	struct EchogramSettings
	{
		float WindowSeconds = 2.0f;
		float SmoothingSeconds = 0.0f;
		bool Validate() const;
	};

	struct EchogramBin
	{
		float EnergyLF = 0.0f;
		float EnergyHF = 0.0f;
	};

	// A reflected ray contributes energy at each surface arrival. Delay is measured from the
	// probe, so this form can represent a room response without an authored audio source.
	struct EchogramArrival
	{
		float DelaySeconds = 0.0f;
		float EnergyLF = 0.0f, EnergyHF = 0.0f;
	};

	// Arrival times are relative to the direct path. Each bin holds linear energy, not dB or
	// amplitude. The caller can cache one result per zone and invalidate it with the geometry
	// generation. No engine types or allocation are required by this representation.
	struct Echogram
	{
		std::array<EchogramBin, kEchogramBins> Bins{};
		float DirectArrivalSeconds = 0.0f;
		float BinSeconds = 0.0f;
		float TotalEnergyLF = 0.0f, TotalEnergyHF = 0.0f;
		std::uint32_t PathCount = 0;
		bool WindowLimited = false;
	};

	// Builds a time-shift histogram from the CPU reference's accepted direct and indirect paths.
	// Retains output on invalid input. Paths outside the authored time window are counted but
	// reported by WindowLimited rather than folded into the last bin.
	[[nodiscard]] EvaluationError BuildEchogram(const SolveResult& solve,
		const EchogramSettings& settings, Echogram& output);
	[[nodiscard]] EvaluationError BuildEchogram(std::span<const EchogramArrival> arrivals,
		const EchogramSettings& settings, Echogram& output);
}
