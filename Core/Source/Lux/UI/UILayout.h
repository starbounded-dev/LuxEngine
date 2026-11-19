// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Game UI layout (docs/GAME_UI_PLAN.md). Pure functions over plain data: no registry, no GPU, any thread.
// Origin is the bottom-left of the canvas and y points up, like the runtime overlay projection.
namespace Lux::UI
{
	struct Rect
	{
		glm::vec2 Min{ 0.0f };
		glm::vec2 Max{ 0.0f };

		glm::vec2 Size() const { return Max - Min; }
		glm::vec2 Center() const { return (Min + Max) * 0.5f; }
		bool Contains(const glm::vec2& point) const { return point.x >= Min.x && point.x <= Max.x && point.y >= Min.y && point.y <= Max.y; }
		bool operator==(const Rect&) const = default;
	};

	// How a canvas turns its reference resolution into pixels.
	enum class ScaleMode : uint8_t
	{
		ConstantPixelSize = 0, // Scale is ScaleFactor, the screen size does not matter.
		ScaleWithScreen,       // Scale follows the screen against the reference resolution, see Match.
		FitInside,             // The whole reference area fits on screen.
		FillScreen,            // The reference area covers the whole screen.
	};

	// The canvas scale (pixels per UI unit) for a screen. Match blends width (0) and height (1) in
	// ScaleWithScreen, on a log scale so doubling the width and halving it cancel out.
	float CanvasScale(ScaleMode mode, const glm::vec2& referenceResolution, float scaleFactor, float match, const glm::vec2& screenSize);

	// The canvas rectangle in UI units: the screen divided by the scale.
	Rect CanvasRect(const glm::vec2& screenSize, float scale);

	// A rectangle placed inside its parent. The anchors are fractions of the parent (0,0 bottom-left,
	// 1,1 top-right). OffsetMin and OffsetMax are distances in UI units from the min and max anchor
	// corners, so equal anchors make them a position and a size, and stretched anchors make them margins.
	struct RectTransform
	{
		glm::vec2 AnchorMin{ 0.5f };
		glm::vec2 AnchorMax{ 0.5f };
		glm::vec2 OffsetMin{ -50.0f, -50.0f };
		glm::vec2 OffsetMax{ 50.0f, 50.0f };
	};

	Rect ResolveRect(const RectTransform& transform, const Rect& parent);

	enum class LayoutKind : uint8_t { None = 0, Horizontal, Vertical, Grid };
	enum class LayoutAlign : uint8_t { Start = 0, Center, End };

	// A group places its children, replacing their own anchors and offsets.
	struct LayoutGroup
	{
		LayoutKind Kind = LayoutKind::None;
		glm::vec4 Padding{ 0.0f }; // left, bottom, right, top
		float Spacing = 0.0f;
		glm::vec2 CellSize{ 100.0f, 100.0f };   // Grid
		int32_t Columns = 2;                    // Grid
		LayoutAlign Align = LayoutAlign::Start; // across the stack direction; Start is the top of a row, the left of a column
		bool ExpandChildren = false;            // stacks: children share the spare length equally
	};

	// What a child asks of a group: its own size (from its offsets) and its share of the spare length.
	struct LayoutChild
	{
		glm::vec2 PreferredSize{ 100.0f };
		float Flex = 0.0f; // stacks
	};

	// The rectangle of each child inside the content area. Rows run left to right, columns top to bottom,
	// grids fill rows from the top left. A None group returns empty rectangles: the caller keeps each
	// child's own.
	std::vector<Rect> LayoutChildren(const LayoutGroup& group, const Rect& content, const std::vector<LayoutChild>& children);

	// The size a group needs for its children, for scroll content.
	glm::vec2 PreferredGroupSize(const LayoutGroup& group, const std::vector<LayoutChild>& children);
}
