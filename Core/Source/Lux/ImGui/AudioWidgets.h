// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include "Lux/Audio/AudioEventRef.h"

namespace Lux::ImGuiEx
{
	// A typed picker for this build's backend: FMOD events by GUID from the loaded catalog, or Sound
	// assets on the native backend. Edits only that backend's field of the reference.
	bool SurfaceEventPicker(const char* label, AudioEventRef& reference, bool oneShot, bool mixed = false, bool require2D = false);
}
