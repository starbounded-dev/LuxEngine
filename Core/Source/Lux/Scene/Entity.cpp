#include "lpch.h"
#include "Lux/Scene/Entity.h"

namespace Lux {

	Entity::Entity(entt::entity handle, Scene* scene)
		: m_EntityHandle(handle), m_Scene(scene)
	{

	}

}
