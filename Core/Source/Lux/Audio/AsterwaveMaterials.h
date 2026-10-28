// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "AcousticMaterial.h"

namespace asterwave { class World; }

namespace Lux
{
	// Asterwave adapter internals, defined in RaytracedAudioScene.cpp and used only there and in its
	// tests. Nothing here exists in a build without --raytraced-audio.
	//
	// Called only on an idle world. Unique custom IDs prevent overrides leaking to other tags.
	// Native coefficients saturate flat losses at 1 and bound tiny transmission distances so
	// 30 / metres stays finite. Authored values and both serialization formats remain unchanged.
	int AcousticMaterialID(AcousticMaterial material);
	bool ConfigureAcousticMaterials(asterwave::World& world, const AcousticMaterialSettings& settings);
}
