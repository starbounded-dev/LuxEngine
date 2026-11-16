// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
// Soundwave — the acoustic kernel. Apache-2.0, the same licence as the rest of Lux.
// This library includes nothing from Lux: it owns the numbers, the engine owns the names
// it shows authors and the entity mapping that turns a mesh into a tag.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Lux { namespace Soundwave {

// One acoustic surface tag. The engine's material enum maps onto these by position, and the
// two are pinned equal where the seam is configured; the library never sees an entity.
using MaterialTag = std::uint8_t;

inline constexpr std::size_t kMaterialTagCount = 23;

struct MaterialProperties
{
	// Energy lost per grazing pass through the surface, as a fraction (0 = no loss).
	float AbsorptionLF = 0.0f;
	float AbsorptionHF = 0.0f;
	// How much of a hit spreads rather than arriving whole: the bounce budget's input.
	float Scattering = 0.0f;
	// Metres of closed geometry that cost 30 dB of the energy — VA's published semantics,
	// which the engine's own occlusion path already reproduces (AudioOcclusion.cpp:14-16).
	float TransmissionLF = 1.0f;
	float TransmissionHF = 1.0f;
	// Energy fraction lost on a surface the path enters but never leaves (one-sided, thin).
	float FlatTransmissionLF = 0.0f;
	float FlatTransmissionHF = 0.0f;
};

// The rows are authored, not read from an acoustics SDK at run time: they were captured once from
// what the engine measured today and are kept as literals, so a shipped number cannot drift on its
// own. OcclusionTests compares every field against the pre-migration engine accessor.
bool IsValidMaterialProperties(const MaterialProperties& properties);
// Out-of-range tags read the Default row.
const MaterialProperties& GetDefaultMaterialProperties(MaterialTag tag);

} }
