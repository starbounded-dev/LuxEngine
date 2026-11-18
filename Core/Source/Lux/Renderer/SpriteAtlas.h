// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/SpriteSheet.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Lux::SpriteAtlas
{
	struct Source
	{
		std::string Name;
		std::filesystem::path File; // a PNG or JPEG
	};

	struct Result
	{
		glm::ivec2 Size{ 0 };
		std::vector<uint8_t> Pixels;     // RGBA8, top row first
		std::vector<SpriteFrame> Frames; // one per source, in source order, named after it
	};

	// Places rectangles of the given sizes in the smallest power-of-two-wide atlas that fits maxSize, in
	// shelves from the tallest down, `padding` texels apart and from the edge. positions[i] is where
	// sizes[i] goes. Returns false when they do not fit or a size is not positive.
	bool PackRects(const std::vector<glm::ivec2>& sizes, int32_t padding, int32_t maxSize, glm::ivec2& atlasSize, std::vector<glm::ivec2>& positions);

	// Decodes the sources and packs them into one image. The editor uses it to build a sprite sheet from a
	// set of single sprites; the game ships the result as an ordinary texture and sheet. Main thread: the
	// image decoder's flip flag is global.
	bool Pack(const std::vector<Source>& sources, int32_t padding, int32_t maxSize, Result& out, std::string& error);

	bool WritePNG(const std::filesystem::path& path, const Result& atlas);
}
