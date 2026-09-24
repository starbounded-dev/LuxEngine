// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/UUID.h"
#include <glm/glm.hpp>
#include <array>

namespace Lux
{
	// Body IDs include Jolt's sequence number. Subshape IDs distinguish compound contacts.
	struct PhysicsContactEvent
	{
		enum class Type { Begin, Persist, End };
		Type State = Type::Begin;
		std::array<uint32_t, 4> Key{};
		UUID Entity1 = 0, Entity2 = 0;
		glm::vec3 Position{ 0.0f };
		float EstimatedImpulse = 0.0f;
		float SlipSpeed = 0.0f;
		float RollSpeed = 0.0f;
		float Mass1 = 0.0f, Mass2 = 0.0f;
	};
}
