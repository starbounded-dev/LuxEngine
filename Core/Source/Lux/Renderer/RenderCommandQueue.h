// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"

#include <atomic>
#include <vector>

namespace Lux {

	struct RenderCommandQueueStats
	{
		uint32_t CommandCount = 0, MaxCommandCount = 0;
		uint64_t Bytes = 0, MaxBytes = 0;
		uint32_t PageCount = 0, MaxPageCount = 0;
	};

	// A frame's render commands, stored as {function, payload} records in fixed pages. Allocate never
	// fails and never moves a payload: when a page is full the queue continues in the next one (kept
	// from earlier frames, or allocated once), so a heavy frame costs memory, not a dropped command.
	// Filled by one producer (the main thread, see Renderer::Submit) and executed after the queues swap.
	class RenderCommandQueue
	{
	public:
		typedef void(*RenderCommandFn)(void*);

		RenderCommandQueue();
		~RenderCommandQueue();

		// Storage for a payload of `size` bytes aligned to `alignment` (a power of two), valid until
		// Execute runs `func` on it.
		void* Allocate(RenderCommandFn func, uint32_t size, uint32_t alignment);

		void Execute();
		bool IsEmpty() const { return m_CommandCount == 0; }

		// Usage of the last executed frame and the high-water marks; readable from any thread.
		RenderCommandQueueStats GetStats() const;
	private:
		struct Page
		{
			Scope<uint8_t[]> Data;
			size_t Capacity = 0;
			size_t Used = 0;
		};

		Page& AddPage(size_t minimumCapacity);
	private:
		std::vector<Page> m_Pages;
		uint32_t m_CurrentPage = 0;
		uint32_t m_CommandCount = 0;

		std::atomic<uint32_t> m_LastCommandCount = 0, m_MaxCommandCount = 0;
		std::atomic<uint64_t> m_LastBytes = 0, m_MaxBytes = 0;
		std::atomic<uint32_t> m_LastPageCount = 0, m_MaxPageCount = 0;
	};

}
