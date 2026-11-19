// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Scene/Entity.h"
#include "Lux/UI/UIInput.h"
#include "Lux/UI/UILayout.h"

#include <glm/glm.hpp>

#include <vector>

namespace Lux
{
	struct FrameRenderPacket;
	class Scene;
}

// Game UI over a scene's canvas entities (docs/GAME_UI_PLAN.md). Runs on the main thread, reading the registry.
namespace Lux::UI
{
	struct ResolvedElement
	{
		Entity Element;
		Rect ScreenRect;       // screen pixels, origin bottom left
		bool Clipped = false;  // inside a scroll list: only the part within Clip shows
		Rect Clip;
		float ScrollRange = 0.0f; // a scroll list: how far its content can move, in UI units
	};

	struct ResolvedCanvas
	{
		Entity Canvas;
		float Scale = 1.0f;          // pixels per UI unit
		Rect CanvasRect;             // UI units
		std::vector<ResolvedElement> Elements; // parents before children, siblings in hierarchy order
	};

	// Lays out every enabled canvas for a screen, lowest SortOrder first.
	std::vector<ResolvedCanvas> ResolveLayout(Scene& scene, const glm::vec2& screenSize);

	// Turns resolved canvases into draw items in packet.UI.
	void CaptureDrawItems(const std::vector<ResolvedCanvas>& canvases, const glm::vec2& screenSize, FrameRenderPacket& packet);

	// One frame of pointer and keyboard input: hover, press, click, drag a slider, edit a text input, scroll a list.
	// Updates the widgets' values and runtime flags and returns what happened, in order.
	std::vector<Event> UpdateInteraction(Scene& scene, InteractionState& state, const glm::vec2& screenSize, const FrameInput& input);

	// Fills the editing keys from the keyboard with key repeat (held keys act again after a short delay).
	Keys ReadEditKeys(float deltaTime, float (&heldTime)[8]);
}
