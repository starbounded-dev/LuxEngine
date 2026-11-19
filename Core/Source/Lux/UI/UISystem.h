// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Scene/Entity.h"
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
		Rect ScreenRect; // screen pixels, origin bottom left
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
}
