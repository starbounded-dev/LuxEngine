#pragma once

#include "Lux/Physics/PhysicsTypes.h"

#include <glm/glm.hpp>

namespace Lux {

	enum class PhysicsDebugType : uint8_t
	{
		LiveDebug = 0,
		CaptureToFile
	};

	struct PhysicsSettings
	{
		float FixedTimestep = 1.0f / 60.0f;
		glm::vec3 Gravity = { 0.0f, -9.81f, 0.0f };
		uint32_t PositionSolverIterations = 8;
		uint32_t VelocitySolverIterations = 2;
		uint32_t MaxBodies = 1000;
		bool CaptureOnPlay = true;
		PhysicsDebugType CaptureMethod = PhysicsDebugType::LiveDebug;
	};

}
