// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Audio/AudioAccessibility.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Lux
{
	struct FrameRenderPacket;
}

// Subtitles, closed captions and visual sound cues drawn by the game UI (docs/GAME_UI_PLAN.md, U4), so they show in an
// exported game that has no ImGui and in the editor's Game View alike.
namespace Lux::UI
{
	struct CaptionEntry
	{
		std::string Text; // direction indicator and speaker name included
		glm::vec4 Color{ 1.0f };
		int Lines = 1;    // lines it takes at the current width
	};

	// The entries to show, oldest first: the newest ones that fit in Preferences.MaxLines (the newest always shows). lineCount
	// says how many lines a text takes; directionTo turns a world position into a direction from the listener.
	std::vector<CaptionEntry> CollectCaptions(const std::vector<AccessibleSubtitle>& subtitles, const AudioAccessibilityPreferences& preferences,
		const std::function<int(const std::string&)>& lineCount, const std::function<glm::vec3(const glm::vec3&)>& directionTo);

	// Adds the captions box and the sound cues for the current accessibility state to packet.UI.
	void CaptureAccessibility(const glm::vec2& screenSize, FrameRenderPacket& packet);
}
