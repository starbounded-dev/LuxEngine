// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Scene/Components.h"

#include <vector>

namespace Lux {

	struct SpriteFrame;

	// Editing and querying a TilemapComponent's layers: cells are keyed by (x, y) with y growing up, and a
	// cell holds the index of a frame of the tile set (-1 is empty). The editor's Tile Palette and the C# API
	// both go through these.
	namespace Tilemap
	{
		// A block of cells: X, Y is its lowest cell, Width and Height count cells.
		struct CellRect
		{
			int32_t X = 0, Y = 0, Width = 0, Height = 0;

			bool Contains(int32_t x, int32_t y) const { return x >= X && y >= Y && x < X + Width && y < Y + Height; }
			bool operator==(const CellRect&) const = default;
		};

		inline int64_t Key(int32_t x, int32_t y) { return (static_cast<int64_t>(y) << 32) | static_cast<uint32_t>(x); }
		inline int32_t KeyX(int64_t key) { return static_cast<int32_t>(static_cast<uint32_t>(key)); }
		inline int32_t KeyY(int64_t key) { return static_cast<int32_t>(key >> 32); }

		int32_t GetTile(const TilemapLayer& layer, int32_t x, int32_t y);
		// Puts a tile in a cell, or empties it for a negative tile. Returns whether the cell changed.
		bool SetTile(TilemapLayer& layer, int32_t x, int32_t y, int32_t tile);
		// The smallest block holding every tile; false for an empty layer.
		bool GetBounds(const TilemapLayer& layer, CellRect& bounds);

		// Sets every cell of a block, a negative tile erasing them. Returns how many cells changed.
		size_t FillRect(TilemapLayer& layer, const CellRect& rect, int32_t tile);
		// Replaces the connected region of cells that hold the same tile as the start cell (or are all empty) with
		// `tile`, staying inside `limit` (an empty region has no end, so there has to be one). Returns the changes.
		size_t FloodFill(TilemapLayer& layer, int32_t x, int32_t y, int32_t tile, const CellRect& limit);

		// The solid cells of a layer as few blocks as possible (runs along X, then runs of equal runs along Y):
		// a floor of 200 tiles is one box shape, not 200. A tile is solid when its frame is flagged Solid.
		void MergeSolid(const TilemapLayer& layer, const std::vector<SpriteFrame>& frames, std::vector<CellRect>& blocks);

		// The cells a view could see, for drawing only those: the view's four corner rays meet the tilemap's plane
		// (local z = 0) and the cells they enclose, plus one cell of margin, are returned. False when that cannot be told (a
		// ray misses the plane), in which case everything is drawn.
		bool ComputeVisibleCells(const glm::mat4& viewProjection, const glm::mat4& tilemapWorld, const glm::vec2& cellSize, CellRect& cells);
	}

}
