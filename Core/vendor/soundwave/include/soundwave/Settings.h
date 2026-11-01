// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cmath>
#include <cstdint>

namespace Lux::Soundwave
{
	inline constexpr std::uint32_t kMaximumInteractions = 16;

	// Work limits are part of the reference algorithm, not elapsed-time cutoffs. Validation
	// and GPU execution must enumerate the same candidates in the same order.
	struct Settings
	{
		std::uint32_t ReflectionDepth = 2;
		std::uint32_t OpeningGridSize = 8;
		std::uint32_t MaxBounces = 2;
		std::uint32_t CandidateBudget = 4096;
		std::uint32_t PathBudget = 64;
		std::uint32_t OpeningNodeBudget = 128;
		float Strength = 1.0f;
		// Fraction of the authored material absorption applied at a reflection. This does
		// not apply another distance fade to the direct signal.
		float Absorption = 1.0f;
		// Listener reverb: rays bounced through the space, and the decay that maps to a full send.
		std::uint32_t ReverbRays = 256;
		std::uint32_t ReverbBounces = 16;
		float ReverbReferenceSeconds = 1.5f; // decay giving a send of 1 - 1/e

		bool Validate() const
		{
			if (ReverbRays < 16 || ReverbRays > 4096 || ReverbBounces < 1 || ReverbBounces > 64 ||
				!std::isfinite(ReverbReferenceSeconds) || ReverbReferenceSeconds < 0.1f || ReverbReferenceSeconds > 10.0f)
				return false;
			return MaxBounces >= 1 && MaxBounces <= kMaximumInteractions && ReflectionDepth <= MaxBounces &&
				OpeningGridSize >= 1 && OpeningGridSize <= 32 && CandidateBudget >= 1 &&
				CandidateBudget <= 65536 && PathBudget >= 1 && PathBudget <= 4096 &&
				OpeningNodeBudget >= 1 && OpeningNodeBudget <= 2048 && std::isfinite(Strength) && Strength >= 0.0f &&
				Strength <= 4.0f && std::isfinite(Absorption) && Absorption >= 0.0f && Absorption <= 1.0f;
		}
	};

}
