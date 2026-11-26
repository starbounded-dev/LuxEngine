// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/Texture.h"
#include "Lux/Renderer/TextureScene.h"
#include "Lux/Renderer/TextureStreamSource.h"

#include <algorithm>
#include <bit>
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
		static constexpr uint32_t VirtualTextureBit = 0x40000000u;
		static constexpr uint32_t FeedbackPageCapacity = 65536;
		static constexpr uint32_t FeedbackWords = FeedbackPageCapacity / 32;
		static constexpr uint64_t PageBytes = uint64_t(TextureStreamSource::PhysicalPageSize) * TextureStreamSource::PhysicalPageSize * 4;

		struct Statistics
		{
			uint32_t StreamedTextures = 0;
			uint32_t FineTextures = 0;
			uint32_t Promotions = 0;
			uint32_t Evictions = 0;
			uint64_t FineBytes = 0;
			uint32_t VirtualPages = 0;
			uint32_t PageUploads = 0;
			uint32_t PageEvictions = 0;
		};

		Ref<Texture2D> Track(GPUTextureIndex index, const GPUTextureSlot& slot, const Ref<Texture2D>& asset)
		{
			auto& entry = m_Entries[index];
			if (entry.Asset == asset && entry.Slot == slot)
				return entry.Fine ? entry.Fine : entry.Tail;
			++m_LayoutRevision;
			entry = {};
			entry.Asset = asset;
			entry.Slot = slot;
			const auto& source = asset->GetStreamSource();
			entry.ResidentMip = source->GetTailMip();
			entry.Tail = slot.ColorSpace == TextureColorSpace::Linear ? asset : CreateMip(entry, entry.ResidentMip);
			return entry.Tail;
		}

		void Untrack(GPUTextureIndex index) { m_LayoutRevision += m_Entries.erase(index); }
		bool Empty() const { return m_Entries.empty(); }
		const Statistics& GetStatistics() const { return m_Statistics; }
		uint64_t FeedbackGeneration() const { return m_Pages.empty() ? 0 : m_FeedbackGeneration; }
		const std::unordered_map<GPUTextureIndex, GPUTextureIndex>& GetVirtualMapping() const { return m_VirtualMapping; }

		template<typename UpdateSlot>
		void ConfigureVirtual(uint32_t ordinarySlots, uint32_t capacity, UpdateSlot&& updateSlot)
		{
			if (m_ConfiguredRevision == m_LayoutRevision && m_OrdinarySlots == ordinarySlots)
				return;
			m_ConfiguredRevision = m_LayoutRevision;
			m_OrdinarySlots = ordinarySlots;
			++m_FeedbackGeneration;
			for (const auto& [index, tableSlot] : m_VirtualMapping)
				if (tableSlot >= ordinarySlots)
					updateSlot(tableSlot, nullptr);
			for (const Page& page : m_Pages)
				if (page.Slot != 0 && page.Slot >= ordinarySlots)
					updateSlot(page.Slot, nullptr);
			m_VirtualMapping.clear();
			m_Pages.clear();
			m_FreePageSlots.clear();
			m_ResidentPages = 0;

			m_VirtualOrder.clear();
			for (auto& [index, entry] : m_Entries)
			{
				entry.Table = nullptr;
				entry.TablePixels.clear();
				const auto& source = entry.Asset->GetStreamSource();
				if (source->GetMode() == TextureResidency::VirtualPages && entry.Slot.AlphaCoverageCutoff == 0)
					m_VirtualOrder.push_back(index);
			}
			std::sort(m_VirtualOrder.begin(), m_VirtualOrder.end());
			uint32_t top = capacity;
			for (GPUTextureIndex index : m_VirtualOrder)
			{
				Entry& entry = m_Entries.at(index);
				const auto& source = entry.Asset->GetStreamSource();
				if (top <= ordinarySlots + 1 || m_Pages.size() + source->GetPageCount() > FeedbackPageCapacity)
					continue;
				const auto& base = source->GetLevel(0, false);
				entry.TablePixels.assign(size_t(source->GetPageTableWidth()) * source->GetPageTableHeight(), glm::vec4(0.0f));
				entry.TablePixels[0] = glm::vec4(base.Width, base.Height, source->GetTailMip(), index);
				entry.TablePixels[1].x = static_cast<float>(m_Pages.size() + 1);
				TextureSpecification spec;
				spec.Width = source->GetPageTableWidth();
				spec.Height = source->GetPageTableHeight();
				spec.Format = ImageFormat::RGBA32F;
				spec.GenerateMips = false;
				spec.DebugName = "Virtual Texture Page Table";
				entry.Table = Texture2D::Create(spec, Buffer(entry.TablePixels));
				m_VirtualMapping.emplace(index, --top);
				updateSlot(top, entry.Table);
				for (uint32_t mip = 0; mip < source->GetTailMip(); ++mip)
				{
					const auto& level = source->GetPageTableLevel(mip);
					for (uint32_t y = 0; y < level.Height; ++y)
						for (uint32_t x = 0; x < level.Width; ++x)
							m_Pages.push_back({ index, mip, x, y });
				}
			}
			for (uint32_t i = 0; !m_Pages.empty() && i < 512 && top > ordinarySlots; ++i)
				m_FreePageSlots.push_back(--top);
		}

		void ReadFeedback(const std::vector<uint32_t>& words, uint64_t generation)
		{
			++m_FeedbackFrame;
			if (generation != FeedbackGeneration())
				return;
			for (uint32_t word = 0; word < words.size() && size_t(word) * 32 < m_Pages.size(); ++word)
			{
				uint32_t requests = words[word];
				while (requests)
				{
					const uint32_t page = word * 32 + static_cast<uint32_t>(std::countr_zero(requests));
					if (page < m_Pages.size())
						m_Pages[page].LastRequest = m_FeedbackFrame;
					requests &= requests - 1;
				}
			}
		}

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
			const uint64_t totalBudget = budget;
			const bool virtualOnly = !m_VirtualMapping.empty() && m_VirtualMapping.size() == m_Entries.size();
			budget = virtualOnly ? 0 : !m_VirtualMapping.empty() ? budget / 2 : budget;
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
				entry.RequestedMip = entry.Table ? tail :
					static_cast<uint32_t>(std::clamp(std::floor(std::log2(footprint)), 0.0f, static_cast<float>(tail)));
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
			ApplyPages(totalBudget - used, updateSlot);
			m_Statistics.FineBytes = used + m_ResidentPages * PageBytes;
			m_Statistics.VirtualPages = m_ResidentPages;
		}

	private:
		struct Entry
		{
			Ref<Texture2D> Asset;
			Ref<Texture2D> Tail;
			Ref<Texture2D> Fine;
			Ref<Texture2D> Table;
			std::vector<glm::vec4> TablePixels;
			GPUTextureSlot Slot;
			uint32_t ResidentMip = 0;
			uint32_t RequestedMip = 0;
			float ProjectedPixels = 0.0f;
			bool TableDirty = false;
		};

		struct Page
		{
			GPUTextureIndex Owner;
			uint32_t Mip, X, Y;
			uint32_t Slot = 0;
			uint64_t LastRequest = 0;
		};

		void WritePage(const Page& page)
		{
			Entry& entry = m_Entries.at(page.Owner);
			const auto& source = entry.Asset->GetStreamSource();
			const auto& level = source->GetPageTableLevel(page.Mip);
			entry.TablePixels[(level.FirstRow + page.Y) * source->GetPageTableWidth() + page.X].x = static_cast<float>(page.Slot);
			entry.TableDirty = true;
		}

		template<typename UpdateSlot>
		void ApplyPages(uint64_t budget, UpdateSlot&& updateSlot)
		{
			const uint64_t maxResident = budget / PageBytes;
			for (Page& page : m_Pages)
			{
				if (!page.Slot || (m_ResidentPages <= maxResident && m_FeedbackFrame <= page.LastRequest + 12))
					continue;
				updateSlot(page.Slot, nullptr);
				m_FreePageSlots.push_back(page.Slot);
				page.Slot = 0;
				WritePage(page);
				--m_ResidentPages;
				++m_Statistics.PageEvictions;
			}
			uint32_t uploaded = 0;
			for (Page& page : m_Pages)
			{
				if (uploaded >= 8 || m_FreePageSlots.empty() || m_ResidentPages >= maxResident)
					break;
				if (page.Slot || page.LastRequest == 0 || m_FeedbackFrame > page.LastRequest + 5)
					continue;
				const Entry& entry = m_Entries.at(page.Owner);
				const bool srgb = entry.Slot.ColorSpace == TextureColorSpace::SRGB;
				const auto pixels = entry.Asset->GetStreamSource()->CopyPage(page.Mip, page.X, page.Y, srgb);
				TextureSpecification spec;
				spec.Width = pixels.Width;
				spec.Height = pixels.Height;
				spec.Format = srgb ? ImageFormat::SRGBA : ImageFormat::RGBA;
				spec.GenerateMips = false;
				spec.DebugName = "Virtual Texture Page";
				Ref<Texture2D> image = Texture2D::Create(spec, pixels.View());
				if (!image)
					continue;
				page.Slot = m_FreePageSlots.back();
				m_FreePageSlots.pop_back();
				updateSlot(page.Slot, image);
				WritePage(page);
				++uploaded;
				++m_ResidentPages;
				++m_Statistics.PageUploads;
			}
			for (auto& [index, entry] : m_Entries)
			{
				if (!entry.Table || !entry.TableDirty)
					continue;
				entry.Table->SetData(Buffer(entry.TablePixels));
				entry.TableDirty = false;
			}
		}

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
		std::vector<GPUTextureIndex> m_VirtualOrder;
		std::unordered_map<GPUTextureIndex, GPUTextureIndex> m_VirtualMapping;
		std::vector<Page> m_Pages;
		std::vector<GPUTextureIndex> m_FreePageSlots;
		Statistics m_Statistics;
		uint64_t m_LayoutRevision = 1;
		uint64_t m_ConfiguredRevision = 0;
		uint64_t m_FeedbackGeneration = 0;
		uint64_t m_FeedbackFrame = 0;
		uint32_t m_OrdinarySlots = 0;
		uint32_t m_ResidentPages = 0;
	};

}
