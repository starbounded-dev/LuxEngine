#pragma once
#include "Lux/Audio/AudioAccessibilitySettings.h"
#include <glm/glm.hpp>

namespace Lux::ImGuiEx
{
	bool AudioAccessibilityOptions(AudioAccessibilityPreferences& preferences, bool runtime);
	void AudioAccessibilityOverlay(const glm::vec2& minimum, const glm::vec2& maximum);
	void AudioAccessibilityMenu(bool& open);
}
