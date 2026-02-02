#pragma once

#include "Lux/Core/KeyCodes.h"
#include "Lux/Core/MouseCodes.h"

#include <glm/glm.hpp>

namespace Lux {

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
