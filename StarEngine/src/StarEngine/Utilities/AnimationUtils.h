#pragma once

#include "StarEngine/Scene/Components.h"
#include "StarEngine/Scene/Entity.h"

#include <glm/gtc/quaternion.hpp>

namespace StarEngine::Utils {

	void ReorientAnimation(Entity& entity, const glm::quat& oldRotation, const glm::quat& newRotation);

}
