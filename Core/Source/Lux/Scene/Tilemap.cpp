// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Tilemap.h"

#include "Lux/Renderer/SpriteSheet.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace Lux::Tilemap {

	namespace {

		constexpr float kParallelTolerance = 1e-6f;
		constexpr int32_t kViewMargin = 1;

		bool IsSolid(int32_t tile, const std::vector<SpriteFrame>& frames)
		{
			return tile >= 0 && static_cast<size_t>(tile) < frames.size() && frames[static_cast<size_t>(tile)].Solid;
		}

	}

	int32_t GetTile(const TilemapLayer& layer, int32_t x, int32_t y)
	{
		const auto found = layer.Cells.find(Key(x, y));
		return found != layer.Cells.end() ? found->second : -1;
	}

	bool SetTile(TilemapLayer& layer, int32_t x, int32_t y, int32_t tile)
	{
		if (tile < 0)
			return layer.Cells.erase(Key(x, y)) > 0;

		const auto [it, inserted] = layer.Cells.try_emplace(Key(x, y), tile);
		if (inserted)
			return true;
		const bool changed = it->second != tile;
		it->second = tile;
		return changed;
	}

	bool GetBounds(const TilemapLayer& layer, CellRect& bounds)
	{
		if (layer.Cells.empty())
			return false;

		int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
		for (const auto& [key, tile] : layer.Cells)
		{
			minX = std::min(minX, KeyX(key));
			maxX = std::max(maxX, KeyX(key));
			minY = std::min(minY, KeyY(key));
			maxY = std::max(maxY, KeyY(key));
		}
		bounds = { minX, minY, maxX - minX + 1, maxY - minY + 1 };
		return true;
	}

	size_t FillRect(TilemapLayer& layer, const CellRect& rect, int32_t tile)
	{
		size_t changes = 0;
		for (int32_t y = rect.Y; y < rect.Y + rect.Height; y++)
		{
			for (int32_t x = rect.X; x < rect.X + rect.Width; x++)
				changes += SetTile(layer, x, y, tile) ? 1 : 0;
		}
		return changes;
	}

	size_t FloodFill(TilemapLayer& layer, int32_t x, int32_t y, int32_t tile, const CellRect& limit)
	{
		if (!limit.Contains(x, y))
			return 0;

		const int32_t target = GetTile(layer, x, y);
		const int32_t replacement = tile < 0 ? -1 : tile;
		if (target == replacement)
			return 0;

		// Collect first, then write: the region is read while it is walked.
		std::vector<std::pair<int32_t, int32_t>> stack{ { x, y } };
		std::vector<std::pair<int32_t, int32_t>> region;
		std::map<int64_t, bool> seen;
		seen[Key(x, y)] = true;
		while (!stack.empty())
		{
			const auto [cx, cy] = stack.back();
			stack.pop_back();
			region.emplace_back(cx, cy);
			for (const auto [dx, dy] : { std::pair{ 1, 0 }, std::pair{ -1, 0 }, std::pair{ 0, 1 }, std::pair{ 0, -1 } })
			{
				const int32_t nx = cx + dx, ny = cy + dy;
				if (!limit.Contains(nx, ny) || GetTile(layer, nx, ny) != target || !seen.try_emplace(Key(nx, ny), true).second)
					continue;
				stack.emplace_back(nx, ny);
			}
		}

		for (const auto& [cx, cy] : region)
			SetTile(layer, cx, cy, replacement);
		return region.size();
	}

	void MergeSolid(const TilemapLayer& layer, const std::vector<SpriteFrame>& frames, std::vector<CellRect>& blocks)
	{
		blocks.clear();

		// Solid cells by row, left to right.
		std::map<int32_t, std::vector<int32_t>> rows;
		for (const auto& [key, tile] : layer.Cells)
		{
			if (IsSolid(tile, frames))
				rows[KeyY(key)].push_back(KeyX(key));
		}

		// Runs along X in each row; a run that repeats the previous row's run (same start and length) grows that
		// block upward instead of starting a new one.
		std::map<std::pair<int32_t, int32_t>, size_t> openBlocks; // (x, width) -> block, for the row below
		int32_t previousRow = INT32_MIN;
		for (auto& [y, xs] : rows)
		{
			std::sort(xs.begin(), xs.end());
			if (previousRow == INT32_MIN || y != previousRow + 1)
				openBlocks.clear();

			std::map<std::pair<int32_t, int32_t>, size_t> nextOpen;
			for (size_t i = 0; i < xs.size();)
			{
				size_t end = i + 1;
				while (end < xs.size() && xs[end] == xs[end - 1] + 1)
					end++;
				const std::pair<int32_t, int32_t> run{ xs[i], static_cast<int32_t>(end - i) };
				const auto below = openBlocks.find(run);
				if (below != openBlocks.end())
				{
					blocks[below->second].Height++;
					nextOpen[run] = below->second;
				}
				else
				{
					blocks.push_back({ run.first, y, run.second, 1 });
					nextOpen[run] = blocks.size() - 1;
				}
				i = end;
			}
			openBlocks = std::move(nextOpen);
			previousRow = y;
		}
	}

	bool ComputeVisibleCells(const glm::mat4& viewProjection, const glm::mat4& tilemapWorld, const glm::vec2& cellSize, CellRect& cells)
	{
		if (!(cellSize.x > 0.0f) || !(cellSize.y > 0.0f) || std::abs(glm::determinant(viewProjection)) < 1e-20f || std::abs(glm::determinant(tilemapWorld)) < 1e-20f)
			return false;

		const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
		const glm::mat4 inverseWorld = glm::inverse(tilemapWorld);
		glm::vec2 low(FLT_MAX), high(-FLT_MAX);
		for (const glm::vec2 corner : { glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 1.0f), glm::vec2(-1.0f, 1.0f) })
		{
			glm::vec4 nearPoint = inverseViewProjection * glm::vec4(corner, 0.0f, 1.0f);
			glm::vec4 farPoint = inverseViewProjection * glm::vec4(corner, 1.0f, 1.0f);
			if (nearPoint.w == 0.0f || farPoint.w == 0.0f)
				return false;
			const glm::vec3 origin = glm::vec3(inverseWorld * (nearPoint / nearPoint.w));
			const glm::vec3 direction = glm::vec3(inverseWorld * (farPoint / farPoint.w)) - origin;
			if (std::abs(direction.z) < kParallelTolerance)
				return false;
			const float t = -origin.z / direction.z;
			const glm::vec2 hit = glm::vec2(origin + direction * t);
			low = glm::min(low, hit);
			high = glm::max(high, hit);
		}

		const glm::vec2 lowCell = glm::floor(low / cellSize);
		const glm::vec2 highCell = glm::floor(high / cellSize);
		if (!std::isfinite(lowCell.x) || !std::isfinite(lowCell.y) || !std::isfinite(highCell.x) || !std::isfinite(highCell.y)
			|| std::max(std::abs(lowCell.x), std::abs(lowCell.y)) > 1e9f || std::max(std::abs(highCell.x), std::abs(highCell.y)) > 1e9f)
			return false;

		cells.X = static_cast<int32_t>(lowCell.x) - kViewMargin;
		cells.Y = static_cast<int32_t>(lowCell.y) - kViewMargin;
		cells.Width = static_cast<int32_t>(highCell.x) - cells.X + 1 + kViewMargin;
		cells.Height = static_cast<int32_t>(highCell.y) - cells.Y + 1 + kViewMargin;
		return true;
	}

}
