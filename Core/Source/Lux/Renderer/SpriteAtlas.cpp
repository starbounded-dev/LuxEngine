// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SpriteAtlas.h"

#include <GLFW/deps/stb_image_write.h>
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <utility>

namespace Lux::SpriteAtlas
{
	namespace
	{
		constexpr int32_t kMinAtlasWidth = 64;

		int32_t NextPowerOfTwo(int32_t value)
		{
			int32_t result = 1;
			while (result < value)
				result <<= 1;
			return result;
		}

		struct DecodedImage
		{
			stbi_uc* Pixels = nullptr;
			int32_t Width = 0;
			int32_t Height = 0;

			DecodedImage() = default;
			DecodedImage(const DecodedImage&) = delete;
			DecodedImage& operator=(const DecodedImage&) = delete;
			DecodedImage(DecodedImage&& other) noexcept : Pixels(std::exchange(other.Pixels, nullptr)), Width(other.Width), Height(other.Height) {}
			~DecodedImage() { stbi_image_free(Pixels); }
		};
	}

	bool PackRects(const std::vector<glm::ivec2>& sizes, int32_t padding, int32_t maxSize, glm::ivec2& atlasSize, std::vector<glm::ivec2>& positions)
	{
		padding = std::max(padding, 0);
		positions.assign(sizes.size(), glm::ivec2(0));
		atlasSize = glm::ivec2(0);
		if (sizes.empty())
			return true;

		for (const glm::ivec2& size : sizes)
		{
			if (size.x < 1 || size.y < 1 || size.x + 2 * padding > maxSize || size.y + 2 * padding > maxSize)
				return false;
		}

		// Tallest first (then widest, then source order) keeps shelves tight and the result deterministic.
		std::vector<size_t> order(sizes.size());
		std::iota(order.begin(), order.end(), size_t{ 0 });
		std::stable_sort(order.begin(), order.end(), [&sizes](size_t a, size_t b)
		{
			return sizes[a].y != sizes[b].y ? sizes[a].y > sizes[b].y : sizes[a].x > sizes[b].x;
		});

		bool found = false;
		int64_t bestArea = 0;
		for (int32_t width = kMinAtlasWidth; width <= maxSize; width <<= 1)
		{
			std::vector<glm::ivec2> trial(sizes.size());
			int32_t x = padding, y = padding, shelfHeight = 0;
			bool fits = true;
			for (const size_t index : order)
			{
				const glm::ivec2 size = sizes[index];
				if (size.x + 2 * padding > width)
				{
					fits = false; // wider than this atlas even on an empty shelf
					break;
				}
				if (x + size.x + padding > width)
				{
					y += shelfHeight + padding;
					x = padding;
					shelfHeight = 0;
				}
				trial[index] = { x, y };
				x += size.x + padding;
				shelfHeight = std::max(shelfHeight, size.y);
			}

			const int32_t height = NextPowerOfTwo(y + shelfHeight + padding);
			if (!fits || height > maxSize)
				continue;
			const int64_t area = static_cast<int64_t>(width) * height;
			if (!found || area < bestArea)
			{
				found = true;
				bestArea = area;
				atlasSize = { width, height };
				positions = std::move(trial);
			}
		}
		return found;
	}

	bool Pack(const std::vector<Source>& sources, int32_t padding, int32_t maxSize, Result& out, std::string& error)
	{
		out = {};
		if (sources.empty())
		{
			error = "No sprites to pack.";
			return false;
		}

		// Decode top row first, the order of the atlas rows; the texture importer sets its own flag per load.
		stbi_set_flip_vertically_on_load(0);
		std::vector<DecodedImage> images;
		images.reserve(sources.size());
		std::vector<glm::ivec2> sizes;
		for (const Source& source : sources)
		{
			DecodedImage image;
			int channels = 0;
			image.Pixels = stbi_load(source.File.string().c_str(), &image.Width, &image.Height, &channels, 4);
			if (!image.Pixels)
			{
				error = "Cannot read '" + source.File.string() + "' as an image.";
				return false;
			}
			sizes.emplace_back(image.Width, image.Height);
			images.push_back(std::move(image));
		}

		std::vector<glm::ivec2> positions;
		glm::ivec2 atlasSize{ 0 };
		if (!PackRects(sizes, padding, maxSize, atlasSize, positions))
		{
			error = "The sprites do not fit in a " + std::to_string(maxSize) + " x " + std::to_string(maxSize) + " atlas.";
			return false;
		}

		out.Size = atlasSize;
		out.Pixels.assign(static_cast<size_t>(atlasSize.x) * static_cast<size_t>(atlasSize.y) * 4, 0);
		for (size_t i = 0; i < images.size(); i++)
		{
			const DecodedImage& image = images[i];
			for (int32_t row = 0; row < image.Height; row++)
			{
				const stbi_uc* from = image.Pixels + static_cast<size_t>(row) * static_cast<size_t>(image.Width) * 4;
				uint8_t* to = out.Pixels.data() + (static_cast<size_t>(positions[i].y + row) * static_cast<size_t>(atlasSize.x) + static_cast<size_t>(positions[i].x)) * 4;
				std::memcpy(to, from, static_cast<size_t>(image.Width) * 4);
			}

			SpriteFrame frame;
			frame.Name = sources[i].Name;
			frame.Rect = { positions[i].x, positions[i].y, image.Width, image.Height };
			out.Frames.push_back(std::move(frame));
		}
		return true;
	}

	bool WritePNG(const std::filesystem::path& path, const Result& atlas)
	{
		if (atlas.Size.x < 1 || atlas.Size.y < 1 || atlas.Pixels.size() != static_cast<size_t>(atlas.Size.x) * static_cast<size_t>(atlas.Size.y) * 4)
			return false;
		return stbi_write_png(path.string().c_str(), atlas.Size.x, atlas.Size.y, 4, atlas.Pixels.data(), atlas.Size.x * 4) != 0;
	}
}
