// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "soundwave/Echogram.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace Lux::Soundwave
{
	namespace
	{
		constexpr float kSpeedOfSound = 343.0f;

		bool ValidPath(float distance, float gainLF, float gainHF, float directDistance)
		{
			return std::isfinite(distance) && distance >= directDistance &&
				std::isfinite(gainLF) && gainLF >= 0.0f && gainLF <= 1.0f &&
				std::isfinite(gainHF) && gainHF >= 0.0f && gainHF <= 1.0f;
		}

		void Add(Echogram& result, float delay, float energyLF, float energyHF,
			const EchogramSettings& settings)
		{
			++result.PathCount;
			if (delay >= settings.WindowSeconds)
			{
				result.WindowLimited = true;
				return;
			}
			if (settings.SmoothingSeconds == 0.0f)
			{
				const auto index = std::min<std::uint32_t>(kEchogramBins - 1,
					static_cast<std::uint32_t>(delay / result.BinSeconds));
				result.Bins[index].EnergyLF += energyLF;
				result.Bins[index].EnergyHF += energyHF;
			}
			else
			{
				// Triangular temporal smoothing preserves each path's total energy, including at
				// the zero-time boundary. Bin centres fix the reduction order on all platforms.
				const auto first = static_cast<std::uint32_t>(std::max(0.0f,
					std::floor((delay - settings.SmoothingSeconds) / result.BinSeconds)));
				const auto last = std::min<std::uint32_t>(kEchogramBins - 1,
					static_cast<std::uint32_t>((delay + settings.SmoothingSeconds) / result.BinSeconds));
				float sum = 0.0f;
				for (std::uint32_t i = first; i <= last; ++i)
				{
					const float centre = (static_cast<float>(i) + 0.5f) * result.BinSeconds;
					sum += std::max(0.0f, 1.0f - std::abs(centre - delay) / settings.SmoothingSeconds);
				}
				if (sum == 0.0f)
				{
					const auto index = std::min<std::uint32_t>(kEchogramBins - 1,
						static_cast<std::uint32_t>(delay / result.BinSeconds));
					result.Bins[index].EnergyLF += energyLF;
					result.Bins[index].EnergyHF += energyHF;
				}
				else
				{
					for (std::uint32_t i = first; i <= last; ++i)
					{
						const float centre = (static_cast<float>(i) + 0.5f) * result.BinSeconds;
						const float weight = std::max(0.0f, 1.0f - std::abs(centre - delay) / settings.SmoothingSeconds) / sum;
						result.Bins[i].EnergyLF += energyLF * weight;
						result.Bins[i].EnergyHF += energyHF * weight;
					}
				}
			}
			result.TotalEnergyLF += energyLF;
			result.TotalEnergyHF += energyHF;
		}
	}

	bool EchogramSettings::Validate() const
	{
		return std::isfinite(WindowSeconds) && WindowSeconds >= 0.1f && WindowSeconds <= 4.0f &&
			std::isfinite(SmoothingSeconds) && SmoothingSeconds >= 0.0f && SmoothingSeconds <= 0.1f;
	}

	EvaluationError BuildEchogram(const SolveResult& solve, const EchogramSettings& settings, Echogram& output)
	{
		if (!settings.Validate())
			return EvaluationError::InvalidSettings;
		const float distance = solve.Direct.Distance;
		const float directLF = solve.Direct.GainLF(), directHF = solve.Direct.GainHF();
		if (!ValidPath(distance, directLF, directHF, 0.0f))
			return EvaluationError::NumericalFailure;
		for (const auto& path : solve.Indirect)
			if (!ValidPath(path.Distance, path.GainLF, path.GainHF, distance))
				return EvaluationError::NumericalFailure;

		Echogram next;
		next.DirectArrivalSeconds = distance / kSpeedOfSound;
		next.BinSeconds = settings.WindowSeconds / static_cast<float>(kEchogramBins);
		Add(next, 0.0f, directLF * directLF, directHF * directHF, settings);
		for (const auto& path : solve.Indirect)
			Add(next, (path.Distance - distance) / kSpeedOfSound,
				path.GainLF * path.GainLF, path.GainHF * path.GainHF, settings);
		output = std::move(next);
		return EvaluationError::None;
	}
}
