// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

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
