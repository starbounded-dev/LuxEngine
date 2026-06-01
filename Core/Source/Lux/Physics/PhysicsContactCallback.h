#pragma once

#include "Lux/Core/UUID.h"

namespace Lux {

	enum class ContactType : int8_t
	{
		None = -1,
		CollisionBegin,
		CollisionEnd,
		TriggerBegin,
		TriggerEnd
	};

}
