// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
// Soundwave — the acoustic kernel. Apache-2.0, the same licence as the rest of Lux.

#include "soundwave/AcousticPath.h"

#include <algorithm>
#include <cmath>

namespace Lux { namespace Soundwave {

float FlatLossDb(float energyFraction)
{
	return -10.0f * std::log10(std::max(1.0f - energyFraction, 1e-6f));
}

float SolidLossDb(float thicknessMetres, float transmissionMetres, float strength)
{
	// The association is pinned: (strength * 30) * thickness / transmission. A different grouping
	// rounds differently and would move the last bit of a shipped dB against the comparison oracle.
	const float lossPerMetre = strength * kTransmissionLossDb;
	return lossPerMetre * thicknessMetres / std::max(transmissionMetres, kMinTransmissionMetres);
}

float GainFromLossDb(float lossDb)
{
	return std::pow(10.0f, -lossDb / 20.0f);
}

void Path::Reset(const Position& from, const Position& to, float distance)
{
	From = from;
	To = to;
	// The distance arrives from the caller: the engine already holds a straight-line measure and
	// recomputing it here in another type could differ in the last bit.
	Distance = distance;
	Crossings.clear();
	Bounces = 0;
	LossLF = 0.0f;
	LossHF = 0.0f;
}

void Path::AddCrossing(const Crossing& crossing)
{
	Crossings.push_back(crossing);
}

void Path::Finalize()
{
	// The walk's order is the reduction's order. Sorting here would be a second unstable sort over
	// equal Start distances, which can permute the terms and move the last bit of a shipped dB.
	float lossLF = 0.0f;
	float lossHF = 0.0f;
	for (const Crossing& crossing : Crossings)
	{
		lossLF += crossing.LossLF;
		lossHF += crossing.LossHF;
	}
	LossLF = std::min(lossLF, kMaxLossDb);
	LossHF = std::min(lossHF, kMaxLossDb);
}

float Path::GainLF() const
{
	return GainFromLossDb(LossLF);
}

float Path::GainHF() const
{
	return GainFromLossDb(LossHF);
}

} }
