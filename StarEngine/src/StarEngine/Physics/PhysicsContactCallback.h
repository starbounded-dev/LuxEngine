#pragma once

#include "StarEngine/Scene/Entity.h"

#include <functional>

namespace StarEngine {

	enum class ContactType : int8_t { None = -1, CollisionBegin, CollisionEnd, TriggerBegin, TriggerEnd };
	using ContactCallbackFn = std::function<void(ContactType, UUID, UUID)>;

}
