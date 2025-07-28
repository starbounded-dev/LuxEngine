#include "sepch.h"
#include "AnimationUtils.h"

namespace StarEngine::Utils {

	/*
	void ReorientAnimation(Entity& entity, const glm::quat& oldRotation, const glm::quat& newRotation)
	{
		if (auto* animComponent = entity.TryGetComponent<AnimationComponent>(); animComponent)
		{
			// work out how much rotation has changed, and apply that to the animation component's root bone transform
			glm::quat deltaRotation = glm::inverse(oldRotation) * newRotation;
			animComponent->RootBoneTransform *= glm::toMat3(deltaRotation);
		}
	}
	*/

}
