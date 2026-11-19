// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "UIEditor.h"

#include "Lux/Scene/Components.h"

#include <algorithm>
#include <cmath>

namespace Lux::UI
{
	namespace
	{
		struct Found
		{
			const ResolvedElement* Element = nullptr;
			float Scale = 1.0f;
		};

		Found Find(const std::vector<ResolvedCanvas>& canvases, UUID id)
		{
			if (!id)
				return {};
			for (const ResolvedCanvas& canvas : canvases)
			{
				for (const ResolvedElement& element : canvas.Elements)
				{
					if (element.Element.GetUUID() == id)
						return { &element, canvas.Scale };
				}
			}
			return {};
		}
	}

	bool EditSession::IsLayoutControlled(Entity element)
	{
		Entity parent = element.GetParent(); // not const: Entity instantiates its const accessors only for the engine's own uses
		return parent && parent.HasComponent<UIElementComponent>() && parent.GetComponent<UIElementComponent>().Layout.Kind != LayoutKind::None;
	}

	bool EditSession::Nudge(Entity element, const glm::vec2& delta)
	{
		if (!element || !element.HasComponent<UIElementComponent>() || IsLayoutControlled(element))
			return false;
		UIElementComponent& component = element.GetComponent<UIElementComponent>();
		component.Rect = DragRect(component.Rect, DragHandle::Body, glm::round(delta));
		return true;
	}

	EditOutput EditSession::Finish()
	{
		EditOutput output;
		if (m_Handle != DragHandle::None && m_Changed && m_Entity)
		{
			output.Ended = true;
			output.EndedEntity = m_Entity;
			output.EndedHandle = m_Handle;
		}
		m_Handle = DragHandle::None;
		m_Entity = 0;
		m_Changed = false;
		return output;
	}

	EditOutput EditSession::Update(const std::vector<ResolvedCanvas>& canvases, const EditInput& input)
	{
		EditOutput output;

		// The element being edited: the dragged one, else the selected one if it is in a canvas.
		Found selected = Find(canvases, m_Handle != DragHandle::None ? m_Entity : input.Selected);

		// The topmost element under the pointer: later canvases and later elements draw over earlier ones.
		if (input.Inside)
		{
			for (auto canvas = canvases.rbegin(); canvas != canvases.rend() && !output.Under; ++canvas)
			{
				for (auto element = canvas->Elements.rbegin(); element != canvas->Elements.rend(); ++element)
				{
					if (element->ScreenRect.Contains(input.Point) && (!element->Clipped || element->Clip.Contains(input.Point)))
					{
						output.Under = &*element;
						break;
					}
				}
			}
		}

		if (m_Handle != DragHandle::None)
		{
			if (input.Down && selected.Element)
			{
				// Whole units, so an edit never leaves a fraction behind.
				const glm::vec2 delta = glm::round((input.Point - m_Start) / selected.Scale);
				Entity entity = selected.Element->Element;
				UIElementComponent& component = entity.GetComponent<UIElementComponent>();
				const RectTransform dragged = DragRect(m_StartRect, m_Handle, delta);
				if (dragged.OffsetMin != component.Rect.OffsetMin || dragged.OffsetMax != component.Rect.OffsetMax)
				{
					component.Rect = dragged;
					m_Changed = true;
				}
			}
			else
			{
				const ResolvedElement* under = output.Under;
				output = Finish();
				output.Under = under;
			}
		}
		else if (input.Inside && input.Pressed)
		{
			DragHandle handle = DragHandle::None;
			if (selected.Element)
				handle = PickHandle(selected.Element->ScreenRect, input.Point, input.Grab);

			Found target = selected;
			if (handle == DragHandle::None && output.Under)
			{
				target = Find(canvases, output.Under->Element.GetUUID());
				handle = DragHandle::Body;
				output.Select = target.Element->Element.GetUUID();
				selected = target; // selected even when its layout keeps it from being dragged
			}

			Entity targetEntity = target.Element ? target.Element->Element : Entity{};
			if (target.Element && handle != DragHandle::None && !IsLayoutControlled(targetEntity))
			{
				m_Handle = handle;
				m_Entity = targetEntity.GetUUID();
				m_Start = input.Point;
				m_StartRect = targetEntity.GetComponent<UIElementComponent>().Rect;
				m_Changed = false;
			}
		}

		output.Selected = selected.Element;
		output.SelectedScale = selected.Scale;
		output.LayoutControlled = selected.Element && IsLayoutControlled(selected.Element->Element);
		if (m_Handle == DragHandle::None && input.Inside && selected.Element && !output.LayoutControlled)
			output.Hover = PickHandle(selected.Element->ScreenRect, input.Point, input.Grab);
		else if (m_Handle != DragHandle::None)
			output.Hover = m_Handle;
		return output;
	}
}
