// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
// Soundwave — the acoustic kernel. Apache-2.0, the same licence as the rest of Lux.

#pragma once

#include <cstdint>
#include <vector>

#include "soundwave/AcousticMaterialSettings.h"

namespace Lux { namespace Soundwave {

// The semantics the material table's Transmission columns are written against: a solid that
// thick costs 30 dB, and a path never carries more than the cap.
inline constexpr float kTransmissionLossDb = 30.0f;
inline constexpr float kMinTransmissionMetres = 0.001f;
inline constexpr float kMaxLossDb = 60.0f;

// Where a sound starts and ends. A plain struct so the same fields can back a compute buffer
// later; the engine hands its own vector type across at the seam.
struct Position
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
};

// One surface the path crosses, in the order the crossings are walked. This is the whole
// description of "a source at a listener": every number the shipped gains come from.
struct Crossing
{
	MaterialTag Material = 0;
	float Start = 0.0f; // metres along the path where the crossing begins
	float End = 0.0f;   // metres along where it ends; equals Start when nothing was left
	float LossLF = 0.0f; // dB already priced in, low frequency
	float LossHF = 0.0f; // dB, high frequency
	bool OneSided = false; // entered but never left: thin, open or non-watertight geometry
	bool Portal = false;  // a shutter that swings, so its cost changes with how open it is
};

struct Path
{
	Position From{}; // the listener
	Position To{};   // the source
	std::vector<Crossing> Crossings;
	float Distance = 0.0f;  // metres, listener to source
	std::uint32_t Bounces = 0; // reflection candidates the solve accepted
	float LossLF = 0.0f; // dB, capped
	float LossHF = 0.0f; // dB, capped

	void Reset(const Position& from, const Position& to, float distance);
	void AddCrossing(const Crossing& crossing);
	// Fixed reduction order: the crossings are summed in the order they were added, which is the
	// order the walk found them, then capped. The oracle sorts once before handing them over, and
	// the GPU port has to reproduce that same order — the authored tolerance (Decision 10) is
	// measured against this call, so the reduction cannot move.
	void Finalize();
	float GainLF() const; // 1 unattenuated, 0 silent
	float GainHF() const;
};

// The loss model, in one place for the CPU solve, the GPU solve and the comparison oracle.
float FlatLossDb(float energyFraction);
float SolidLossDb(float thicknessMetres, float transmissionMetres, float strength);
float GainFromLossDb(float lossDb);

} }
