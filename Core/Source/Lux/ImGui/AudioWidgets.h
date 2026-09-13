#pragma once
#include "Lux/Audio/AudioEventRef.h"

namespace Lux::ImGuiEx
{
	// Typed, GUID-based picker over the already-loaded FMOD catalog.
	bool SurfaceEventPicker(const char* label, AudioEventRef& reference, bool oneShot, bool mixed = false);
}
