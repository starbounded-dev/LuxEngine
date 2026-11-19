// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/UUID.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

// What the game UI reads each frame and what it reports back (docs/GAME_UI_PLAN.md). Plain data, so the UI update is
// checked headless and the editor's Game View can feed it from its own image rectangle.
namespace Lux::UI
{
	// The mouse (or touch), in screen pixels with the origin at the bottom left like the canvas.
	struct Pointer
	{
		glm::vec2 Position{ -1.0f };
		bool Present = false; // false when the cursor is outside the view
		bool Down = false;    // the primary button
		float Scroll = 0.0f;  // wheel notches this frame, positive is wheel up (a list shows earlier items)
	};

	// Editing keys, true on the frame they act (the caller adds key repeat).
	struct Keys
	{
		bool Backspace = false, Delete = false, Left = false, Right = false, Home = false, End = false;
		bool Enter = false, Escape = false;
	};

	struct FrameInput
	{
		Pointer Mouse;
		Keys Edit;
		std::u32string Typed; // characters typed this frame
		float DeltaTime = 0.0f;
	};

	enum class EventKind : uint8_t
	{
		Click,        // a button was pressed and released on itself
		Toggled,      // a toggle changed; Checked holds the new state
		ValueChanged, // a slider moved; Value holds the new value
		TextChanged,  // a text input's text changed
		Submit,       // Enter in a text input
	};

	struct Event
	{
		EventKind Kind = EventKind::Click;
		UUID Element = 0;
		float Value = 0.0f;
		bool Checked = false;
	};

	// Kept by the scene between frames.
	struct InteractionState
	{
		UUID Pressed = 0; // the widget the primary button went down on
		UUID Focus = 0;   // the widget that gets keys
		bool WasDown = false;
		float CaretTime = 0.0f; // seconds since the caret last moved, for blinking
	};
}
