// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cmath>
#include <cstdint>

namespace Lux::Soundwave
{

	// Work limits are part of the reference algorithm, not elapsed-time cutoffs. Validation
	// and GPU execution must enumerate the same candidates in the same order.
	struct Settings
	{
		std::uint32_t ReflectionDepth = 2;
		std::uint32_t OpeningGridSize = 8;
		std::uint32_t MaxBounces = 2;
		std::uint32_t CandidateBudget = 4096;
		float Strength = 1.0f;
		// Fraction of the authored material absorption applied at a reflection. This does
		// not apply another distance fade to the direct signal.
		float Absorption = 1.0f;

		bool Validate() const
		{
			return MaxBounces >= 1 && MaxBounces <= 16 && ReflectionDepth <= MaxBounces &&
				OpeningGridSize >= 1 && OpeningGridSize <= 32 && CandidateBudget >= 1 &&
				CandidateBudget <= 65536 && std::isfinite(Strength) && Strength >= 0.0f &&
				Strength <= 4.0f && std::isfinite(Absorption) && Absorption >= 0.0f && Absorption <= 1.0f;
		}
	};

}
