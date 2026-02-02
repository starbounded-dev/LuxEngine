#pragma once

#include "StarEngine/Core/KeyCodes.h"
#include "StarEngine/Core/MouseCodes.h"

#include <glm/glm.hpp>

namespace StarEngine {

	class Input
	{
	public:
		static bool IsKeyPressed(KeyCode key);
		static bool IsMouseButtonPressed(MouseCode button);

		static glm::vec2 GetMousePosition();

		static float GetMouseX();
		static float GetMouseY();
	};

}
