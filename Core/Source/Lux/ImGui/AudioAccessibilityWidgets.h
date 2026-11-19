// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include "Lux/Audio/AudioAccessibilitySettings.h"
#include <glm/glm.hpp>

namespace Lux::ImGuiEx
{
	bool AudioAccessibilityOptions(AudioAccessibilityPreferences& preferences, bool runtime);
	void AudioAccessibilityMenu(bool& open);
}
