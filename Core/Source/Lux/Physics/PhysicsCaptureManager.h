// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Ref.h"

namespace Lux {

	class PhysicsCaptureManager : public RefCounted
	{
	public:
		virtual ~PhysicsCaptureManager() = default;
		virtual void BeginCapture() {}
		virtual void EndCapture() {}
	};

}
