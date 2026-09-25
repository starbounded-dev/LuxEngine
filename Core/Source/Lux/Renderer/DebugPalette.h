// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>

namespace Lux
{
	// Categorical colours for debug overlays that tint geometry by category. The renderer owns one
	// material per entry, created at Init, so a category is only ever an index into this table.
	// Entries follow the AcousticMaterial tag order so the acoustic material view reads naturally
	// (brick red, grass green, glass cyan...); other categorical views may reuse them.
	inline constexpr std::array<glm::vec3, 24> DebugCategoryPalette = {
		glm::vec3{ 0.55f, 0.55f, 0.58f }, // Default
		glm::vec3{ 0.80f, 0.30f, 0.22f }, // Brick
		glm::vec3{ 0.75f, 0.35f, 0.70f }, // Carpet
		glm::vec3{ 0.90f, 0.55f, 0.80f }, // Cloth
		glm::vec3{ 0.70f, 0.68f, 0.62f }, // Concrete
		glm::vec3{ 0.88f, 0.86f, 0.80f }, // ConcretePolished
		glm::vec3{ 0.55f, 0.40f, 0.25f }, // Dirt
		glm::vec3{ 0.35f, 0.90f, 0.95f }, // Glass
		glm::vec3{ 0.40f, 0.80f, 0.30f }, // Grass
		glm::vec3{ 0.62f, 0.58f, 0.50f }, // Gravel
		glm::vec3{ 0.95f, 0.92f, 0.85f }, // Marble
		glm::vec3{ 0.45f, 0.60f, 0.85f }, // Metal
		glm::vec3{ 0.95f, 0.85f, 0.65f }, // Plaster
		glm::vec3{ 0.95f, 0.85f, 0.25f }, // Plastic
		glm::vec3{ 0.50f, 0.45f, 0.42f }, // Rock
		glm::vec3{ 1.00f, 1.00f, 1.00f }, // Snow
		glm::vec3{ 0.40f, 0.30f, 0.18f }, // Soil
		glm::vec3{ 0.20f, 0.45f, 0.95f }, // Water
		glm::vec3{ 0.70f, 0.48f, 0.25f }, // Wood
		glm::vec3{ 0.85f, 0.65f, 0.40f }, // WoodThin
		glm::vec3{ 0.30f, 0.75f, 0.70f }, // Ceramic
		glm::vec3{ 0.25f, 0.25f, 0.28f }, // Rubber
		glm::vec3{ 0.25f, 0.60f, 0.25f }, // Foliage
		glm::vec3{ 1.00f, 0.40f, 0.90f }, // spare
	};
	inline constexpr float DebugCategoryFillAlpha = 0.35f;
}
