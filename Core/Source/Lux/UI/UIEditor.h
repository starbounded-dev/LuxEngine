// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/UI/UISystem.h"

#include <glm/glm.hpp>

#include <vector>

// The logic of editing a user interface by pointer in the Game View (docs/GAME_UI_PLAN.md, U5 and E9): what a click selects, what a
// drag changes. No ImGui and no editor state, so it is checked headless; the panel maps its mouse and draws the outlines.
namespace Lux::UI
{
	struct EditInput
	{
		glm::vec2 Point{ 0.0f }; // the pointer in view pixels (origin bottom left); may be outside the view while dragging
		bool Inside = false;     // the pointer is over the view and not over something else
		bool Down = false;       // the primary button is held
		bool Pressed = false;    // it went down this frame
		UUID Selected = 0;       // the first selected entity
		float Grab = 8.0f;       // how near an edge or corner picks it, in view pixels
	};

	struct EditOutput
	{
		const ResolvedElement* Selected = nullptr;  // the element being edited, in the layout passed in
		float SelectedScale = 1.0f;                 // its canvas's pixels per UI unit
		const ResolvedElement* Under = nullptr;     // the topmost element under the pointer
		UUID Select = 0;                            // select this entity (a click picked it)
		DragHandle Hover = DragHandle::None;        // the handle under the pointer, for the cursor
		bool LayoutControlled = false;              // the selected element's parent places it, so it cannot be dragged
		bool Ended = false;                         // a drag finished with a change: record one undo step
		UUID EndedEntity = 0;
		DragHandle EndedHandle = DragHandle::None;
	};

	class EditSession
	{
	public:
		// One frame. `canvases` is ResolveLayout for the view. Changes the offsets of the dragged element.
		EditOutput Update(const std::vector<ResolvedCanvas>& canvases, const EditInput& input);

		// Ends a drag in progress (the mode was switched off), reporting it as Update does when the button is released.
		EditOutput Finish();

		bool IsDragging() const { return m_Handle != DragHandle::None; }
		DragHandle GetHandle() const { return m_Handle; }

		// Moves a fixed-size or stretched element by whole units, as an arrow key does. False when its parent's layout places it.
		static bool Nudge(Entity element, const glm::vec2& delta);
		static bool IsLayoutControlled(Entity element);

	private:
		DragHandle m_Handle = DragHandle::None;
		UUID m_Entity = 0;
		glm::vec2 m_Start{ 0.0f }; // the pointer when the drag began, in view pixels
		RectTransform m_StartRect;
		bool m_Changed = false;
	};
}
