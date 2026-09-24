// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include "Lux/Audio/AudioEventRef.h"

namespace Lux::ImGuiEx
{
	// Typed, GUID-based picker over the already-loaded FMOD catalog.
	bool SurfaceEventPicker(const char* label, AudioEventRef& reference, bool oneShot, bool mixed = false, bool require2D = false);
}
