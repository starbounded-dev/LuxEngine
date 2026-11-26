// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/Texture.h"
#include "Lux/Renderer/TextureScene.h"
#include "Lux/Renderer/TextureStreamSource.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Lux {

	// Per-view material residency. Bindless frame tables hold their own image references
	// until their frame slot is safe to reuse. Releasing a fine texture here only evicts it
	// from future descriptor sets.
	class TextureStreamingCache
	{
	public:
		struct Statistics
		{
			uint32_t StreamedTextures = 0;
			uint32_t FineTextures = 0;
			uint32_t Promotions = 0;
			uint32_t Evictions = 0;
			uint64_t FineBytes = 0;
		};

		Ref<Texture2D> Track(GPUTextureIndex index, const GPUTextureSlot& slot, const Ref<Texture2D>& asset)
		{
			auto& entry = m_Entries[index];
			if (entry.Asset == asset && entry.Slot == slot)
				return entry.Fine ? entry.Fine : entry.Tail;
			entry = {};
			entry.Asset = asset;
			entry.Slot = slot;
			const auto& source = asset->GetStreamSource();
			entry.ResidentMip = source->GetTailMip();
			entry.Tail = slot.ColorSpace == TextureColorSpace::Linear ? asset : CreateMip(entry, entry.ResidentMip);
			return entry.Tail;
		}

		void Untrack(GPUTextureIndex index) { m_Entries.erase(index); }
		bool Empty() const { return m_Entries.empty(); }
		const Statistics& GetStatistics() const { return m_Statistics; }

		void BeginFeedback()
		{
			for (auto& [index, entry] : m_Entries)
				entry.ProjectedPixels = 0.0f;
		}

		void Request(GPUTextureIndex index, float projectedPixels)
		{
			if (const auto it = m_Entries.find(index); it != m_Entries.end())
				it->second.ProjectedPixels = std::max(it->second.ProjectedPixels, projectedPixels);
		}

		template<typename UpdateSlot>
		void Apply(uint64_t budget, UpdateSlot&& updateSlot)
		{
			m_Order.clear();
			for (auto& [index, entry] : m_Entries)
				m_Order.push_back(index);
			std::sort(m_Order.begin(), m_Order.end(), [&](GPUTextureIndex a, GPUTextureIndex b) {
				const float first = m_Entries.at(a).ProjectedPixels, second = m_Entries.at(b).ProjectedPixels;
				return first == second ? a < b : first > second;
			});

			uint64_t remaining = budget;
			for (GPUTextureIndex index : m_Order)
			{
				Entry& entry = m_Entries.at(index);
				const auto& source = entry.Asset->GetStreamSource();
				const auto& base = source->GetLevel(0, false);
				const uint32_t tail = source->GetTailMip();
				const float sourceExtent = static_cast<float>(std::max(base.Width, base.Height));
				const float footprint = sourceExtent / std::max(entry.ProjectedPixels, 1.0f);
				entry.RequestedMip = static_cast<uint32_t>(std::clamp(std::floor(std::log2(footprint)), 0.0f, static_cast<float>(tail)));
				while (entry.RequestedMip < tail && MipBytes(*source, entry.RequestedMip) > remaining)
					++entry.RequestedMip;
				remaining -= MipBytes(*source, entry.RequestedMip);
			}

			// Evict first so a newly visible surface can claim the released budget.
			uint64_t used = 0;
			for (GPUTextureIndex index : m_Order)
			{
				Entry& entry = m_Entries.at(index);
				const auto& source = entry.Asset->GetStreamSource();
				if (entry.RequestedMip > entry.ResidentMip)
				{
					entry.Fine = nullptr;
					entry.ResidentMip = source->GetTailMip();
					updateSlot(index, entry.Tail);
					++m_Statistics.Evictions;
				}
				used += MipBytes(*source, entry.ResidentMip);
			}
			uint32_t uploads = 0;
			uint32_t fineTextures = 0;
			for (GPUTextureIndex index : m_Order)
			{
				Entry& entry = m_Entries.at(index);
				const auto& source = entry.Asset->GetStreamSource();
				if (uploads < 4 && entry.RequestedMip < entry.ResidentMip)
				{
					const uint64_t newBytes = MipBytes(*source, entry.RequestedMip);
					const uint64_t oldBytes = MipBytes(*source, entry.ResidentMip);
					if (used - oldBytes + newBytes <= budget)
					{
						Ref<Texture2D> fine = CreateMip(entry, entry.RequestedMip);
						if (fine)
						{
							entry.Fine = fine;
							entry.ResidentMip = entry.RequestedMip;
							updateSlot(index, fine);
							used = used - oldBytes + newBytes;
							++uploads;
							++m_Statistics.Promotions;
						}
					}
				}
				fineTextures += entry.Fine ? 1u : 0u;
			}
			m_Statistics.StreamedTextures = static_cast<uint32_t>(m_Entries.size());
			m_Statistics.FineTextures = fineTextures;
			m_Statistics.FineBytes = used;
		}

	private:
		struct Entry
		{
			Ref<Texture2D> Asset;
			Ref<Texture2D> Tail;
			Ref<Texture2D> Fine;
			GPUTextureSlot Slot;
			uint32_t ResidentMip = 0;
			uint32_t RequestedMip = 0;
			float ProjectedPixels = 0.0f;
		};

		static uint64_t MipBytes(const TextureStreamSource& source, uint32_t firstMip)
		{
			if (firstMip >= source.GetTailMip())
				return 0;
			uint64_t bytes = 0;
			for (uint32_t mip = firstMip; mip < source.GetMipCount(); ++mip)
				bytes += source.GetLevel(mip, false).Pixels.size();
			return bytes;
		}

		static Ref<Texture2D> CreateMip(const Entry& entry, uint32_t mip)
		{
			const bool srgb = entry.Slot.ColorSpace == TextureColorSpace::SRGB;
			const auto& level = entry.Asset->GetStreamSource()->GetLevel(mip, srgb);
			TextureSpecification spec;
			spec.Width = level.Width;
			spec.Height = level.Height;
			spec.Format = srgb ? ImageFormat::SRGBA : ImageFormat::RGBA;
			spec.DebugName = "Streamed Material Mips";
			Ref<Texture2D> texture = Texture2D::Create(spec, level.View());
			if (srgb && entry.Slot.AlphaCoverageCutoff)
				texture->GenerateMips(float(entry.Slot.AlphaCoverageCutoff) / 255.0f);
			return texture;
		}

		std::unordered_map<GPUTextureIndex, Entry> m_Entries;
		std::vector<GPUTextureIndex> m_Order;
		Statistics m_Statistics;
	};

}
