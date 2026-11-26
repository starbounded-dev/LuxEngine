// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/AssetMetadata.h"
#include "Lux/Core/Buffer.h"
#include "Lux/Core/Ref.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Lux {

	// The source mip chain lives in CPU memory. Only the coarse tail and the requested
	// levels or pages become GPU textures. The same source can supply two interpretations
	// of a map that is used once as colour and once as data.
	class TextureStreamSource : public RefCounted
	{
	public:
		static constexpr uint32_t PageSize = 128;
		static constexpr uint32_t PageBorder = 2;
		static constexpr uint32_t PhysicalPageSize = PageSize + 2 * PageBorder;

		struct Level
		{
			uint32_t Width = 0;
			uint32_t Height = 0;
			std::vector<uint8_t> Pixels;

			Buffer View() const { return Buffer(Pixels.data(), Pixels.size()); }
		};

		struct PageTableLevel
		{
			uint32_t Width;
			uint32_t Height;
			uint32_t FirstRow;
			uint32_t FirstPage;
		};

		TextureStreamSource(TextureResidency mode, uint32_t width, uint32_t height, Buffer rgba)
			: m_Mode(mode)
		{
			const auto* first = static_cast<const uint8_t*>(rgba.Data);
			m_Data.push_back({ width, height, { first, first + size_t(width) * height * 4 } });
			for (uint32_t mip = 0; width > 1 || height > 1; ++mip)
			{
				width = std::max(width / 2, 1u);
				height = std::max(height / 2, 1u);
				m_Colour.push_back(Downsample(mip > 0 ? m_Colour[mip - 1] : m_Data.front(), width, height, true));
				m_Data.push_back(Downsample(m_Data[mip], width, height, false));
			}
			while (m_Data[m_TailMip].Width > PageSize || m_Data[m_TailMip].Height > PageSize)
				++m_TailMip;
			for (uint32_t mip = 0; mip < m_TailMip; ++mip)
			{
				const uint32_t columns = TileCount(m_Data[mip].Width), rows = TileCount(m_Data[mip].Height);
				m_PageTable.push_back({ columns, rows, m_PageTableHeight, m_PageCount });
				m_PageTableWidth = std::max(m_PageTableWidth, columns);
				m_PageTableHeight += rows;
				m_PageCount += columns * rows;
			}
		}

		TextureResidency GetMode() const { return m_Mode; }
		uint32_t GetMipCount() const { return static_cast<uint32_t>(m_Data.size()); }
		uint32_t GetTailMip() const { return m_TailMip; }
		const Level& GetLevel(uint32_t mip, bool srgb) const { return srgb && mip > 0 ? m_Colour[mip - 1] : m_Data[mip]; }
		const PageTableLevel& GetPageTableLevel(uint32_t mip) const { return m_PageTable[mip]; }
		uint32_t GetPageTableWidth() const { return m_PageTableWidth; }
		uint32_t GetPageTableHeight() const { return m_PageTableHeight; }
		uint32_t GetPageCount() const { return m_PageCount; }

		static uint32_t TileCount(uint32_t extent) { return (extent + PageSize - 1) / PageSize; }

		Level CopyPage(uint32_t mip, uint32_t pageX, uint32_t pageY, bool srgb) const
		{
			const Level& level = GetLevel(mip, srgb);
			Level page{ PhysicalPageSize, PhysicalPageSize, std::vector<uint8_t>(size_t(PhysicalPageSize) * PhysicalPageSize * 4) };
			for (uint32_t y = 0; y < PhysicalPageSize; ++y)
			{
				const uint32_t srcY = Wrap(int(pageY * PageSize + y) - int(PageBorder), level.Height);
				for (uint32_t x = 0; x < PhysicalPageSize; ++x)
				{
					const uint32_t srcX = Wrap(int(pageX * PageSize + x) - int(PageBorder), level.Width);
					std::memcpy(&page.Pixels[4 * (size_t(y) * PhysicalPageSize + x)],
						&level.Pixels[4 * (size_t(srcY) * level.Width + srcX)], 4);
				}
			}
			return page;
		}

	private:
		static uint32_t Wrap(int position, uint32_t extent)
		{
			const int remainder = position % static_cast<int>(extent);
			return static_cast<uint32_t>(remainder < 0 ? remainder + static_cast<int>(extent) : remainder);
		}

		static float DecodeSRGB(float channel)
		{
			return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f);
		}

		static float EncodeSRGB(float channel)
		{
			return channel <= 0.0031308f ? channel * 12.92f : 1.055f * std::pow(channel, 1.0f / 2.4f) - 0.055f;
		}

		static Level Downsample(const Level& source, uint32_t width, uint32_t height, bool srgb)
		{
			Level result{ width, height, std::vector<uint8_t>(size_t(width) * height * 4) };
			for (uint32_t y = 0; y < height; ++y)
			{
				for (uint32_t x = 0; x < width; ++x)
				{
					const uint32_t x0 = x * source.Width / width, x1 = (x + 1) * source.Width / width;
					const uint32_t y0 = y * source.Height / height, y1 = (y + 1) * source.Height / height;
					const float weight = 1.0f / static_cast<float>((x1 - x0) * (y1 - y0));
					for (uint32_t channel = 0; channel < 4; ++channel)
					{
						float sum = 0.0f;
						const bool colour = srgb && channel < 3;
						for (uint32_t sy = y0; sy < y1; ++sy)
						{
							for (uint32_t sx = x0; sx < x1; ++sx)
							{
								const float value = float(source.Pixels[4 * (size_t(sy) * source.Width + sx) + channel]) / 255.0f;
								sum += colour ? DecodeSRGB(value) : value;
							}
						}
						const float average = sum * weight;
						const float encoded = colour ? EncodeSRGB(average) : average;
						result.Pixels[4 * (size_t(y) * width + x) + channel] =
							static_cast<uint8_t>(std::round(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
					}
				}
			}
			return result;
		}

		TextureResidency m_Mode;
		uint32_t m_TailMip = 0;
		uint32_t m_PageTableWidth = 2;
		uint32_t m_PageTableHeight = 1;
		uint32_t m_PageCount = 0;
		std::vector<PageTableLevel> m_PageTable;
		std::vector<Level> m_Data;
		std::vector<Level> m_Colour;
	};

}
