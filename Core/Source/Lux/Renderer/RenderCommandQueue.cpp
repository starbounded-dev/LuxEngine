// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RenderCommandQueue.h"

namespace Lux {

	namespace {

		// One page holds a typical frame; a heavy frame chains more pages instead of overrunning.
		constexpr size_t k_RenderCommandPageSize = 4 * 1024 * 1024;

		struct CommandHeader
		{
			RenderCommandQueue::RenderCommandFn Function = nullptr;
			uint32_t PayloadOffset = 0; // from the header to the payload
			uint32_t NextOffset = 0;    // from the header to the next header in the same page
		};

		uintptr_t AlignAddress(uintptr_t address, size_t alignment)
		{
			return (address + alignment - 1) & ~(uintptr_t)(alignment - 1);
		}

		void StoreMax(std::atomic<uint32_t>& target, uint32_t value)
		{
			target.store(std::max(target.load(std::memory_order_relaxed), value), std::memory_order_relaxed);
		}

		void StoreMax(std::atomic<uint64_t>& target, uint64_t value)
		{
			target.store(std::max(target.load(std::memory_order_relaxed), value), std::memory_order_relaxed);
		}

	}

	RenderCommandQueue::RenderCommandQueue()
	{
		AddPage(k_RenderCommandPageSize);
	}

	RenderCommandQueue::~RenderCommandQueue() = default;

	RenderCommandQueue::Page& RenderCommandQueue::AddPage(size_t minimumCapacity)
	{
		Page& page = m_Pages.emplace_back();
		page.Capacity = std::max(k_RenderCommandPageSize, minimumCapacity);
		page.Data = std::make_unique_for_overwrite<uint8_t[]>(page.Capacity);
		return page;
	}

	void* RenderCommandQueue::Allocate(RenderCommandFn fn, uint32_t size, uint32_t alignment)
	{
		LUX_CORE_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0, "Render command alignment must be a power of two");
		alignment = std::max<uint32_t>(alignment, alignof(CommandHeader));

		// Header and payload must sit in one page; the payload is aligned on its real address, so a
		// capture aligned beyond the allocator's guarantee still lands correctly.
		auto place = [&](const Page& page, uintptr_t& header, uintptr_t& payload, uintptr_t& end)
		{
			const uintptr_t base = reinterpret_cast<uintptr_t>(page.Data.get());
			header = AlignAddress(base + page.Used, alignof(CommandHeader));
			payload = AlignAddress(header + sizeof(CommandHeader), alignment);
			end = AlignAddress(payload + size, alignof(CommandHeader));
			return end <= base + page.Capacity;
		};

		uintptr_t header = 0, payload = 0, end = 0;
		while (!place(m_Pages[m_CurrentPage], header, payload, end))
		{
			m_CurrentPage++;
			if (m_CurrentPage == m_Pages.size())
				AddPage(sizeof(CommandHeader) + size + 2 * (size_t)alignment);
		}

		Page& page = m_Pages[m_CurrentPage];
		CommandHeader* command = reinterpret_cast<CommandHeader*>(header);
		command->Function = fn;
		command->PayloadOffset = static_cast<uint32_t>(payload - header);
		command->NextOffset = static_cast<uint32_t>(end - header);
		page.Used = end - reinterpret_cast<uintptr_t>(page.Data.get());

		m_CommandCount++;
		return reinterpret_cast<void*>(payload);
	}

	void RenderCommandQueue::Execute()
	{
		LUX_PROFILE_FUNCTION_AUTO;

		uint64_t bytes = 0;
		const uint32_t pageCount = m_CommandCount > 0 ? m_CurrentPage + 1 : 0;
		for (uint32_t pageIndex = 0; pageIndex < pageCount; pageIndex++)
		{
			Page& page = m_Pages[pageIndex];
			uint8_t* cursor = page.Data.get();
			// Commands start at the first aligned header, which may not be the page's first byte.
			cursor = reinterpret_cast<uint8_t*>(AlignAddress(reinterpret_cast<uintptr_t>(cursor), alignof(CommandHeader)));
			uint8_t* const end = page.Data.get() + page.Used;
			while (cursor < end)
			{
				const CommandHeader* command = reinterpret_cast<const CommandHeader*>(cursor);
				command->Function(cursor + command->PayloadOffset);
				cursor += command->NextOffset;
			}

			bytes += page.Used;
			page.Used = 0;
		}

		m_LastCommandCount.store(m_CommandCount, std::memory_order_relaxed);
		m_LastBytes.store(bytes, std::memory_order_relaxed);
		m_LastPageCount.store(pageCount, std::memory_order_relaxed);
		StoreMax(m_MaxCommandCount, m_CommandCount);
		StoreMax(m_MaxBytes, bytes);
		StoreMax(m_MaxPageCount, pageCount);

		m_CurrentPage = 0;
		m_CommandCount = 0;
	}

	RenderCommandQueueStats RenderCommandQueue::GetStats() const
	{
		RenderCommandQueueStats stats;
		stats.CommandCount = m_LastCommandCount.load(std::memory_order_relaxed);
		stats.MaxCommandCount = m_MaxCommandCount.load(std::memory_order_relaxed);
		stats.Bytes = m_LastBytes.load(std::memory_order_relaxed);
		stats.MaxBytes = m_MaxBytes.load(std::memory_order_relaxed);
		stats.PageCount = m_LastPageCount.load(std::memory_order_relaxed);
		stats.MaxPageCount = m_MaxPageCount.load(std::memory_order_relaxed);
		return stats;
	}

}
