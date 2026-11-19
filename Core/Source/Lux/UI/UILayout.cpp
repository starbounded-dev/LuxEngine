// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "UILayout.h"

#include <algorithm>
#include <cmath>

namespace Lux::UI
{
	float CanvasScale(ScaleMode mode, const glm::vec2& referenceResolution, float scaleFactor, float match, const glm::vec2& screenSize)
	{
		if (mode == ScaleMode::ConstantPixelSize)
			return std::max(scaleFactor, 0.01f);

		const glm::vec2 reference = glm::max(referenceResolution, glm::vec2(1.0f));
		const float widthScale = std::max(screenSize.x, 1.0f) / reference.x;
		const float heightScale = std::max(screenSize.y, 1.0f) / reference.y;
		switch (mode)
		{
			case ScaleMode::FitInside:  return std::min(widthScale, heightScale);
			case ScaleMode::FillScreen: return std::max(widthScale, heightScale);
			default:
			{
				const float t = std::clamp(match, 0.0f, 1.0f);
				return std::exp2(std::lerp(std::log2(widthScale), std::log2(heightScale), t));
			}
		}
	}

	Rect CanvasRect(const glm::vec2& screenSize, float scale)
	{
		return { glm::vec2(0.0f), screenSize / std::max(scale, 1e-4f) };
	}

	Rect ResolveRect(const RectTransform& transform, const Rect& parent)
	{
		const glm::vec2 size = parent.Size();
		return {
			parent.Min + size * transform.AnchorMin + transform.OffsetMin,
			parent.Min + size * transform.AnchorMax + transform.OffsetMax };
	}

	namespace
	{
		float AlignOffset(LayoutAlign align, float room)
		{
			switch (align)
			{
				case LayoutAlign::Center: return room * 0.5f;
				case LayoutAlign::End:    return room;
				default:                  return 0.0f;
			}
		}
	}

	std::vector<Rect> LayoutChildren(const LayoutGroup& group, const Rect& content, const std::vector<LayoutChild>& children)
	{
		std::vector<Rect> result(children.size());
		if (children.empty() || group.Kind == LayoutKind::None)
			return result;

		const Rect inner{ content.Min + glm::vec2(group.Padding.x, group.Padding.y), content.Max - glm::vec2(group.Padding.z, group.Padding.w) };
		const glm::vec2 innerSize = glm::max(inner.Size(), glm::vec2(0.0f));

		if (group.Kind == LayoutKind::Grid)
		{
			const int32_t columns = std::max(group.Columns, 1);
			for (size_t i = 0; i < children.size(); ++i)
			{
				const int32_t column = (int32_t)i % columns;
				const int32_t row = (int32_t)i / columns;
				const float x = inner.Min.x + column * (group.CellSize.x + group.Spacing);
				const float top = inner.Max.y - row * (group.CellSize.y + group.Spacing);
				result[i] = { { x, top - group.CellSize.y }, { x + group.CellSize.x, top } };
			}
			return result;
		}

		const bool horizontal = group.Kind == LayoutKind::Horizontal;
		const int axis = horizontal ? 0 : 1;
		const int cross = 1 - axis;

		float used = group.Spacing * (float)(children.size() - 1);
		float flexTotal = 0.0f;
		for (const LayoutChild& child : children)
		{
			used += std::max(child.PreferredSize[axis], 0.0f);
			flexTotal += group.ExpandChildren ? std::max(child.Flex, 1.0f) : std::max(child.Flex, 0.0f);
		}
		const float spare = std::max(innerSize[axis] - used, 0.0f);

		// A column runs from the top down, so its cursor starts at the max edge and moves down.
		float cursor = horizontal ? inner.Min.x : inner.Max.y;
		for (size_t i = 0; i < children.size(); ++i)
		{
			const LayoutChild& child = children[i];
			float length = std::max(child.PreferredSize[axis], 0.0f);
			const float flex = group.ExpandChildren ? std::max(child.Flex, 1.0f) : std::max(child.Flex, 0.0f);
			if (flexTotal > 0.0f)
				length += spare * flex / flexTotal;

			const float crossLength = std::min(std::max(child.PreferredSize[cross], 0.0f), innerSize[cross]);
			const float crossRoom = innerSize[cross] - crossLength;
			if (horizontal)
			{
				const float y = inner.Min.y + crossRoom - AlignOffset(group.Align, crossRoom); // Start is the top
				result[i] = { { cursor, y }, { cursor + length, y + crossLength } };
				cursor += length + group.Spacing;
			}
			else
			{
				const float x = inner.Min.x + AlignOffset(group.Align, crossRoom);
				result[i] = { { x, cursor - length }, { x + crossLength, cursor } };
				cursor -= length + group.Spacing;
			}
		}
		return result;
	}

	glm::vec2 PreferredGroupSize(const LayoutGroup& group, const std::vector<LayoutChild>& children)
	{
		glm::vec2 size{ 0.0f };
		if (!children.empty())
		{
			switch (group.Kind)
			{
				case LayoutKind::Horizontal:
				case LayoutKind::Vertical:
				{
					const int axis = group.Kind == LayoutKind::Horizontal ? 0 : 1;
					for (const LayoutChild& child : children)
					{
						size[axis] += std::max(child.PreferredSize[axis], 0.0f);
						size[1 - axis] = std::max(size[1 - axis], child.PreferredSize[1 - axis]);
					}
					size[axis] += group.Spacing * (float)(children.size() - 1);
					break;
				}
				case LayoutKind::Grid:
				{
					const int32_t columns = std::max(group.Columns, 1);
					const int32_t used = std::min((int32_t)children.size(), columns);
					const int32_t rows = ((int32_t)children.size() + columns - 1) / columns;
					size = { used * group.CellSize.x + (used - 1) * group.Spacing, rows * group.CellSize.y + (rows - 1) * group.Spacing };
					break;
				}
				default:
					break;
			}
		}
		return size + glm::vec2(group.Padding.x + group.Padding.z, group.Padding.y + group.Padding.w);
	}

	DragHandle PickHandle(const Rect& screenRect, const glm::vec2& point, float grab)
	{
		const glm::vec2 size = screenRect.Size();
		// A handle never reaches past a third of the rectangle, so the body stays reachable on a small one.
		const glm::vec2 reach = glm::min(glm::vec2(grab), size / 3.0f);
		const bool left = std::abs(point.x - screenRect.Min.x) <= reach.x, right = std::abs(point.x - screenRect.Max.x) <= reach.x;
		const bool bottom = std::abs(point.y - screenRect.Min.y) <= reach.y, top = std::abs(point.y - screenRect.Max.y) <= reach.y;
		const bool withinX = point.x >= screenRect.Min.x - reach.x && point.x <= screenRect.Max.x + reach.x;
		const bool withinY = point.y >= screenRect.Min.y - reach.y && point.y <= screenRect.Max.y + reach.y;
		if (!withinX || !withinY)
			return DragHandle::None;

		if (left && bottom) return DragHandle::BottomLeft;
		if (right && bottom) return DragHandle::BottomRight;
		if (left && top) return DragHandle::TopLeft;
		if (right && top) return DragHandle::TopRight;
		if (left) return DragHandle::Left;
		if (right) return DragHandle::Right;
		if (bottom) return DragHandle::Bottom;
		if (top) return DragHandle::Top;
		return screenRect.Contains(point) ? DragHandle::Body : DragHandle::None;
	}

	RectTransform DragRect(const RectTransform& start, DragHandle handle, const glm::vec2& delta)
	{
		RectTransform result = start;
		const bool moves = handle == DragHandle::Body;
		const bool left = moves || handle == DragHandle::Left || handle == DragHandle::BottomLeft || handle == DragHandle::TopLeft;
		const bool right = moves || handle == DragHandle::Right || handle == DragHandle::BottomRight || handle == DragHandle::TopRight;
		const bool bottom = moves || handle == DragHandle::Bottom || handle == DragHandle::BottomLeft || handle == DragHandle::BottomRight;
		const bool top = moves || handle == DragHandle::Top || handle == DragHandle::TopLeft || handle == DragHandle::TopRight;
		if (handle == DragHandle::None)
			return result;

		if (left) result.OffsetMin.x += delta.x;
		if (right) result.OffsetMax.x += delta.x;
		if (bottom) result.OffsetMin.y += delta.y;
		if (top) result.OffsetMax.y += delta.y;

		// A fixed-size axis keeps at least one unit; a stretched one has margins, which may be anything.
		if (!moves)
		{
			for (int axis = 0; axis < 2; ++axis)
			{
				if (start.AnchorMin[axis] != start.AnchorMax[axis])
					continue;
				if (result.OffsetMax[axis] - result.OffsetMin[axis] < 1.0f)
				{
					const bool draggedMin = axis == 0 ? left : bottom;
					if (draggedMin)
						result.OffsetMin[axis] = result.OffsetMax[axis] - 1.0f;
					else
						result.OffsetMax[axis] = result.OffsetMin[axis] + 1.0f;
				}
			}
		}
		return result;
	}
}
