// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
// Soundwave — the acoustic kernel. Apache-2.0, the same licence as the rest of Lux.

#include "soundwave/AcousticMaterialSettings.h"

#include <cmath>

namespace Lux { namespace Soundwave {

// Tag order matches the engine's material enum, which is the authoring vocabulary: an author
// types a name, the scene resolves a mesh to that name, and the tag index reaches this row.
//
// { AbsorptionLF, AbsorptionHF, Scattering, TransmissionLF, TransmissionHF, FlatTransmissionLF,
//   FlatTransmissionHF }. Row 0 is Default. Captured from the engine's own accessor on
// 2026-10-31; every literal is the exact float that accessor returned, so Phase 1's shipped
// numbers are unchanged by this table existing at all.
static const std::array<MaterialProperties, kMaterialTagCount> kDefaults{{
	{ 0.31f, 0.55f, 0.40f, 0.60f, 0.30f, 0.10f, 0.25f }, // Default
	{ 0.31f, 0.52f, 0.40f, 0.50f, 0.30f, 0.10f, 0.25f }, // Brick
	{ 0.72f, 0.91f, 0.70f, 3.00f, 0.50f, 0.10f, 0.25f }, // Carpet
	{ 0.72f, 0.91f, 0.70f, 3.00f, 0.50f, 0.10f, 0.25f }, // Cloth
	{ 0.31f, 0.55f, 0.40f, 0.60f, 0.30f, 0.10f, 0.25f }, // Concrete
	{ 0.06f, 0.12f, 0.10f, 0.60f, 0.30f, 0.10f, 0.25f }, // ConcretePolished
	{ 0.48f, 0.88f, 0.50f, 1.50f, 0.40f, 0.10f, 0.25f }, // Dirt
	{ 0.08f, 0.12f, 0.05f, 0.20f, 0.05f, 0.10f, 0.25f }, // Glass
	{ 0.71f, 0.93f, 0.80f, 4.30f, 0.90f, 0.10f, 0.25f }, // Grass
	{ 0.06f, 0.12f, 0.70f, 1.00f, 0.30f, 0.10f, 0.25f }, // Gravel
	{ 0.06f, 0.12f, 0.03f, 0.50f, 0.20f, 0.10f, 0.25f }, // Marble
	{ 0.05f, 0.02f, 0.01f, 0.10f, 0.05f, 0.10f, 0.25f }, // Metal
	{ 0.04f, 0.08f, 0.05f, 0.40f, 0.20f, 0.10f, 0.25f }, // Plaster
	{ 0.12f, 0.30f, 0.10f, 0.40f, 0.20f, 0.10f, 0.25f }, // Plastic
	{ 0.31f, 0.51f, 0.40f, 0.60f, 0.30f, 0.10f, 0.25f }, // Rock
	{ 0.65f, 0.92f, 0.70f, 3.00f, 0.60f, 0.10f, 0.25f }, // Snow
	{ 0.56f, 0.82f, 0.50f, 1.20f, 0.40f, 0.10f, 0.25f }, // Soil
	{ 0.06f, 0.12f, 0.10f, 0.50f, 0.20f, 0.10f, 0.25f }, // Water
	{ 0.16f, 0.37f, 0.20f, 0.50f, 0.20f, 0.10f, 0.25f }, // Wood
	{ 0.12f, 0.30f, 0.10f, 0.40f, 0.20f, 0.10f, 0.25f }, // WoodThin
	{ 0.06f, 0.12f, 0.03f, 0.50f, 0.20f, 0.10f, 0.25f }, // Ceramic
	{ 0.72f, 0.91f, 0.70f, 3.00f, 0.50f, 0.10f, 0.25f }, // Rubber
	{ 0.71f, 0.91f, 0.80f, 5.00f, 1.00f, 0.10f, 0.25f }, // Foliage
}};

bool IsValidMaterialProperties(const MaterialProperties& properties)
{
	const auto fraction = [](const float value)
	{
		return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
	};
	return fraction(properties.AbsorptionLF) && fraction(properties.AbsorptionHF) && fraction(properties.Scattering)
		&& std::isfinite(properties.TransmissionLF) && properties.TransmissionLF > 0.0f
		&& std::isfinite(properties.TransmissionHF) && properties.TransmissionHF > 0.0f
		&& std::isfinite(properties.FlatTransmissionLF) && properties.FlatTransmissionLF >= 0.0f
		&& std::isfinite(properties.FlatTransmissionHF) && properties.FlatTransmissionHF >= 0.0f;
}

const MaterialProperties& GetDefaultMaterialProperties(MaterialTag tag)
{
	return kDefaults[static_cast<std::size_t>(tag) < kMaterialTagCount ? static_cast<std::size_t>(tag) : 0];
}

} }
