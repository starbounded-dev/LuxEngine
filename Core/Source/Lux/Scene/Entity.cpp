#include "sepch.h"
#include "StarEngine/Scene/Entity.h"

namespace StarEngine {

	Entity::Entity(entt::entity handle, Scene* scene)
		: m_EntityHandle(handle), m_Scene(scene)
	{

	}

}
